// Cached headers are provisional until their changed providers publish the same
// signatures. A changed export restarts the complete affected dependency cycle.
template <typename QueueReference>
static bool reactivateCachedClassifications(
	ParseContext &context, SkippedCachedBodies &skipped, std::list<PatternReference *> &bodyReferences,
	std::list<Section *> &unresolvedSections,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &definitionToReferences,
	QueueReference queueReference
) {
	if (!context.invalidateCachedClassifications)
		return false;
	std::vector<Section *> sections = context.invalidateCachedClassifications();
	if (sections.empty())
		return false;
	for (Section *section : sections) {
		requireCompilerInvariant(skipped.contains(section), "invalidated classification has no skipped body");
		for (PatternDefinition *definition : section->patternDefinitions) {
			requireCompilerInvariant(
				definition->resolved && definition->indexedTree, "invalidated cached header is not indexed"
			);
			if (auto found = definitionToReferences.find(definition); found != definitionToReferences.end()) {
				std::vector<PatternReference *> references = found->second;
				for (PatternReference *reference : references)
					queueReference(reference);
			}
			definition->indexedTree->removePatternDefinition(definition);
			definition->resolved = false;
		}
		section->patternDefinitionsResolved = false;
		// Parsed explicit bindings remain owned by their original statements.
		// Only names counted before body classification can be cached promotions.
		cleanupStaleImplicitPromotionsInSection(context, section, [&](const std::string &name) {
			return section->variableLikeCounts.contains(name);
		});
		unresolvedSections.push_back(section);
	}
	deferSkippedBodyReferences(bodyReferences, skipped, sections);
	std::list<Section *> countSections(sections.begin(), sections.end());
	computeVariableLikeCounts(context, countSections, true);
	return true;
}
