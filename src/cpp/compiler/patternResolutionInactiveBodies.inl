using SkippedInactiveBodies = std::unordered_map<Section *, std::vector<PatternReference *>>;

static void skipInactiveBodyReferences(
	ParseContext &context, std::list<PatternReference *> &bodyReferences, SkippedInactiveBodies &skipped
) {
	if (!context.selectInactiveBodiesForResolutionReuse)
		return;
	std::vector<Section *> selected = context.selectInactiveBodiesForResolutionReuse();
	std::unordered_set<Section *> owners(selected.begin(), selected.end());
	std::erase_if(bodyReferences, [&](PatternReference *reference) {
		for (Section *section = reference->range().section(); section; section = section->parent) {
			if (!owners.contains(section))
				continue;
			skipped[section].push_back(reference);
			reference->patternElements.clear();
			reference->resolve();
			return true;
		}
		return false;
	});
}

static void reactivateInactiveBodies(
	ParseContext &context, std::list<PatternReference *> &bodyReferences, SkippedInactiveBodies &skipped,
	const std::vector<Section *> &sections
) {
	std::list<Section *> countSections;
	std::vector<PatternReference *> references;
	for (Section *section : sections) {
		auto found = skipped.find(section);
		requireCompilerInvariant(found != skipped.end(), "inactive body lost its skipped references");
		for (PatternReference *reference : found->second) {
			reference->resolved = false;
			reference->range().section()->incrementUnresolved();
			reference->patternElements = getPatternElements(reference->pattern.text);
			references.push_back(reference);
		}
		countSections.push_back(section);
		skipped.erase(found);
	}
	computeVariableLikeCounts(context, countSections);
	std::sort(references.begin(), references.end(), referenceComesBefore);
	bodyReferences.insert(bodyReferences.end(), references.begin(), references.end());
}

static std::vector<Section *> inactiveBodiesToReactivate(
	ParseContext &context, const SkippedInactiveBodies &skipped,
	const std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &definitionToReferences
) {
	std::vector<Section *> activated;
	if (skipped.empty())
		return activated;
	bool sameTree = context.inactiveBodyMatcherTreeUnchanged();
	for (const auto &[section, _] : skipped) {
		bool called = std::ranges::any_of(section->patternDefinitions, [&](PatternDefinition *definition) {
			return definitionToReferences.contains(definition);
		});
		if (!sameTree || called)
			activated.push_back(section);
	}
	return activated;
}

// Replaying the saved resolution is necessary even when the body currently
// has no direct call. Later inference can request it through a function value.
static void replayInactiveBodies(
	ParseContext &context, std::list<PatternReference *> &deferred, SkippedInactiveBodies &skipped
) {
	std::vector<Section *> sections;
	for (const auto &[section, references] : skipped) {
		sections.push_back(section);
		context.inactiveBodyResolutionReferencesSkipped += references.size();
	}
	reactivateInactiveBodies(context, deferred, skipped, sections);
}
