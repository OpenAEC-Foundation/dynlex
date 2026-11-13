constexpr std::array inferenceStateFields{"all_function_projection", "section_dependency_graph", "inference_summaries"};

Json inferenceStateManifest(const fs::path &directory, const Json &source) {
	Json state = Json::object();
	for (const char *field :
		 {"files", "configuration_files", "syntax_structure", "comment_prefixes", "global_types", "inference_summary_keys",
		  "inference_body_keys", "inference_class_keys"})
		if (source.contains(field))
			state[field] = source.at(field);
	for (const char *field : inferenceStateFields) {
		if (source.contains(field))
			state[field] = source.at(field);
		if (source.contains("payload_keys") && source.at("payload_keys").contains(field))
			state["payload_keys"][field] = source.at("payload_keys").at(field);
	}
	recordInferencePayloadKeys(state);
	storeSnapshotPayloads(directory, state, inferenceStateFields);
	return state;
}

Json *selectInferenceState(Json *native, Json *failure, const fs::path &directory, BuildStatistics &statistics) {
	if (!failure || failure->at("inference_state").is_null())
		return native;
	Json &state = failure->at("inference_state");
	for (const auto &key : state.at("payload_keys"))
		if (!recordCacheAccess(programPayloadPath(directory, key.get<std::string>())))
			return native; // The independently evictable semantic index is unavailable.
	auto start = Clock::now();
	for (const char *field : inferenceStateFields)
		statistics.cachePayloadsLoaded += hydrateProgramField(state, directory, field);
	statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(start);
	return &state;
}

Json captureFailedInferenceState(
	ParseContext &context, RecordingFileSystem &files, const Json &projection,
	const std::map<const Section *, std::string> &paths, IncrementalInferenceReads &reads, IncrementalInferenceReuse *reuse,
	InferenceSummaryStore &store, BuildStatistics &statistics, std::map<std::string, std::string> &fingerprints, Json *prior,
	const std::unordered_set<const Section *> &stable, const Json *native, const IncrementalInferenceProducts &products
) {
	Json functions = native ? native->at("function_sections") : Json::object();
	Json keys = native ? native->at("function_instantiations") : Json::object();
	Json globals = captureGlobalTypeProjection(paths).value_or(Json(nullptr));
	Json state{{"files", files.observations}, {"all_function_projection", projection}, {"global_types", globals}};
	state["inference_summaries"] = reuse ? reuse->snapshot(paths, functions, keys, nullptr, true)
										 : captureInferenceSummaries(
											   context, products.sourceIndex(), paths, products.globalInputs(), store, {},
											   nullptr, nullptr, true, &products.references()
										   );
	// No native definitions were produced by this failed compilation. Semantic
	// unit products, including errors, retain their own reachable dependency graph.
	state["function_sections"] = Json::object();
	state["function_instantiations"] = Json::object();
	state["section_dependency_graph"] = captureSectionDependencyGraph(
		context, state, paths, reads, projection.at("constraint_probe_schedule").at("callable_lookups"), statistics,
		fingerprints, products.globalInputs(), prior ? &prior->at("section_dependency_graph") : nullptr, stable, {}, &products
	);
	recordSemanticSources(state, files, context);
	return state;
}
