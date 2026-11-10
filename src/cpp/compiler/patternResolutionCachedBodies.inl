static void restoreDeferredBodyReferences(
	ParseContext &context, std::list<PatternReference *> &references,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &definitionToReferences
) {
	if (!context.restoreDeferredBodyUnits)
		return;
	std::vector<ParseContext::RestoredBodyReference> restored = context.restoreDeferredBodyUnits();
	std::unordered_set<PatternReference *> restoredReferences;
	for (auto &result : restored) {
		PatternReference *reference = result.reference;
		if (result.match) {
			reference->resolve(result.match.release());
			addVariableReferencesFromMatch(context, reference, *reference->match);
			trackMatchDefinitions(*reference->match, reference, definitionToReferences);
		} else {
			auto &[element, start, end] = result.variable.value();
			Range variableRange = reference->range().subRange(start, end);
			reference->patternElements = {std::move(element)};
			reference->resolve();
			reference->variableResolutionOffsets = std::pair{start, end};
			reference->range().section()->addVariableReference(
				context, context.createVariableReference(variableRange, reference->patternElements.front().text)
			);
		}
		restoredReferences.insert(reference);
	}
	context.bodyResolutionReferencesRestored += restored.size();
	references.remove_if([&](PatternReference *reference) {
		return restoredReferences.contains(reference);
	});
}
