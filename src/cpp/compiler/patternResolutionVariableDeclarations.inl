static std::optional<std::string> explicitWholeVariableSourceName(const PatternReference *reference) {
	if (!reference)
		return std::nullopt;
	const std::vector<PatternElement> elements =
		reference->patternElements.empty() ? getPatternElements(reference->pattern.text) : reference->patternElements;
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
	return result;
}

static const std::string *findVisibleExplicitWholeVariableName(const PatternReference *reference) {
	if (!reference || !reference->range().line)
		return nullptr;
	std::optional<std::string> sourceName = explicitWholeVariableSourceName(reference);
	if (!sourceName || sourceName->find(' ') == std::string::npos)
		return nullptr;
	Section *sourceScope = reference->matchingScope ? reference->matchingScope : reference->range().section();
	for (Section *section = sourceScope; section; section = section->parent) {
		auto declaration = section->explicitVariableDeclarations.find(*sourceName);
		if (declaration == section->explicitVariableDeclarations.end())
			continue;
		const Range &declarationRange = declaration->second;
		if (std::pair(declarationRange.line->mergedLineIndex, declarationRange.start()) <
			std::pair(reference->range().line->mergedLineIndex, reference->range().start()))
			return &declaration->first;
	}
	return nullptr;
}

static std::pair<int, int> variableReferenceSourcePosition(const VariableReference *reference) {
	requireCompilerInvariant(reference && reference->range.line, "variable reference has no source position");
	return {reference->range.line->mergedLineIndex, reference->range.start()};
}

static bool
sectionHasVariableReferenceBefore(const Section *section, const std::string &name, const VariableReference *reference) {
	auto found = section->variableReferences.find(name);
	if (found == section->variableReferences.end())
		return false;
	const std::pair<int, int> referencePosition = variableReferenceSourcePosition(reference);
	return std::ranges::any_of(found->second, [&](const VariableReference *candidate) {
		return variableReferenceSourcePosition(candidate) < referencePosition;
	});
}

static void materializeVariableGroup(
	const std::string &name, Section *highestSection, std::vector<VariableReference *> &groupReferences, bool isGlobal
) {
	VariableReference *definition =
		*std::min_element(groupReferences.begin(), groupReferences.end(), [](auto *left, auto *right) {
		return variableReferenceSourcePosition(left) < variableReferenceSourcePosition(right);
	});
	bool groupIsGlobal = isGlobal;
	if (groupIsGlobal) {
		for (Section *ancestor = highestSection; ancestor; ancestor = ancestor->parent) {
			if (ancestor->type == SectionType::Function && !ancestor->isFlex) {
				groupIsGlobal = false;
				break;
			}
		}
	}

	auto existingDefinition = highestSection->variableDefinitions.find(name);
	if (existingDefinition != highestSection->variableDefinitions.end() && existingDefinition->second != definition) {
		VariableReference *oldDefinition = existingDefinition->second;
		auto references = highestSection->variableReferences.find(name);
		if (references != highestSection->variableReferences.end()) {
			auto &values = references->second;
			values.erase(std::remove(values.begin(), values.end(), oldDefinition), values.end());
			if (values.empty())
				highestSection->variableReferences.erase(references);
		}
	}
	highestSection->variableDefinitions[name] = definition;
	auto variable = highestSection->variables.find(name);
	if (variable == highestSection->variables.end()) {
		highestSection->variables.emplace(name, new Variable(name, definition, groupIsGlobal));
	} else {
		requireCompilerInvariant(variable->second, "section variable map contains a null variable");
		*variable->second = Variable(name, definition, groupIsGlobal);
	}
	Variable *materialized = highestSection->variables.at(name);
	for (VariableReference *reference : groupReferences) {
		if (!reference->declaredTypeConstraintName.empty())
			materialized->addDeclaredTypeConstraintReference(reference);
	}
	for (VariableReference *reference : groupReferences) {
		if (reference != definition)
			reference->definition = definition;
	}
}
