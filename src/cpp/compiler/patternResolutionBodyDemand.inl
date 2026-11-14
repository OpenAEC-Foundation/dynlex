void ParseContext::restoreBodyReference(RestoredBodyReference result) {
	PatternReference *reference = result.reference;
	if (reference->resolved) {
		reference->resolved = false;
		reference->range().section()->incrementUnresolved();
	}
	if (result.match) {
		reference->resolve(result.match.release());
		addVariableReferencesFromMatch(*this, reference, *reference->match);
	} else {
		auto &[element, start, end] = result.variable.value();
		Range variableRange = reference->range().subRange(start, end);
		reference->patternElements = {std::move(element)};
		reference->resolve();
		reference->variableResolutionOffsets = std::pair{start, end};
		reference->range().section()->addVariableReference(
			*this, createVariableReference(variableRange, reference->patternElements.front().text)
		);
	}
	if (restoredBodyReference)
		restoredBodyReference(reference);
	bodyResolutionReferencesRestored++;
}
