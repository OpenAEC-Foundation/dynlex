std::optional<std::unordered_set<std::string>> dirtySectionsFromSavedGraph(
	const Json &graph, const std::map<std::string, Section *> &sections,
	const std::unordered_map<const Section *, bool> &unchanged, const std::unordered_set<const Section *> &stableBoundaries
) {
	const Json &forward = graph.at("forward");
	const Json &reverse = graph.at("reverse");
	if (forward.size() != sections.size())
		return std::nullopt;
	std::unordered_set<std::string> dirty;
	std::vector<std::string> pending;
	auto markDirty = [&](const std::string &path) {
		auto found = sections.find(path);
		if (found == sections.end())
			return false;
		if (!stableBoundaries.contains(found->second) && dirty.insert(path).second)
			pending.push_back(path);
		return true;
	};
	for (const auto &[path, section] : sections) {
		if (!forward.contains(path))
			return std::nullopt;
		if (!unchanged.at(section) && !markDirty(path))
			return std::nullopt;
	}
	for (const Json &path : graph.at("unknown"))
		if (!markDirty(path.get<std::string>()))
			return std::nullopt;
	while (!pending.empty()) {
		std::string path = std::move(pending.back());
		pending.pop_back();
		if (!reverse.contains(path))
			continue;
		for (const Json &caller : reverse.at(path))
			if (!markDirty(caller.get<std::string>()))
				return std::nullopt;
	}
	return dirty;
}

std::unordered_set<const Section *> eligibleCodegenSections(
	const ParseContext &context, const Json &previous, const Json &currentObservations, const Json &currentProjection,
	const std::map<std::string, Section *> &sections, const std::unordered_set<const Section *> &interfaceStableChanges = {},
	size_t *graphReused = nullptr
) {
	std::unordered_set<const Section *> eligible;
	if (context.options.optimizationLevel || context.options.emitDebugInfo || context.options.traceExecution ||
		!previous.contains("all_function_projection") || !previous.contains("function_sections") ||
		!compatibleObservations(previous, currentObservations, context))
		return eligible;
	const Json &priorProjection = previous.at("all_function_projection");
	bool fullyCompatible = compatibleFunctionProjection(priorProjection, currentProjection);
	bool globalFreeCompatible = compatibleGlobalFreeProjection(priorProjection, currentProjection);
	if (!fullyCompatible && !globalFreeCompatible)
		return eligible;
	std::unordered_map<const Section *, bool> unchanged;
	for (const auto &[path, section] : sections)
		unchanged[section] =
			(fullyCompatible || (section != context.mainSection && hasNoGlobalAccess(section))) &&
			(!reusableCodegenSection(*section) || unchangedFunctionSource(path, priorProjection, currentProjection) ||
			 interfaceStableChanges.contains(section));
	if (fullyCompatible && previous.contains("section_dependency_graph")) {
		auto dirty =
			dirtySectionsFromSavedGraph(previous.at("section_dependency_graph"), sections, unchanged, interfaceStableChanges);
		if (dirty) {
			for (const auto &[path, section] : sections)
				if (reusableCodegenSection(*section) && !interfaceStableChanges.contains(section) && !dirty->contains(path)) {
					eligible.insert(section);
					if (graphReused)
						(*graphReused)++;
				}
			return eligible;
		}
	}
	std::unordered_map<Section *, bool> memo;
	for (const auto &[_, section] : sections)
		if (reusableCodegenSection(*section) && !interfaceStableChanges.contains(section) &&
			functionDependenciesUnchanged(section, unchanged, memo, interfaceStableChanges))
			eligible.insert(section);
	return eligible;
}
