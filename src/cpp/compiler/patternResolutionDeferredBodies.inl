// A body can wait until all definitions are indexed when it cannot classify
// an enclosing pattern parameter. Its eventual match still contributes normal
// variable bindings, diagnostics, and precedence information.
static bool bodyReferenceCanWaitForDefinitions(const PatternReference *reference) {
	if (reference->purpose != PatternReference::Purpose::Expression)
		return false;
	bool foundOrdinaryFunction = false;
	for (Section *section = reference->range().section(); section; section = section->parent) {
		if (!section->globalVariables.empty())
			return false;
		if (section->patternDefinitions.empty())
			continue;
		if (!foundOrdinaryFunction) {
			if (section->type != SectionType::Function || section->isConversion || section->dataSection)
				return false;
			foundOrdinaryFunction = true;
		}
		for (PatternDefinition *definition : section->patternDefinitions) {
			bool canClassifyParameter = false;
			forEachLeafElement(definition->patternElements, [&](const DefinitionPatternElement &element) {
				canClassifyParameter |= section->canPromoteImplicitParameter(*definition, element) &&
										section->variableLikeCounts.at(element.text) != 0;
			});
			if (canClassifyParameter)
				return false;
		}
	}
	return foundOrdinaryFunction;
}
