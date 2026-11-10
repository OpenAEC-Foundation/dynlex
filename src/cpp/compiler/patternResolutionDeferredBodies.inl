// An expression reference can wait until all definitions are indexed when it
// cannot classify an enclosing pattern parameter. Its eventual match contributes
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
		// The count map was populated only with promotable definition words.
		for (const PatternElement &element : reference->patternElements) {
			if (element.type != PatternElement::Type::VariableLike)
				continue;
			auto candidate = section->variableLikeCounts.find(element.text);
			if (candidate != section->variableLikeCounts.end() && candidate->second != 0)
				return false;
		}
	}
	return foundOrdinaryFunction;
}
