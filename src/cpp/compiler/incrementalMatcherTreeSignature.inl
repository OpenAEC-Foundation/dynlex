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
		const PatternTreeMutationState &state = *root->mutationState;
		append(state.wildcardDigest);
		std::vector<std::pair<std::string_view, std::string_view>> literals;
		literals.reserve(state.literalDigests.size());
		for (const auto &[literal, digest] : state.literalDigests)
			literals.emplace_back(literal, digest);
		std::sort(literals.begin(), literals.end());
		append(std::to_string(literals.size()));
		for (const auto &[literal, digest] : literals) {
			append(literal);
			append(digest);
		}
	}
	return llvm::toHex(hash.final(), true);
}

Json matcherTreeMutationSnapshot(const ParseContext &context) {
	Json roots = Json::array();
	for (const PatternTreeNode *root : context.patternTrees) {
		requireCompilerInvariant(root && root->mutationState, "incremental pattern tree has no mutation state");
		Json literals = Json::object();
		for (const auto &[literal, digest] : root->mutationState->literalDigests)
			literals[llvm::toHex(llvm::StringRef(literal), true)] = digest;
		roots.push_back(Json{{"wildcard", root->mutationState->wildcardDigest}, {"literals", std::move(literals)}});
	}
	return roots;
}

struct MatcherTreeChanges {
	bool wildcard = false;
	std::vector<std::string> literals;

	MatcherTreeChanges(const Json &prior, const Json &current) {
		requireCompilerInvariant(prior.size() == current.size(), "incremental matcher tree root count changed");
		for (size_t index = 0; index < current.size(); index++) {
			const Json &oldRoot = prior.at(index);
			const Json &newRoot = current.at(index);
			wildcard = wildcard || oldRoot.at("wildcard") != newRoot.at("wildcard");
			const Json &oldLiterals = oldRoot.at("literals");
			const Json &newLiterals = newRoot.at("literals");
			for (const auto &[hex, digest] : oldLiterals.items())
				if (!newLiterals.contains(hex) || newLiterals.at(hex) != digest)
					literals.push_back(llvm::fromHex(hex));
			for (const auto &[hex, _] : newLiterals.items())
				if (!oldLiterals.contains(hex))
					literals.push_back(llvm::fromHex(hex));
		}
		std::sort(literals.begin(), literals.end());
		literals.erase(std::unique(literals.begin(), literals.end()), literals.end());
	}

	bool mayAffect(const Section &section) const {
		if (wildcard)
			return true;
		const auto containsChangedStart = [&](std::string_view text) {
			return std::ranges::any_of(literals, [&](const std::string &literal) {
				return text.find(literal) != std::string_view::npos;
			});
		};
		const auto visit = [&](auto &&self, const Section &body) -> bool {
			if (body.openingLine && containsChangedStart(body.openingLine->fullText))
				return true;
			for (const CodeLine *line : body.codeLines)
				if (containsChangedStart(line->fullText))
					return true;
			for (const PatternReference *reference : body.patternReferences) {
				if (containsChangedStart(reference->pattern.text))
					return true;
				if (reference->expression)
					for (size_t index = 0; index < reference->expression->arguments.size(); index++) {
						std::optional<std::string> spelling = numericPatternArgumentSpelling(reference, index);
						if (spelling && containsChangedStart(*spelling))
							return true;
					}
			}
			for (const Section *child : body.children)
				if (child->patternDefinitions.empty() && self(self, *child))
					return true;
			return false;
		};
		return visit(visit, section);
	}
};
