static const std::string *findVisibleExplicitWholeVariableName(const PatternReference *reference) {
	if (!reference || !reference->range().line || reference->pattern.text.find(' ') == std::string::npos)
		return nullptr;
	for (Section *section = reference->range().section(); section; section = section->parent) {
		auto name = section->explicitVariableNames.find(reference->pattern.text);
		if (name != section->explicitVariableNames.end())
			return &*name;
	}
	return nullptr;
}

static void materializeVariableGroup(
	const std::string &name, Section *highestSection, std::vector<VariableReference *> &groupReferences, bool isGlobal
) {
	VariableReference *definition =
		*std::min_element(groupReferences.begin(), groupReferences.end(), [](auto *left, auto *right) {
		return left->range.line->mergedLineIndex < right->range.line->mergedLineIndex;
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
