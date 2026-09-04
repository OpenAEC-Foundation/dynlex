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

static Section *enclosingVariableFunctionScope(Section *section) {
	Section *functionScope = nullptr;
	for (Section *ancestor = section; ancestor; ancestor = ancestor->parent) {
		if (ancestor->type == SectionType::Function && !ancestor->isFlex) {
			functionScope = ancestor;
			break;
		}
	}
	return functionScope;
}

static bool functionDeclaresGlobalName(const Section *functionScope, const std::string &name) {
	return functionScope && std::ranges::find(functionScope->globalVariables, name) != functionScope->globalVariables.end();
}

static Section *visibleExplicitVariableSection(Section *section, const std::string &name, const VariableReference *reference) {
	Section *functionScope = enclosingVariableFunctionScope(section);
	bool declaredGlobalHere = functionDeclaresGlobalName(functionScope, name);
	const std::pair<int, int> referencePosition = variableReferenceSourcePosition(reference);
	for (Section *current = section; current; current = current->parent) {
		auto declaration = current->explicitVariableDeclarations.find(name);
		if (declaration != current->explicitVariableDeclarations.end()) {
			const Range &range = declaration->second;
			if (std::pair(range.line->mergedLineIndex, range.start()) <= referencePosition)
				return current;
		}
		if (!declaredGlobalHere && current == functionScope)
			break;
	}
	return nullptr;
}

static Section *highestImplicitVariableSection(
	Section *section, const std::string &name,
	const std::unordered_map<Section *, VariableReference *> &earliestImplicitSectionReferences
) {
	Section *highest = section;
	Section *functionScope = enclosingVariableFunctionScope(section);
	bool declaredGlobalHere = false;
	if (functionScope)
		declaredGlobalHere = functionDeclaresGlobalName(functionScope, name);
	for (Section *ancestor = section->parent; ancestor; ancestor = ancestor->parent) {
		if (!declaredGlobalHere && ancestor == functionScope)
			break;
		if (!earliestImplicitSectionReferences.contains(ancestor))
			continue;
		highest = ancestor;
	}
	return highest;
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
