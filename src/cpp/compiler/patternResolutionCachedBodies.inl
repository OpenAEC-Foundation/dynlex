static void restoreDeferredBodyReferences(ParseContext &context, std::list<PatternReference *> &references) {
	if (!context.restoreDeferredBodyUnits)
		return;
	std::vector<ParseContext::RestoredBodyReference> restored = context.restoreDeferredBodyUnits();
	std::unordered_set<PatternReference *> restoredReferences;
	for (auto &result : restored) {
		PatternReference *reference = result.reference;
		context.restoreBodyReference(std::move(result));
		restoredReferences.insert(reference);
	}
	references.remove_if([&](PatternReference *reference) {
		return restoredReferences.contains(reference) ||
			   (context.deferredBodyResolution && context.deferredBodyResolution(reference->range().section()));
	});
}
