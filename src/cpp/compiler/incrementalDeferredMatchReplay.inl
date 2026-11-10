// The program snapshot owns these entries throughout frontend compilation.
struct PriorMatchKey {
	const Json *entry;
	const Section *owner;
};
using PriorMatchKeys = std::unordered_map<const PatternReference *, PriorMatchKey>;

void prepareDeferredMatchReplay(
	ParseContext &context, const Json &previous, const Json &observations, std::string_view initialMatcherTree,
	const Json &initialMatcherRoots, const std::list<PatternReference *> &deferredReferences, PriorMatchKeys &precedenceKeys
) {
	if (!previous.contains("all_function_projection") || !compatibleObservations(previous, observations, context))
		return;
	const Json &projection = previous.at("all_function_projection");
	const Json &prior = projection.at("matcher_projection");
	bool sameTree = projection.at("initial_matcher_tree").get_ref<const std::string &>() == initialMatcherTree;
	std::optional<MatcherTreeChanges> treeChanges;
	if (!sameTree)
		treeChanges.emplace(projection.at("initial_matcher_roots"), initialMatcherRoots);
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	std::unordered_set<const Section *> reusable;
	std::unordered_set<const Section *> functionBodies;
	Json selected = Json::object();
	for (const auto &[path, section] : sections) {
		if (reusableCodegenSection(*section))
			reusable.insert(section);
		if (section->type == SectionType::Function && !section->patternDefinitions.empty() &&
			!containsNestedDefinition(*section)) {
			functionBodies.insert(section);
			selected[path] = sectionIdentity(*section);
		}
	}
	const Json &priorDeclarations = prior.at("declaration_digests");
	if (sameTree && reusable.size() != priorDeclarations.size())
		return;
	for (const auto &[path, section] : sections)
		if (sameTree && reusable.contains(section) &&
			(!priorDeclarations.contains(path) ||
			 digest(sectionDeclaration(*section).dump()) != priorDeclarations.at(path).get_ref<const std::string &>()))
			return;
	// Matching consumes declarations and lexical bindings, not flex execution.
	// The active tree below already accounts for body-driven parameter promotion.
	bool stableBindings =
		digest(selected.dump()) == prior.at("selected_digest").get_ref<const std::string &>() &&
		sourceProjectionsEqual(
			sourceProjection(context, observations, functionBodies, false, false, false, &prior.at("sources")),
			prior.at("sources")
		);
	if (!stableBindings) {
		Json ambient = sourceProjection(context, observations, reusable, true, true, false, &prior.at("ambient_sources"));
		stableBindings = sourceProjectionsEqual(ambient, prior.at("ambient_sources"));
		if (!stableBindings) {
			if (treeChanges)
				return;
			Json nonexecution =
				sourceProjection(context, observations, reusable, true, true, true, &prior.at("ambient_nonexecution_sources"));
			if (!sourceProjectionsEqual(nonexecution, prior.at("ambient_nonexecution_sources")))
				return;
		}
	}
	if (treeChanges || stableBindings) {
		Json rootDeclarations = globalResolutionIdentity(*context.mainSection);
		const Json &priorRoot = prior.at("global_resolution");
		if (rootDeclarations.at(0) != priorRoot.at(0) || rootDeclarations.at(2) != priorRoot.at(2))
			return;
	}
	Json bodies = functionBodyDigests(context, sections);
	{
		Json scopeRuleBodies = scopeRuleBodyPaths(sections);
		if (scopeRuleBodies != prior.at("scope_rule_bodies"))
			return;
		for (const auto &[path, _] : scopeRuleBodies.items())
			if (!bodies.contains(path) || !prior.at("bodies").contains(path) || bodies.at(path) != prior.at("bodies").at(path))
				return;
	}
	std::unordered_set<const PatternReference *> deferred(deferredReferences.begin(), deferredReferences.end());
	for (const auto &[path, section] : sections) {
		if (!reusable.contains(section) || !priorDeclarations.contains(path) ||
			(treeChanges &&
			 digest(sectionDeclaration(*section).dump()) != priorDeclarations.at(path).get_ref<const std::string &>()) ||
			(treeChanges && (section->parent != context.mainSection || treeChanges->mayAffect(*section))) ||
			(!stableBindings && !hasNoGlobalAccess(section)) || !prior.at("bodies").contains(path) || !bodies.contains(path) ||
			bodies.at(path) != prior.at("bodies").at(path))
			continue;
		const Json &entries = prior.at("match_overrides").contains(path) ? prior.at("match_overrides").at(path)
																		 : projection.at("match_entries").at(path);
		std::vector<PatternReference *> references;
		const auto collect = [&](auto &&self, Section *body) -> void {
			references.insert(references.end(), body->patternReferences.begin(), body->patternReferences.end());
			for (Section *child : body->children)
				if (child->patternDefinitions.empty())
					self(self, child);
		};
		collect(collect, section);
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
		for (size_t index = 0; index < references.size(); index++) {
			PatternReference *reference = references[index];
			const Json &entry = entries.at(index);
			if (!entry.at("initial_key").is_string())
				continue;
			const std::string &initial = entry.at("initial_key").get_ref<const std::string &>();
			if (deferred.contains(reference))
				reference->prevalidatedMatcherKey = initial;
			if (entry.at("key").is_string() && entry.at("key") != entry.at("initial_key"))
				precedenceKeys.emplace(reference, PriorMatchKey{&entry, section});
		}
	}
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
	const std::vector<PatternReference *> &candidates, double &treeMilliseconds, double &precedenceMilliseconds,
	double &ruleMilliseconds, size_t &signaturesReused
) {
	if (precedenceKeys.empty() || context.patternPrecedenceRevision != 1)
		return;
	auto started = Clock::now();
	std::string tree = matcherTreeSignature(context);
	treeMilliseconds += elapsedMilliseconds(started);
	std::optional<MatcherTreeChanges> treeChanges;
	if (tree != prior.at("final_matcher_tree").get<std::string>())
		treeChanges.emplace(prior.at("final_matcher_roots"), matcherTreeMutationSnapshot(context));
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
