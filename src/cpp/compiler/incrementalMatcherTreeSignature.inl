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
