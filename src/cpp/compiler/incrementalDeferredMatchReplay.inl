// The program snapshot owns these entries throughout frontend compilation.
using PriorMatchKeys = std::unordered_map<const PatternReference *, const Json *>;

struct MatcherTreeChanges {
	bool wildcard = false;
	std::vector<std::string> literals;

	MatcherTreeChanges(const Json &prior, const Json &current) {
		requireCompilerInvariant(prior.size() == current.size(), "incremental matcher tree root count changed");
		for (size_t index = 0; index < current.size(); index++) {
			const Json &oldRoot = prior.at(index);
			const Json &newRoot = current.at(index);
			wildcard = wildcard || oldRoot.at("wildcard") != newRoot.at("wildcard");
			const Json &oldLiterals = oldRoot.at("literals");
			const Json &newLiterals = newRoot.at("literals");
			for (const auto &[hex, digest] : oldLiterals.items())
				if (!newLiterals.contains(hex) || newLiterals.at(hex) != digest)
					literals.push_back(llvm::fromHex(hex));
			for (const auto &[hex, _] : newLiterals.items())
				if (!oldLiterals.contains(hex))
					literals.push_back(llvm::fromHex(hex));
		}
		std::sort(literals.begin(), literals.end());
		literals.erase(std::unique(literals.begin(), literals.end()), literals.end());
	}

	bool mayAffect(const Section &section) const {
		if (wildcard)
			return true;
		const auto containsChangedStart = [&](std::string_view text) {
			return std::ranges::any_of(literals, [&](const std::string &literal) {
				return text.find(literal) != std::string_view::npos;
			});
		};
		const auto visit = [&](auto &&self, const Section &body) -> bool {
			if (body.openingLine && containsChangedStart(body.openingLine->fullText))
				return true;
			for (const CodeLine *line : body.codeLines)
				if (containsChangedStart(line->fullText))
					return true;
			for (const PatternReference *reference : body.patternReferences) {
				if (containsChangedStart(reference->pattern.text))
					return true;
				if (reference->expression)
					for (size_t index = 0; index < reference->expression->arguments.size(); index++) {
						std::optional<std::string> spelling = numericPatternArgumentSpelling(reference, index);
						if (spelling && containsChangedStart(*spelling))
							return true;
					}
			}
			for (const Section *child : body.children)
				if (child->patternDefinitions.empty() && self(self, *child))
					return true;
			return false;
		};
		return visit(visit, section);
	}
};

void prepareDeferredMatchReplay(
	ParseContext &context, const Json &previous, const Json &observations, std::string_view initialMatcherTree,
	const Json &initialMatcherRoots, const std::list<PatternReference *> &deferredReferences,
	PriorMatchKeys &precedenceKeys
) {
	if (!previous.contains("all_function_projection") || !compatibleObservations(previous, observations, context))
		return;
	const Json &prior = previous.at("all_function_projection");
	bool sameTree = prior.at("initial_matcher_tree").get_ref<const std::string &>() == initialMatcherTree;
	std::optional<MatcherTreeChanges> treeChanges;
	if (!sameTree)
		treeChanges.emplace(prior.at("initial_matcher_roots"), initialMatcherRoots);
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	std::unordered_set<const Section *> reusable;
	for (const auto &[_, section] : sections)
		if (reusableCodegenSection(*section))
			reusable.insert(section);
	const Json &priorDeclarations = prior.at("function_declarations");
	if (sameTree && reusable.size() != priorDeclarations.size())
		return;
	for (const auto &[path, section] : sections)
		if (sameTree && reusable.contains(section) &&
			(!priorDeclarations.contains(path) || sectionDeclaration(*section) != priorDeclarations.at(path)))
			return;
	Json ambient = sourceProjection(context, observations, reusable, true, true, false, &prior.at("ambient_sources"));
	bool stableAmbient = sourceProjectionsEqual(ambient, prior.at("ambient_sources"));
	if (!stableAmbient) {
		if (treeChanges)
			return;
		Json nonexecution =
			sourceProjection(context, observations, reusable, true, true, true, &prior.at("ambient_nonexecution_sources"));
		if (!sourceProjectionsEqual(nonexecution, prior.at("ambient_nonexecution_sources")))
			return;
	}
	if (treeChanges) {
		Json rootDeclarations = globalResolutionIdentity(*context.mainSection);
		const Json &priorRoot = prior.at("global_resolution");
		if (rootDeclarations.at(0) != priorRoot.at(0) || rootDeclarations.at(2) != priorRoot.at(2))
			return;
	}
	Json bodies = functionBodyDigests(context, sections);
	if (treeChanges) {
		Json scopeRuleBodies = scopeRuleBodyPaths(sections);
		if (scopeRuleBodies != prior.at("scope_rule_bodies"))
			return;
		for (const auto &[path, _] : scopeRuleBodies.items())
			if (!bodies.contains(path) || !prior.at("bodies").contains(path) ||
				bodies.at(path) != prior.at("bodies").at(path))
				return;
	}
	std::unordered_set<const PatternReference *> deferred(deferredReferences.begin(), deferredReferences.end());
	for (const auto &[path, section] : sections) {
		if (!reusable.contains(section) || !priorDeclarations.contains(path) ||
			(treeChanges && sectionDeclaration(*section) != priorDeclarations.at(path)) ||
			(treeChanges && (section->parent != context.mainSection || treeChanges->mayAffect(*section))) ||
			(!stableAmbient && !hasNoGlobalAccess(section)) ||
			!prior.at("bodies").contains(path) || !bodies.contains(path) || bodies.at(path) != prior.at("bodies").at(path) ||
			!prior.at("match_entries").contains(path))
			continue;
		const Json &entries = prior.at("match_entries").at(path);
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
				precedenceKeys.emplace(reference, &entry);
		}
	}
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
	if (tree != prior.at("final_matcher_tree").get<std::string>())
		return;
	started = Clock::now();
	std::string rules = precedenceRulesSignature(*context.mainSection);
	ruleMilliseconds += elapsedMilliseconds(started);
	if (rules == prior.at("precedence_rules").get<std::string>()) {
		matcherCache.reusePrecedenceSignature(prior.at("precedence_signature").get_ref<const std::string &>());
		signaturesReused++;
	} else {
		started = Clock::now();
		std::string precedence = matcherCache.precedenceSignature();
		precedenceMilliseconds += elapsedMilliseconds(started);
		if (precedence != prior.at("precedence_signature").get<std::string>())
			return;
	}
	for (PatternReference *reference : candidates) {
		auto old = precedenceKeys.find(reference);
		if (old == precedenceKeys.end())
			continue;
		const Json &entry = *old->second;
		if (reference->incrementalMatcherKey == entry.at("initial_key").get_ref<const std::string &>())
			reference->prevalidatedMatcherKey = entry.at("key").get_ref<const std::string &>();
	}
}
