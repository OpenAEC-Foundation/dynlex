static std::optional<std::string> explicitWholeVariableSourceName(const PatternReference *reference) {
	if (!reference)
		return std::nullopt;
	std::vector<PatternElement> parsedElements;
	std::span<const PatternElement> elements(reference->patternElements);
	if (elements.empty()) {
		parsedElements = getPatternElements(reference->pattern.text);
		elements = parsedElements;
	}
	std::string result;
	size_t argumentIndex = 0;
	for (const PatternElement &element : elements) {
		if (element.type != PatternElement::Type::Variable) {
			result += element.text;
			continue;
		}
		std::optional<std::string> numericSpelling = numericPatternArgumentSpelling(reference, argumentIndex++);
		if (!numericSpelling)
			return std::nullopt;
		result += *numericSpelling;
	}
	const size_t first = result.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return std::nullopt;
	const size_t last = result.find_last_not_of(" \t\r\n");
	return result.substr(first, last - first + 1);
}

static bool indexDataDeclarationNames(
	ParseContext &context, const std::list<PatternReference *> &references, std::unordered_set<Section *> &changedScopes
) {
	bool changed = false;
	for (PatternReference *reference : references) {
		if (reference->patternType != SectionType::Section || !reference->range().line->sectionOpening)
			continue;
		// Discover names through the same metadata and matcher as the final
		// header resolution, without resolving or executing the declaration.
		std::unique_ptr<PatternMatch> match(context.match(reference));
		if (match && reference->indexDataDeclarationNames(*match)) {
			changedScopes.insert(reference->range().section());
			changed = true;
		}
	}
	return changed;
}

static const std::string *findVisibleExplicitWholeVariableName(const PatternReference *reference) {
	if (!reference || !reference->range().line)
		return nullptr;
	Section *sourceScope = reference->matchingScope ? reference->matchingScope : reference->range().section();
	bool hasExplicitDeclarations = false;
	for (Section *section = sourceScope; section; section = section->parent) {
		if (!section->explicitVariableDeclarations.empty()) {
			hasExplicitDeclarations = true;
			break;
		}
	}
	if (!hasExplicitDeclarations)
		return nullptr;
	std::optional<std::string> sourceName = explicitWholeVariableSourceName(reference);
	if (!sourceName)
		return nullptr;
	for (Section *section = sourceScope; section; section = section->parent) {
		auto declaration = section->explicitVariableDeclarations.find(*sourceName);
		if (declaration == section->explicitVariableDeclarations.end())
			continue;
		const Range &declarationRange = declaration->second;
		if (section->dataVariableDeclarations.contains(*sourceName))
			return &declaration->first;
		if (sourceName->find(' ') == std::string::npos)
			continue;
		if (std::pair(declarationRange.line->mergedLineIndex, declarationRange.start()) <
			std::pair(reference->range().line->mergedLineIndex, reference->range().start()))
			return &declaration->first;
	}
	return nullptr;
}
