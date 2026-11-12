// Entry replay requires stable execution dependencies and the same typed
// declaration inputs, including any restored class layouts.
bool prepareRootClassLayouts(const Json &saved, const std::map<const Section *, std::string> &sectionPaths) {
	Json current = captureClassInstantiations(sectionPaths);
	if (current.size() != saved.size())
		return false;
	Json missing = Json::object();
	for (const auto &[path, entry] : saved.items()) {
		if (!current.contains(path) || current.at(path).at("source") != entry.at("source"))
			return false;
		const Json &instances = current.at(path).at("instances");
		if (instances == entry.at("instances"))
			continue;
		if (!instances.empty())
			return false;
		missing[path] = entry;
	}
	if (missing.empty())
		return true;
	std::map<std::string, Section *> sections;
	for (const auto &[section, path] : sectionPaths)
		sections.emplace(path, const_cast<Section *>(section));
	return restoreClassInstantiations(missing, sections);
}

bool reusableRootExecution(
	const ParseContext &context, const Json &previous, const Json &frontendDiagnostics,
	const std::map<const Section *, std::string> &sectionPaths
) {
	if (context.options.noMain || !context.scopeRuleWarnings.empty() ||
		context.diagnostics.size() != frontendDiagnostics.size() ||
		!sameFrontendDiagnosticRecords(frontendDiagnostics, previous.at("frontend_diagnostic_records")) ||
		previous.at("root_diagnostics").is_null() || context.constraintValuesReused != context.constraintProbeItems.size())
		return false;
	return prepareRootClassLayouts(previous.at("all_function_projection").at("class_instantiations"), sectionPaths);
}

bool reuseRootWithGlobalTypes(
	ParseContext &context, const Json &previous, const std::map<const Section *, std::string> &sectionPaths,
	IncrementalIRReuse &reuse
) {
	const Json &saved = previous.at("global_types");
	if (saved.is_null())
		return false;
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	std::vector<std::pair<Variable *, DataType>> globals;
	for (const auto &[section, path] : sectionPaths)
		for (const auto &[name, variable] : section->variables) {
			if (!variable->isGlobal)
				continue;
			std::string identity = Json::array({path, name}).dump();
			if (!saved.contains(identity))
				return false;
			auto type = decodeInferenceTypeIdentity(saved.at(identity), classPaths);
			if (!type || (variable->type.isDeduced() && variable->type != *type))
				return false;
			globals.emplace_back(variable, std::move(*type));
		}
	if (globals.size() != saved.size() || !reuse.reuseRoot())
		return false;
	// Exposed functions and lifecycle inference run after entry replay. They
	// consume the entry's global types without executing its body again.
	for (auto &[variable, type] : globals)
		variable->type = std::move(type);
	context.reuseRootExecution = true;
	return true;
}

void recordIRReuseStatistics(
	BuildStatistics &statistics, const IncrementalIRReuse &reuse, const Json &previous,
	const std::map<const Section *, std::string> &sectionPaths
) {
	statistics.reusedFunctionIR = reuse.reusedCount();
	statistics.cachedIRDependencyScans = reuse.dependencyScans();
	statistics.cachedIRDependencyHits = reuse.dependencyHits();
	statistics.cachedIRGlobalInputChecks = reuse.globalInputChecks();
	statistics.cachedIRGlobalInputHits = reuse.globalInputHits();
	if (!reuse.rootReused())
		return;

	// Entry replay consumes saved instantiations without creating an
	// Instantiation in the current context or calling the summary loader.
	auto retained = retainedGraphVariants(&reuse, &previous);
	const Json &summaries = previous.at("inference_summaries");
	InferenceValueIdentities identities(sectionPaths);
	for (const auto &[section, path] : sectionPaths) {
		auto variants = retained.keysByPath.find(path);
		if (variants == retained.keysByPath.end() || !summaries.contains(path))
			continue;
		for (const auto &[key, _] : section->instantiations)
			if (auto identity = inferenceSummaryKey(key, identities))
				variants->second.erase(*identity);
		for (const std::string &key : variants->second)
			if (summaries.at(path).contains(key))
				statistics.reusedInferenceInstantiations++;
	}
}
