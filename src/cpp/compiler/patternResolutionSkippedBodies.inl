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

static void deferSkippedBodyReferences(
	std::list<PatternReference *> &bodyReferences, SkippedCachedBodies &skipped, const std::vector<Section *> &sections
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
	std::sort(references.begin(), references.end(), referenceComesBefore);
	bodyReferences.insert(bodyReferences.end(), references.begin(), references.end());
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
	deferSkippedBodyReferences(deferred, skipped, sections);
}
