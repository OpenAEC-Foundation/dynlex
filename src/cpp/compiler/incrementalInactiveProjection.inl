// Reused IR can contain a compile-time-folded call whose callee has no emitted
// function. Its old instantiation is absent from this compilation's Section,
// but the callee body still contributes to that IR and must remain active.
std::set<std::string> retainedActiveFunctionPaths(const IncrementalIRReuse *reuse, const Json *previous) {
	std::set<std::string> active;
	if (!reuse)
		return active;
	requireCompilerInvariant(previous != nullptr, "reused IR has no previous inactive projection");
	const Json &owners = previous->at("function_sections");
	const Json &forward = previous->at("section_dependency_graph").at("forward");
	const Json &priorInactive = previous->at("inactive_projection").at("inactive");
	std::set<std::string> visited;
	std::stack<std::string> pending;
	if (reuse->rootReused())
		pending.push("root");
	for (const std::string &name : reuse->reusedFunctionNames())
		pending.push(owners.at(name).get<std::string>());
	while (!pending.empty()) {
		std::string path = pending.top();
		pending.pop();
		if (!visited.insert(path).second)
			continue;
		if (!priorInactive.contains(path))
			active.insert(path);
		if (forward.contains(path))
			for (const Json &dependency : forward.at(path))
				pending.push(dependency.get<std::string>());
	}
	return active;
}

// Rejected trials and restored constraints can consume a body without
// leaving an instantiation or emitted function.
void retainInferenceInputPaths(const Json &schedule, const Json &graph, std::set<std::string> &active) {
	std::stack<std::string> pending;
	auto add = [&](const Json &paths) {
		for (const Json &path : paths)
			pending.push(path.get<std::string>());
	};
	for (const Json &roots : schedule.at("roots"))
		add(roots);
	for (const auto &[_, reads] : graph.at("inference_reads").items())
		add(reads);
	for (const auto &[_, lookup] : graph.at("callable_lookups").items()) {
		const Json &candidates = lookup.at("candidates");
		if (candidates.size() == 1)
			pending.push(candidates.at(0).at(0).get<std::string>());
	}
	std::unordered_set<std::string> visited;
	while (!pending.empty()) {
		std::string path = std::move(pending.top());
		pending.pop();
		if (!visited.insert(path).second)
			continue;
		active.insert(path);
		for (const Json &dependency : graph.at("forward").at(path))
			pending.push(dependency.get<std::string>());
	}
}

Json captureInactiveProjection(
	const Json &resolvedDeclarations, const ParseContext &context, const Json &observations,
	const Json *priorInactive = nullptr,
	const std::set<std::string> *generatedFunctionPaths = nullptr,
	const std::map<std::string, Section *> *knownSections = nullptr, const Json *priorSources = nullptr,
	SourceProjectionWork *projectionWork = nullptr, const Json *requiredSources = nullptr,
	const std::set<std::string> *retainedActivePaths = nullptr
) {
	std::map<std::string, Section *> collectedSections;
	if (!knownSections) {
		collectSections(context.mainSection, "root", collectedSections);
		knownSections = &collectedSections;
	}
	Json inactive = priorInactive ? *priorInactive : inactiveFunctionSections(*knownSections);
	if (generatedFunctionPaths) {
		for (const std::string &path : *generatedFunctionPaths)
			inactive.erase(path);
	}
	if (retainedActivePaths)
		for (const std::string &path : *retainedActivePaths)
			inactive.erase(path);
	std::unordered_set<const Section *> selected;
	for (const auto &[path, identity] : inactive.items()) {
		auto section = knownSections->find(path);
		if (section == knownSections->end() || sectionIdentity(*section->second) != identity)
			return nullptr;
		selected.insert(section->second);
	}
	Json sources = sourceProjection(context, observations, selected, false, false, false, priorSources, projectionWork);
	if (requiredSources && !sourceProjectionsEqual(sources, *requiredSources))
		return nullptr;
	return Json{{"inactive", inactive}, {"declarations", resolvedDeclarations}, {"sources", std::move(sources)}};
}
