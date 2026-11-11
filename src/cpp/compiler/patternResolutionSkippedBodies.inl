using SkippedCachedBodies = std::unordered_map<Section *, std::vector<PatternReference *>>;

static void
skipCachedBodyReferences(ParseContext &context, std::list<PatternReference *> &bodyReferences, SkippedCachedBodies &skipped) {
	if (!context.selectCachedBodiesForResolutionReuse)
		return;
	std::vector<Section *> selected = context.selectCachedBodiesForResolutionReuse();
	std::unordered_set<Section *> owners(selected.begin(), selected.end());
	std::erase_if(bodyReferences, [&](PatternReference *reference) {
		for (Section *section = reference->range().section(); section; section = section->parent) {
			if (!owners.contains(section))
				continue;
			skipped[section].push_back(reference);
			decrementVariableLikeCounts(reference);
			reference->resolve();
			return true;
		}
		return false;
	});
}

static void reactivateCachedBodies(
	ParseContext &context, std::list<PatternReference *> &bodyReferences, SkippedCachedBodies &skipped,
	const std::vector<Section *> &sections, bool recomputeCounts = true
) {
	std::vector<PatternReference *> references;
	for (Section *section : sections) {
		auto found = skipped.find(section);
		requireCompilerInvariant(found != skipped.end(), "cached body lost its skipped references");
		for (PatternReference *reference : found->second) {
			reference->resolved = false;
			reference->range().section()->incrementUnresolved();
			references.push_back(reference);
		}
		skipped.erase(found);
	}
	if (recomputeCounts) {
		std::list<Section *> countSections(sections.begin(), sections.end());
		computeVariableLikeCounts(context, countSections);
	}
	std::sort(references.begin(), references.end(), referenceComesBefore);
	bodyReferences.insert(bodyReferences.end(), references.begin(), references.end());
}

static std::vector<Section *> cachedBodiesToReactivate(ParseContext &context, const SkippedCachedBodies &skipped) {
	std::vector<Section *> activated;
	if (skipped.empty())
		return activated;
	bool sameTree = context.cachedBodyMatcherTreeUnchanged();
	for (const auto &[section, _] : skipped)
		if (!sameTree)
			activated.push_back(section);
	return activated;
}

// Replaying the saved resolution is necessary even when the body currently
// has no direct call. Later inference can request it through a function value.
static void replayCachedBodies(ParseContext &context, std::list<PatternReference *> &deferred, SkippedCachedBodies &skipped) {
	if (skipped.empty())
		return;
	std::vector<Section *> sections;
	for (const auto &[section, references] : skipped) {
		sections.push_back(section);
		context.cachedBodyResolutionReferencesSkipped += references.size();
	}
	reactivateCachedBodies(context, deferred, skipped, sections, false);
}
