std::unordered_set<std::string> dirtySectionsFromSavedGraph(
	const Json &graph, const std::map<std::string, Section *> &sections,
	const std::unordered_map<const Section *, bool> &unchanged, const std::unordered_set<const Section *> &stableBoundaries,
	bool matchingEnvironmentChanged, const IncrementalInferenceReads::Keys &changedLookups
) {
	const Json &forward = graph.at("forward");
	const Json &reverse = graph.at("reverse");
	std::unordered_set<std::string> dirty;
	std::vector<std::string> pending;
	auto markDirty = [&](const std::string &path) {
		auto found = sections.find(path);
		if ((found == sections.end() || !stableBoundaries.contains(found->second)) && dirty.insert(path).second)
			pending.push_back(path);
	};
	std::unordered_map<const Section *, std::string_view> currentPaths;
	if (matchingEnvironmentChanged)
		for (const auto &[path, section] : sections)
			currentPaths.emplace(section, path);
	for (const auto &[path, section] : sections) {
		if (!forward.contains(path) || !unchanged.at(section)) {
			markDirty(path);
			continue;
		}
		if (matchingEnvironmentChanged) {
			std::vector<Section *> currentMatches;
			collectMatchedSectionDependencies(section, currentMatches);
			const Json &saved = forward.at(path);
			if (std::ranges::any_of(currentMatches, [&](const Section *dependency) {
					return std::ranges::none_of(saved, [&](const Json &edge) {
						return edge.get_ref<const std::string &>() == currentPaths.at(dependency);
					});
				}))
				markDirty(path);
		}
	}
	for (const auto &[path, _] : forward.items())
		if (!sections.contains(path))
			markDirty(path);
	for (const std::string &key : changedLookups)
		for (const Json &owner : graph.at("callable_lookups").at(key).at("owners"))
			markDirty(owner.get_ref<const std::string &>());
	while (!pending.empty()) {
		std::string path = std::move(pending.back());
		pending.pop_back();
		if (!reverse.contains(path))
			continue;
		for (const Json &caller : reverse.at(path))
			markDirty(caller.get_ref<const std::string &>());
	}
	return dirty;
}

// The execution source projection already proves the same top-level
// statements. A body edit can shift their physical line numbers.
bool sameRootGlobalBindings(const Json &prior, const Json &current) {
	for (size_t group = 0; group < 2; group++) {
		const Json &before = prior.at(group);
		const Json &after = current.at(group);
		if (before.size() != after.size())
			return false;
		for (const auto &[name, location] : before.items()) {
			auto binding = after.find(name);
			if (binding == after.end() || location.at(0) != binding->at(0) || location.at(2) != binding->at(2))
				return false;
		}
	}
	return prior.at(2) == current.at(2);
}

struct IncrementalCodegenReusePlan {
	std::unordered_set<const Section *> functions;
	bool unchangedRoot = false;
};

IncrementalCodegenReusePlan planCodegenReuse(
	const ParseContext &context, const Json &previous, const Json &currentObservations, const Json &currentProjection,
	const std::map<std::string, Section *> &sections, const IncrementalInferenceReads::Keys &changedLookups,
	const std::unordered_set<const Section *> &interfaceStableChanges, size_t *graphReused
) {
	IncrementalCodegenReusePlan plan;
	if (context.options.emitDebugInfo || context.options.traceExecution || !previous.contains("all_function_projection") ||
		!previous.contains("function_sections") || !compatibleObservations(previous, currentObservations, context))
		return plan;
	const Json &priorProjection = previous.at("all_function_projection");
	bool fullyCompatible = compatibleFunctionProjection(priorProjection, currentProjection);
	bool globalFreeCompatible = compatibleGlobalFreeProjection(priorProjection, currentProjection);
	if (!fullyCompatible && !globalFreeCompatible)
		return plan;
	std::unordered_map<const Section *, bool> unchanged;
	// A top-level execution edit does not change an unchanged function body.
	// Global readers remain candidates: summary replay checks current global
	// types, and IR import checks the finalized global types and initializers.
	for (const auto &[path, section] : sections)
		unchanged[section] = (fullyCompatible || section != context.mainSection) &&
							 (unchangedSectionBody(*section, path, priorProjection, currentProjection) ||
							  interfaceStableChanges.contains(section));
	auto dirty = dirtySectionsFromSavedGraph(
		previous.at("section_dependency_graph"), sections, unchanged, interfaceStableChanges,
		priorProjection.at("final_matcher_tree") != currentProjection.at("final_matcher_tree"), changedLookups
	);
	plan.unchangedRoot = fullyCompatible && !dirty.contains("root") &&
						 priorProjection.at("declarations") == currentProjection.at("declarations") &&
						 priorProjection.at("initial_matcher_tree") == currentProjection.at("initial_matcher_tree") &&
						 priorProjection.at("final_matcher_tree") == currentProjection.at("final_matcher_tree") &&
						 sameRootGlobalBindings(priorProjection.at("global_resolution"), currentProjection.at("global_resolution")) &&
						 priorProjection.at("precedence_signature") == currentProjection.at("precedence_signature") &&
						 priorProjection.at("precedence_rules") == currentProjection.at("precedence_rules") &&
						 priorProjection.at("scope_rule_bodies") == currentProjection.at("scope_rule_bodies");
	for (const auto &[path, section] : sections)
		if (reusableCodegenSection(*section) && !interfaceStableChanges.contains(section) &&
			!dirty.contains(path)) {
			plan.functions.insert(section);
			if (graphReused)
				(*graphReused)++;
		}
	return plan;
}
