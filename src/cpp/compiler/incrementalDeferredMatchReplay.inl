// The program snapshot owns these entries throughout frontend compilation.
struct PriorMatchKey {
	const Json *entry;
	const Section *owner;
};
using PriorMatchKeys = std::unordered_map<const PatternReference *, PriorMatchKey>;

struct BodyResolutionUnit {
	std::vector<PatternReference *> references;
	std::vector<size_t> deferredIndices;
	std::string key;
};

void prepareDeferredMatchReplay(
	ParseContext &context, const Json &previous, const Json &observations, std::string_view initialMatcherTree,
	MatcherPathSnapshots &matcherPaths, const InitialMatcherProjection &currentProjection, PriorMatchKeys &precedenceKeys,
	std::vector<BodyResolutionUnit> &bodyUnits
) {
	if (!previous.contains("all_function_projection") || !compatibleObservations(previous, observations, context))
		return;
	const Json &projection = previous.at("all_function_projection");
	const Json &prior = projection.at("matcher_projection");
	const Json &current = currentProjection.value;
	auto treeChanges = matcherPaths.compare(
		projection.at("initial_matcher_tree").get_ref<const std::string &>(), initialMatcherTree
	);
	bool sameTree = !treeChanges;
	const Json &priorDeclarations = prior.at("declaration_digests");
	if (sameTree && current.at("declaration_digests") != priorDeclarations)
		return;
	// Matching consumes declarations and lexical bindings, not flex execution.
	// The active tree below already accounts for body-driven parameter promotion.
	bool stableBindings = current.at("selected_digest") == prior.at("selected_digest") &&
						  sourceProjectionsEqual(current.at("sources"), prior.at("sources"));
	if (!stableBindings) {
		stableBindings = sourceProjectionsEqual(current.at("ambient_sources"), prior.at("ambient_sources"));
		if (!stableBindings) {
			if (treeChanges)
				return;
			if (!sourceProjectionsEqual(current.at("ambient_nonexecution_sources"), prior.at("ambient_nonexecution_sources")))
				return;
		}
	}
	if (treeChanges || stableBindings) {
		const Json &rootDeclarations = current.at("global_resolution");
		const Json &priorRoot = prior.at("global_resolution");
		if (rootDeclarations.at(0) != priorRoot.at(0) || rootDeclarations.at(2) != priorRoot.at(2))
			return;
	}
	const Json &bodies = current.at("bodies");
	{
		const Json &scopeRuleBodies = current.at("scope_rule_bodies");
		if (scopeRuleBodies != prior.at("scope_rule_bodies"))
			return;
		for (const auto &[path, _] : scopeRuleBodies.items())
			if (!bodies.contains(path) || !prior.at("bodies").contains(path) || bodies.at(path) != prior.at("bodies").at(path))
				return;
	}
	std::set<std::string> diagnosticSources = previous.at("diagnostic_sources").get<std::set<std::string>>();
	for (const auto &owner : currentProjection.owners) {
		const std::string &path = owner.path;
		const Section *section = owner.section;
		if (!priorDeclarations.contains(path) ||
			(treeChanges && current.at("declaration_digests").at(path) != priorDeclarations.at(path)) ||
			(treeChanges && (section->parent != context.mainSection || treeChanges->mayAffect(*section))) ||
			(!stableBindings && !hasNoGlobalAccess(section)) || !prior.at("bodies").contains(path) || !bodies.contains(path) ||
			bodies.at(path) != prior.at("bodies").at(path))
			continue;
		const Json &entries = prior.at("match_overrides").contains(path) ? prior.at("match_overrides").at(path)
																		 : projection.at("match_entries").at(path);
		const std::vector<PatternReference *> &references = owner.references;
		if (references.size() != entries.size())
			continue;
		bool sameShapes = true;
		for (size_t index = 0; index < references.size(); index++) {
			const Json &shape = entries.at(index).at("shape");
			PatternReference *reference = references[index];
			if (shape.at(0).get_ref<const std::string &>() != reference->pattern.text ||
				shape.at(1).get<int>() != static_cast<int>(reference->patternType) ||
				shape.at(2).get<int>() != static_cast<int>(reference->purpose)) {
				sameShapes = false;
				break;
			}
		}
		if (!sameShapes)
			continue;
		BodyResolutionUnit unit;
		bool restoreUnit = section->parent == context.mainSection &&
						   !diagnosticSources.contains(section->openingLine->sourceFile->uri) &&
						   prior.at("body_units").contains(path);
		size_t deferredIndex = 0;
		for (size_t index = 0; index < references.size(); index++) {
			PatternReference *reference = references[index];
			const Json &entry = entries.at(index);
			bool isDeferred =
				deferredIndex < owner.deferredReferences.size() && owner.deferredReferences[deferredIndex].first == index;
			if (isDeferred) {
				deferredIndex++;
				unit.deferredIndices.push_back(index);
			}
			if (!entry.at("initial_key").is_string())
				continue;
			const std::string &initial = entry.at("initial_key").get_ref<const std::string &>();
			if (isDeferred)
				reference->prevalidatedMatcherKey = initial;
			if (entry.at("key").is_string() && entry.at("key") != entry.at("initial_key"))
				precedenceKeys.emplace(reference, PriorMatchKey{&entry, section});
		}
		if (restoreUnit && !unit.deferredIndices.empty()) {
			unit.references = references;
			unit.key = prior.at("body_units").at(path).get<std::string>();
			bodyUnits.push_back(std::move(unit));
		}
	}
}

std::vector<ParseContext::RestoredBodyReference> restoreBodyResolutionUnits(
	ParseContext &context, IncrementalMatcherCache &matcherCache, std::vector<BodyResolutionUnit> &units
) {
	std::vector<ParseContext::RestoredBodyReference> result;
	for (const BodyResolutionUnit &unit : units) {
		auto decoded = matcherCache.restoreBodyResolution(unit.key);
		if (!decoded)
			continue;
		requireCompilerInvariant(decoded->size() == unit.references.size(), "cached body has mismatched reference count");
		std::vector<ParseContext::RestoredBodyReference> staged;
		for (size_t referenceIndex : unit.deferredIndices) {
			auto &saved = decoded->at(referenceIndex);
			requireCompilerInvariant(saved.referenceIndex == referenceIndex, "cached body reference order changed");
			PatternReference *reference = unit.references.at(referenceIndex);
			ParseContext::RestoredBodyReference restored{reference, {}, {}};
			if (saved.variable) {
				if (!saved.matchKey.empty()) {
					if (!matcherCache.contains(saved.matchKey))
						break;
					matcherCache.retain(saved.matchKey);
				}
				reference->incrementalMatcherKey = saved.matchKey;
				reference->prevalidatedMatcherKey.clear();
				restored.variable = std::move(saved.variable);
			} else {
				MatchDependencies dependencies;
				reference->prevalidatedMatcherKey = saved.matchKey;
				auto match = matcherCache.reuse(reference, {}, &dependencies);
				if (!match || !*match)
					break;
				restored.match.reset(*match);
			}
			staged.push_back(std::move(restored));
		}
		if (staged.size() != unit.deferredIndices.size())
			continue;
		context.bodyResolutionUnitsReused++;
		for (auto &reference : staged)
			result.push_back(std::move(reference));
	}
	units.clear();
	return result;
}

bool changedTreeMayAffectPrecedenceRules(const MatcherTreeChanges &changes, const Section &root) {
	if (!root.patternDefinitions.empty() &&
		(!root.beforePatterns.empty() || !root.afterPatterns.empty() ||
		 root.associativity != PatternAssociativity::Unspecified) &&
		changes.mayAffect(root))
		return true;
	for (const Section *child : root.children)
		if (changedTreeMayAffectPrecedenceRules(changes, *child))
			return true;
	return false;
}

void preparePrecedenceMatchReplay(
	ParseContext &context, const Json &prior, IncrementalMatcherCache &matcherCache, const PriorMatchKeys &precedenceKeys,
	MatcherPathSnapshots &matcherPaths, const std::vector<PatternReference *> &candidates, double &treeMilliseconds, double &precedenceMilliseconds,
	double &ruleMilliseconds, size_t &signaturesReused
) {
	if (precedenceKeys.empty() || context.patternPrecedenceRevision != 1)
		return;
	auto started = Clock::now();
	std::string tree = matcherTreeSignature(context);
	treeMilliseconds += elapsedMilliseconds(started);
	matcherPaths.retain(tree, context);
	auto treeChanges = matcherPaths.compare(prior.at("final_matcher_tree").get_ref<const std::string &>(), tree);
	started = Clock::now();
	std::string rules = precedenceRulesSignature(*context.mainSection);
	ruleMilliseconds += elapsedMilliseconds(started);
	if (rules == prior.at("precedence_rules").get<std::string>() &&
		(!treeChanges || !changedTreeMayAffectPrecedenceRules(*treeChanges, *context.mainSection))) {
		matcherCache.reusePrecedenceSignature(prior.at("precedence_signature").get_ref<const std::string &>());
		signaturesReused++;
	} else {
		started = Clock::now();
		std::string precedence = matcherCache.precedenceSignature();
		precedenceMilliseconds += elapsedMilliseconds(started);
		if (precedence != prior.at("precedence_signature").get<std::string>())
			return;
	}
	std::unordered_map<const Section *, bool> affectedOwners;
	for (PatternReference *reference : candidates) {
		auto old = precedenceKeys.find(reference);
		if (old == precedenceKeys.end())
			continue;
		if (treeChanges) {
			auto [affected, inserted] = affectedOwners.try_emplace(old->second.owner, false);
			if (inserted)
				affected->second = treeChanges->mayAffect(*old->second.owner);
			if (affected->second)
				continue;
		}
		const Json &entry = *old->second.entry;
		if (reference->incrementalMatcherKey == entry.at("initial_key").get_ref<const std::string &>())
			reference->prevalidatedMatcherKey = entry.at("key").get_ref<const std::string &>();
	}
}
