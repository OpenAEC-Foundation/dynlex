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

Json captureInactiveProjection(
	const ParseContext &context, const Json &observations, const Json *priorInactive = nullptr,
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
	return Json{{"inactive", inactive}, {"declarations", declarationDigest(*knownSections)}, {"sources", std::move(sources)}};
}
