using SkippedCachedBodies = std::unordered_map<Section *, std::vector<PatternReference *>>;

static void skipCachedBodyReferences(
	ParseContext &context, std::list<PatternReference *> &bodyReferences, SkippedCachedBodies &skipped,
	std::vector<PatternReference *> &definitionReferences
) {
	if (!context.selectCachedBodiesForResolutionReuse)
		return;
	std::vector<Section *> selected = context.selectCachedBodiesForResolutionReuse();
	std::unordered_set<Section *> owners(selected.begin(), selected.end());
	for (Section *owner : selected)
		if (context.retainedSourceOwners.contains(owner)) {
			skipped.try_emplace(owner);
			for (auto &[_, count] : owner->variableLikeCounts)
				count = 0;
		}
	bodyReferences.clear();
	std::list<PatternReference *> globals;
	std::list<Section *> definitions;
	context.mainSection->collectPatternReferencesAndSections(bodyReferences, globals, definitions);
	definitionReferences.assign(bodyReferences.begin(), bodyReferences.end());
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
	ParseContext &context, std::list<PatternReference *> &bodyReferences, SkippedCachedBodies &skipped,
	const std::vector<Section *> &sections
) {
	std::vector<PatternReference *> references;
	for (Section *section : sections) {
		auto found = skipped.find(section);
		requireCompilerInvariant(found != skipped.end(), "cached body lost its skipped references");
		bool held = context.retainedSourceOwners.contains(section);
		if (held) {
			context.materializeSourceOwnerReferences(section);
			found->second = sourceOwnerReferences(*section);
		}
		for (PatternReference *reference : found->second) {
			if (held)
				requireCompilerInvariant(!reference->resolved, "new owner reference was already resolved");
			else {
				requireCompilerInvariant(reference->resolved, "skipped owner reference was not resolved");
				reference->resolved = false;
				reference->range().section()->incrementUnresolved();
			}
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
	for (auto entry = skipped.begin(); entry != skipped.end();) {
		auto &[section, references] = *entry;
		if (auto held = context.retainedSourceOwners.find(section); held != context.retainedSourceOwners.end()) {
			context.cachedBodyResolutionReferencesSkipped += held->second.referenceCount;
			entry = skipped.erase(entry);
		} else {
			sections.push_back(section);
			context.cachedBodyResolutionReferencesSkipped += references.size();
			++entry;
		}
	}
	deferSkippedBodyReferences(context, deferred, skipped, sections);
}
