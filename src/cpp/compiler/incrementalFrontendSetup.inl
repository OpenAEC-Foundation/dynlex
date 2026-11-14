InitialParameterCounts
	initialParameterCounts(context, frontendState, recording->observations, statistics.parameterCountCacheMilliseconds);
IncrementalClassification classification(
	context, matcherCache, frontendState, recording->observations, initialParameterCounts, statistics.classification
);
context.selectCachedBodiesForResolutionReuse = [&]() {
	auto selectionStart = Clock::now();
	std::vector<Section *> selected = classification.select();
	statistics.cachedBodySelectionMilliseconds += elapsedMilliseconds(selectionStart);
	return selected;
};
context.invalidateCachedClassifications = [&]() {
	return classification.invalidated();
};
std::string initialMatcherTree;
MatcherPathSnapshots matcherPaths(directory, compilerIdentity);
InitialMatcherProjection initialMatcherProjection;
PriorMatchKeys precedenceKeys;
std::vector<BodyResolutionUnit> bodyUnits;
context.reuseMatcherResult = std::bind_front(&IncrementalMatcherCache::reuse, &matcherCache);
context.recordMatcherResult = std::bind_front(&IncrementalMatcherCache::record, &matcherCache);
context.prepareDeferredMatcherReplay = [&](const std::list<PatternReference *> &references) {
	auto projectionStart = Clock::now();
	classification.recordOutputs();
	const Json *prior = frontendState && compatibleObservations(*frontendState, recording->observations, context)
							? &frontendState->at("all_function_projection")
							: nullptr;
	initialMatcherTree = matcherTreeSignature(context);
	initialMatcherProjection.capture(
		context, recording->observations, references, prior, initialParameterCounts.bodies(), initialParameterCounts.sections(),
		initialMatcherTree
	);
	statistics.matcherProjectionMilliseconds += elapsedMilliseconds(projectionStart);
	statistics.matcherDeclarationsReused = initialMatcherProjection.declarationsReused;
	matcherPaths.retain(initialMatcherTree, context);
	if (frontendState)
		prepareDeferredMatchReplay(
			context, *frontendState, recording->observations, initialMatcherTree, matcherPaths, matcherCache,
			initialMatcherProjection, precedenceKeys, bodyUnits
		);
};
context.restoreDeferredBodyUnits = [&]() {
	return restoreBodyResolutionUnits(context, matcherCache, bodyUnits);
};
context.preparePrecedenceMatcherReplay = [&](const std::vector<PatternReference *> &references) {
	initialMatcherProjection.captureBodyUnits(matcherCache);
	if (frontendState)
		preparePrecedenceMatchReplay(
			context, frontendState->at("all_function_projection"), matcherCache, precedenceKeys, matcherPaths, references,
			statistics.precedenceTreeSignatureMilliseconds, statistics.precedenceSignatureMilliseconds,
			statistics.precedenceRuleSignatureMilliseconds, statistics.precedenceSignaturesReused
		);
};
