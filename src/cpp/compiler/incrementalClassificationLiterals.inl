// Include punctuation substrings because matcher input can split Other tokens.
template <typename Element> static size_t matcherTokenCount(const std::vector<Element> &elements) {
	size_t count = 0;
	for (const auto &element : elements)
		if (element.type == PatternElement::Type::Other)
			count += std::ranges::count_if(element.text, [](unsigned char character) {
				return !std::isspace(character);
			});
		else
			count++;
	return count;
}

static bool patternCanBeEmpty(const std::vector<DefinitionPatternElement> &elements) {
	return std::ranges::all_of(elements, [](const DefinitionPatternElement &element) {
		if (element.type == PatternElement::Type::Choice)
			return std::ranges::any_of(element.alternatives, patternCanBeEmpty);
		return element.type == PatternElement::Type::Other && std::ranges::all_of(element.text, [](unsigned char character) {
			return std::isspace(character);
		});
	});
}

static void addMatcherStartLiterals(std::string_view text, std::unordered_set<std::string_view> &literals) {
	for (size_t runStart = 0; runStart < text.size();) {
		auto category = [](unsigned char character) {
			if (std::isalnum(character) || character == '_')
				return 0;
			return character == ' ' ? 1 : 2;
		};
		int runCategory = category(static_cast<unsigned char>(text[runStart]));
		size_t runEnd = runStart + 1;
		while (runEnd < text.size() && category(static_cast<unsigned char>(text[runEnd])) == runCategory)
			runEnd++;
		if (runCategory == 0)
			literals.insert(text.substr(runStart, runEnd - runStart));
		else
			for (size_t start = runStart; start < runEnd; start++)
				for (size_t end = start + 1; end <= runEnd; end++)
					literals.insert(text.substr(start, end - start));
		runStart = runEnd;
	}
}
