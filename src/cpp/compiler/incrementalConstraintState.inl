// Constraint query products are independent of the completed ordinary-inference
// state. A failed early stage must not publish incomplete function signatures
// to the ordinary inference controller.
struct ConstraintReuseState {
	const Json *source = nullptr;
	const Json *projection = nullptr;
	const Json *graph = nullptr;
};

const Json *priorMatchedDependencies(const ConstraintReuseState &constraints, const Json *inferred) {
	if (!constraints.graph || (inferred && constraints.graph == &inferred->at("section_dependency_graph")))
		return nullptr;
	return &constraints.graph->at("matched");
}

constexpr std::array constraintStateFields{"all_function_projection", "graph"};

const Json *selectFrontendReuseState(
	Json *native, Json *failure, const fs::path &directory, ProgramPayloads &retainedPayloads, BuildStatistics &statistics
) {
	if (failure && !failure->at("constraint_state").is_null()) {
		Json &state = failure->at("constraint_state");
		const auto &key = state.at("payload_keys").at("all_function_projection").get_ref<const std::string &>();
		if (retainProgramPayload(directory, key, retainedPayloads)) {
			auto start = Clock::now();
			statistics.cachePayloadsLoaded += hydrateProgramField(state, retainedPayloads, "all_function_projection");
			statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(start);
			return &state;
		}
	}
	return native;
}

Json constraintStateManifest(const fs::path &directory, Json state) {
	storeSnapshotPayloads(directory, state, constraintStateFields);
	return state;
}

ConstraintReuseState selectConstraintReuseState(
	Json *inferred, Json *failure, const fs::path &directory, ProgramPayloads &retainedPayloads, BuildStatistics &statistics
) {
	if (failure && !failure->at("constraint_state").is_null()) {
		Json &state = failure->at("constraint_state");
		bool available = retainProgramPayloads(state, directory, retainedPayloads);
		if (available) {
			auto start = Clock::now();
			for (const char *field : constraintStateFields)
				statistics.cachePayloadsLoaded += hydrateProgramField(state, retainedPayloads, field);
			statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(start);
			return {&state, &state.at("all_function_projection"), &state.at("graph")};
		}
	}
	return inferred
			   ? ConstraintReuseState{inferred, &inferred->at("all_function_projection"), &inferred->at("section_dependency_graph")}
			   : ConstraintReuseState{};
}

Json captureConstraintGraph(
	const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths,
	const IncrementalInferenceReads &reads, const Json &schedule, const Json *prior, BuildStatistics &statistics
) {
	MatchedDependencyFacts matchedFacts(context, projection, paths, prior ? prior->at("matched") : Json::object(), statistics);
	std::map<std::string, std::set<std::string>> reverse, lookupOwners;
	for (const auto &[section, path] : paths) {
		for (const Json &dependency : matchedFacts.dependencies(section))
			reverse[dependency.get<std::string>()].insert(path);
		for (const std::string &dependency : reads.dependenciesOf(path))
			reverse[dependency].insert(path);
		for (const std::string &key : reads.readsOf(path))
			lookupOwners[key].insert(path);
	}
	for (const Json &keys : schedule.at("callable_lookups"))
		for (const Json &key : keys)
			lookupOwners.try_emplace(key.get<std::string>());
	Json lookups = Json::object();
	for (const auto &[key, owners] : lookupOwners) {
		Json observation = reads.observation(key, prior ? &prior->at("callable_lookups") : nullptr);
		for (const std::string &owner : owners)
			if (observation.at("candidates").size() == 1)
				reverse[observation.at("candidates").at(0).at(0).get<std::string>()].insert(owner);
		observation["owners"] = owners;
		lookups[key] = std::move(observation);
	}
	return Json{
		{"reverse", std::move(reverse)}, {"callable_lookups", std::move(lookups)}, {"matched", matchedFacts.snapshot()}
	};
}

Json captureCurrentConstraintState(
	ParseContext &context, RecordingFileSystem &files, Json &projection, const std::map<const Section *, std::string> &paths,
	const IncrementalInferenceReads &reads, const Json &frontendDiagnosticUnits, const ConstraintReuseState &prior,
	bool compiled, bool reusedInference, const Json *native, BuildStatistics &statistics
) {
	if (reusedInference) {
		for (const char *field : {"constraint_probe_schedule", "class_instantiations"})
			projection[field] = native->at("all_function_projection").at(field);
		return nullptr;
	}
	if (!context.collectedConstraintQueries)
		return nullptr;
	projection["constraint_probe_schedule"] = captureConstraintProbeSchedule(
		context, paths, reads, prior.projection ? &prior.projection->at("constraint_probe_schedule") : nullptr,
		prior.graph ? &prior.graph->at("callable_lookups") : nullptr
	);
	projection["class_instantiations"] = captureClassInstantiations(paths);
	if (compiled)
		return nullptr;
	Json state{
		{"files", files.observations},
		{"all_function_projection", projection},
		{"frontend_diagnostic_units", frontendDiagnosticUnits},
		{"graph", captureConstraintGraph(
					  context, projection, paths, reads, projection.at("constraint_probe_schedule"), prior.graph, statistics
				  )}
	};
	recordSemanticSources(state, files, context);
	return state;
}
