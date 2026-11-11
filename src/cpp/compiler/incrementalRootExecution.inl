// Entry replay requires stable execution dependencies and the same typed
// declaration inputs, including any restored class layouts.
bool reusableRootExecution(
	const ParseContext &context, const Json &previous, const Json &frontendDiagnostics,
	const std::map<const Section *, std::string> &sectionPaths
) {
	if (context.options.noMain || !context.scopeRuleWarnings.empty() ||
		context.diagnostics.size() != frontendDiagnostics.size() ||
		!sameFrontendDiagnosticRecords(frontendDiagnostics, previous.at("diagnostic_records")) ||
		context.constraintValuesReused != context.constraintProbeItems.size())
		return false;
	for (const auto &[section, _] : sectionPaths)
		if (section->isExposed || section->type == SectionType::Retain || section->type == SectionType::Release)
			return false;
	return captureClassInstantiations(sectionPaths) == previous.at("all_function_projection").at("class_instantiations");
}

void recordIRReuseStatistics(
	BuildStatistics &statistics, const IncrementalIRReuse &reuse, const Json &previous,
	const std::map<const Section *, std::string> &sectionPaths
) {
	statistics.reusedFunctionIR = reuse.reusedCount();
	statistics.cachedIRDependencyScans = reuse.dependencyScans();
	statistics.cachedIRDependencyHits = reuse.dependencyHits();
	if (!reuse.rootReused())
		return;

	// Entry replay consumes saved instantiations without creating an
	// Instantiation in the current context or calling the summary loader.
	auto retained = retainedGraphVariants(&reuse, &previous);
	const Json &summaries = previous.at("inference_summaries");
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	for (const auto &[section, path] : sectionPaths) {
		auto variants = retained.keysByPath.find(path);
		if (variants == retained.keysByPath.end() || !summaries.contains(path))
			continue;
		for (const auto &[key, _] : section->instantiations)
			if (auto identity = inferenceSummaryKey(key, classPaths))
				variants->second.erase(*identity);
		for (const std::string &key : variants->second)
			if (summaries.at(path).contains(key))
				statistics.reusedInferenceInstantiations++;
	}
}
