InitialParameterCounts
	initialParameterCounts(context, frontendState, recording->observations, statistics.parameterCountCacheMilliseconds);
IncrementalClassification classification(
	context, matcherCache, frontendState, recording->observations, initialParameterCounts, statistics.classification
);
#include "incrementalSourceOwners.inl"
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
IncrementalBodyDemand bodyDemand(context, matcherCache, frontendState, matcherPaths, initialParameterCounts);
context.sourceOwnerReferencesPublished = [&](Section *section, std::vector<PatternReference *> references) {
	classification.sourceReferencesPublished(section, references);
};
context.reuseMatcherResult = std::bind_front(&IncrementalMatcherCache::reuse, &matcherCache);
context.recordMatcherResult = std::bind_front(&IncrementalMatcherCache::record, &matcherCache);
context.prepareDeferredMatcherReplay = [&](std::list<PatternReference *> &references) {
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
	context.sourceOwnerReferencesPublished = [&](Section *section, std::vector<PatternReference *> source) {
		classification.sourceReferencesPublished(section, source);
		auto owner = std::ranges::find(initialMatcherProjection.owners, section, &InitialMatcherProjection::Owner::section);
		requireCompilerInvariant(
			owner != initialMatcherProjection.owners.end(), "retained source owner lost its matcher identity"
		);
		requireCompilerInvariant(source.size() == owner->referenceCount, "source owner reference order changed");
		owner->references = std::move(source);
	};
	statistics.matcherProjectionMilliseconds += elapsedMilliseconds(projectionStart);
	statistics.matcherDeclarationsReused = initialMatcherProjection.declarationsReused;
	matcherPaths.retain(initialMatcherTree, context);
	if (frontendState)
		prepareDeferredMatchReplay(
			context, *frontendState, recording->observations, initialMatcherTree, matcherPaths, matcherCache,
			initialMatcherProjection, precedenceKeys, bodyUnits
		);
	std::unordered_set<const Section *> retained;
	for (const auto &unit : bodyUnits)
		retained.insert(unit.section);
	std::vector<Section *> demanded;
	for (const auto &[owner, _] : context.retainedSourceOwners)
		if (!retained.contains(owner))
			demanded.push_back(const_cast<Section *>(owner));
	for (Section *owner : demanded) {
		context.materializeSourceOwnerReferences(owner);
		const auto source = sourceOwnerReferences(*owner);
		references.insert(references.end(), source.begin(), source.end());
	}
};
context.restoreDeferredBodyUnits = [&](std::list<PatternReference *> &references) {
	return bodyDemand.restore(bodyUnits, initialMatcherTree, references);
};
context.preparePrecedenceMatcherReplay = [&](const std::vector<PatternReference *> &references) {
	initialMatcherProjection.captureBodyUnits(context, matcherCache, initialParameterCounts.sections());
	if (frontendState)
		preparePrecedenceMatchReplay(
			context, frontendState->at("all_function_projection"), matcherCache, precedenceKeys, matcherPaths, references,
			statistics.precedenceTreeSignatureMilliseconds, statistics.precedenceSignatureMilliseconds,
			statistics.precedenceRuleSignatureMilliseconds, statistics.precedenceSignaturesReused
		);
};
