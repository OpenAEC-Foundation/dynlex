void appendParsedDefinitionElement(std::string &output, Range root, const DefinitionPatternElement &element) {
	appendParseByte(output, static_cast<unsigned char>(element.type));
	appendParseString(output, element.text);
	appendParseLength(output, element.startPos);
	appendParseString(output, element.typeConstraintName);
	unsigned char flags = (element.isArgumentPack ? 1 : 0) | (element.isExplicitLiteral ? 2 : 0) |
						  (element.isLiteralInSingleWordSpelling ? 4 : 0) |
						  (element.firstDuplicateVariableLikeRange.line ? 8 : 0);
	appendParseByte(output, flags);
	if (element.firstDuplicateVariableLikeRange.line)
		appendParseRange(output, root, element.firstDuplicateVariableLikeRange);
	appendParseLength(output, element.alternatives.size());
	for (const auto &alternative : element.alternatives) {
		appendParseLength(output, alternative.size());
		for (const DefinitionPatternElement &child : alternative)
			appendParsedDefinitionElement(output, root, child);
	}
}

DefinitionPatternElement readParsedDefinitionElement(std::string_view input, size_t &position, Range root) {
	auto type = static_cast<PatternElement::Type>(readParseByte(input, position));
	if (type >= PatternElement::Type::Count)
		throw std::runtime_error("invalid parsed definition element type");
	DefinitionPatternElement element(type, std::string(readParseString(input, position)));
	element.startPos = readParseLength(input, position);
	if (element.startPos > root.subString.size())
		throw std::runtime_error("invalid parsed definition element offset");
	element.typeConstraintName = std::string(readParseString(input, position));
	unsigned char flags = readParseByte(input, position);
	if (flags & ~15)
		throw std::runtime_error("invalid parsed definition element flags");
	element.isArgumentPack = flags & 1;
	element.isExplicitLiteral = flags & 2;
	element.isLiteralInSingleWordSpelling = flags & 4;
	if (flags & 8)
		element.firstDuplicateVariableLikeRange = readParseRange(input, position, root);
	std::uint64_t alternatives = readParseLength(input, position);
	if (alternatives > input.size() - position)
		throw std::runtime_error("invalid parsed definition alternative count");
	for (std::uint64_t index = 0; index < alternatives; index++) {
		std::uint64_t children = readParseLength(input, position);
		if (children > input.size() - position)
			throw std::runtime_error("invalid parsed definition child count");
		auto &alternative = element.alternatives.emplace_back();
		for (std::uint64_t child = 0; child < children; child++)
			alternative.push_back(readParsedDefinitionElement(input, position, root));
	}
	return element;
}
