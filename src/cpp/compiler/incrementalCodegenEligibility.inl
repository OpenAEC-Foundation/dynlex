std::unordered_set<const Section *> eligibleCodegenSections(
	const ParseContext &context, const Json &previous, const Json &currentObservations, const Json &currentProjection,
	const std::map<std::string, Section *> &sections, const std::unordered_set<const Section *> &interfaceStableChanges = {}
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
	std::unordered_map<Section *, bool> memo;
	for (const auto &[_, section] : sections)
		if (reusableCodegenSection(*section) && !interfaceStableChanges.contains(section) &&
			functionDependenciesUnchanged(section, unchanged, memo, interfaceStableChanges))
			eligible.insert(section);
	return eligible;
}
