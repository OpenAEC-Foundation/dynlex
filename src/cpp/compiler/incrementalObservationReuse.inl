bool observationsStillEquivalent(
	const Json &previous, RecordingFileSystem &files, const ParseContext &context, std::string &cachedDiagnostics,
	bool &configOnlyChange
) {
	std::vector<std::string> changedSources;
	bool configChanged = previous.at("configuration_files") != files.configurationObservations;
	for (const auto &[path, observation] : previous.at("files").items()) {
		const Json &current = files.observe(path);
		Json rawObservation = observation;
		if (rawObservation.is_object())
			rawObservation.erase("semantic");
		if (current == rawObservation)
			continue;
		if (current.is_null() || observation.is_null() || fs::path(path).extension() != ".dl" ||
			!previous.contains("comment_prefixes") || !observation.contains("semantic"))
			return false;
		if (current.at("canonical") != observation.at("canonical") || current.at("internal") != observation.at("internal"))
			return false;
		changedSources.push_back(path);
	}
	if (!configChanged && changedSources.empty()) {
		cachedDiagnostics = previous.at("diagnostics").get<std::string>();
		return true;
	}
	if (previous.at("syntax_structure") !=
		Json::array({context.builtinSyntax.nonMessageIdentity, context.projectSyntax.nonMessageIdentity}))
		return false;
	if (!changedSources.empty() && (context.options.emitDebugInfo || context.options.traceExecution))
		return false;
	const Json &prefixes = previous.at("comment_prefixes");
	for (const std::string &path : changedSources) {
		lsp::SourceFile *file = files.cachedFile(path);
		if (!file)
			throw std::runtime_error("recorded source disappeared: " + path);
		const std::string &prefix = file->isInternal ? prefixes.at("builtin").get_ref<const std::string &>()
													 : prefixes.at("project").get_ref<const std::string &>();
		if (semanticSourceDigest(file->content, prefix) != previous.at("files").at(path).at("semantic").get<std::string>())
			return false;
	}
	std::optional<std::string> rendered =
		renderCachedDiagnostics(previous, files, context, std::set<std::string>(changedSources.begin(), changedSources.end()));
	if (!rendered)
		return false;
	cachedDiagnostics = std::move(*rendered);
	configOnlyChange = configChanged && changedSources.empty();
	return true;
}
