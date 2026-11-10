std::string matcherTreeSignature(const ParseContext &context) {
	llvm::BLAKE3 hash;
	auto append = [&](std::string_view value) {
		std::array<char, 8> length;
		std::uint64_t size = value.size();
		for (char &byte : length) {
			byte = static_cast<char>(size & 0xff);
			size >>= 8;
		}
		hash.update(llvm::StringRef(length.data(), length.size()));
		if (!value.empty())
			hash.update(llvm::StringRef(value.data(), value.size()));
	};
	for (const PatternTreeNode *root : context.patternTrees) {
		requireCompilerInvariant(root && root->mutationState, "incremental pattern tree has no mutation state");
		append(root->mutationState->definitionDigest);
	}
	return llvm::toHex(hash.final(), true);
}

Json matcherTreeMutationSnapshot(const ParseContext &context) {
	Json roots = Json::array();
	for (const PatternTreeNode *root : context.patternTrees) {
		requireCompilerInvariant(root && root->mutationState, "incremental pattern tree has no mutation state");
		Json paths = Json::object();
		std::string path;
		auto visit = [&](auto &&self, const PatternTreeSignatureNode &node) -> void {
			if (!node.digest.empty())
				paths[path] = node.digest;
			for (const auto &[literal, child] : node.children) {
				size_t length = path.size();
				if (length)
					path += ',';
				path += llvm::toHex(llvm::StringRef(literal), true);
				self(self, *child);
				path.resize(length);
			}
		};
		visit(visit, root->mutationState->signatures);
		roots.push_back(std::move(paths));
	}
	return roots;
}

const Json &finalMatcherRootSnapshot(const Json &projection) {
	if (projection.at("final_matcher_tree") == projection.at("initial_matcher_tree"))
		return projection.at("initial_matcher_roots");
	return projection.at("final_matcher_roots");
}

struct MatcherTreeChanges {
	bool wildcard = false;
	std::vector<std::vector<std::string>> paths;

	MatcherTreeChanges(const Json &prior, const Json &current) {
		requireCompilerInvariant(prior.size() == current.size(), "incremental matcher tree root count changed");
		std::set<std::string_view> changed;
		for (size_t index = 0; index < current.size(); index++) {
			const Json &oldRoot = prior.at(index);
			const Json &newRoot = current.at(index);
			for (const auto &[path, digest] : oldRoot.items())
				if (!newRoot.contains(path) || newRoot.at(path) != digest)
					changed.insert(path);
			for (const auto &[path, _] : newRoot.items())
				if (!oldRoot.contains(path))
					changed.insert(path);
		}
		wildcard = changed.contains("");
		if (wildcard)
			return;
		for (std::string_view encoded : changed) {
			llvm::SmallVector<llvm::StringRef, 8> literals;
			llvm::StringRef(encoded).split(literals, ',');
			std::vector<std::string> &required = paths.emplace_back();
			for (llvm::StringRef literal : literals)
				required.push_back(llvm::fromHex(literal));
			// Check longer, more selective words first.
			std::ranges::sort(required, std::greater{}, &std::string::size);
		}
	}

	bool mayAffect(const Section &section) const {
		if (wildcard)
			return true;
		const auto contains = [&](auto &&self, const Section &body, std::string_view literal) -> bool {
			if (body.openingLine && body.openingLine->fullText.find(literal) != std::string_view::npos)
				return true;
			for (const CodeLine *line : body.codeLines)
				if (line->fullText.find(literal) != std::string_view::npos)
					return true;
			for (const PatternReference *reference : body.patternReferences) {
				if (reference->pattern.text.find(literal) != std::string_view::npos)
					return true;
				if (reference->expression)
					for (size_t index = 0; index < reference->expression->arguments.size(); index++) {
						std::optional<std::string> spelling = numericPatternArgumentSpelling(reference, index);
						if (spelling && spelling->find(literal) != std::string::npos)
							return true;
					}
			}
			for (const Section *child : body.children)
				if (child->patternDefinitions.empty() && self(self, *child, literal))
					return true;
			return false;
		};
		std::unordered_map<std::string_view, bool> present;
		for (const auto &path : paths)
			if (std::ranges::all_of(path, [&](const std::string &literal) {
					auto [entry, inserted] = present.try_emplace(literal);
					if (inserted)
						entry->second = contains(contains, section, literal);
					return entry->second;
				}))
				return true;
		return false;
	}
};
