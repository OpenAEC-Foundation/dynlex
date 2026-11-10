constexpr std::array<const char *, 6> programPayloadFields{
	"all_function_projection", "section_dependency_graph", "inference_summaries", "generated_ir", "native_units",
	"optimization_units"
};

bool isModulePayload(std::string_view field) { return field == "generated_ir"; }

fs::path programPayloadPath(const fs::path &directory, std::string_view key) {
	return directory / ("payload-" + std::string(key) + ".entry");
}

Json encodeSectionGraph(const Json &graph) {
	const Json &forward = graph.at("forward");
	requireCompilerInvariant(
		graph.at("instantiations").size() == forward.size(),
		"section dependency graph has unequal forward and instantiation entries"
	);
	Json paths = Json::array();
	std::unordered_map<std::string, size_t> ids;
	ids.reserve(forward.size());
	for (const auto &[path, _] : forward.items()) {
		ids.emplace(path, paths.size());
		paths.push_back(path);
	}
	auto encodeEdges = [&](const Json &edges) {
		Json encoded = Json::array();
		for (const Json &edge : edges)
			encoded.push_back(ids.at(edge.get_ref<const std::string &>()));
		return encoded;
	};
	Json encodedForward = Json::array();
	Json encodedInstantiations = Json::array();
	for (const Json &path : paths) {
		const std::string &name = path.get_ref<const std::string &>();
		encodedForward.push_back(encodeEdges(forward.at(name)));
		Json variants = Json::object();
		for (const auto &[key, edges] : graph.at("instantiations").at(name).items())
			variants[key] = encodeEdges(edges);
		encodedInstantiations.push_back(std::move(variants));
	}
	Json encodedReverse = Json::array();
	for (const auto &[path, callers] : graph.at("reverse").items())
		encodedReverse.push_back(Json::array({ids.at(path), encodeEdges(callers)}));
	Json encodedLookups = Json::object();
	for (const auto &[signature, lookup] : graph.at("callable_lookups").items())
		encodedLookups[signature] = Json{
			{"owners", encodeEdges(lookup.at("owners"))}, {"candidates", lookup.at("candidates")}
		};
	return Json{
		{"paths", std::move(paths)},
		{"forward", std::move(encodedForward)},
		{"reverse", std::move(encodedReverse)},
		{"instantiations", std::move(encodedInstantiations)},
		{"callable_lookups", std::move(encodedLookups)}
	};
}

Json decodeSectionGraph(const Json &encoded) {
	const Json &paths = encoded.at("paths");
	const Json &forward = encoded.at("forward");
	const Json &instantiations = encoded.at("instantiations");
	if (!paths.is_array() || !forward.is_array() || !instantiations.is_array() || paths.size() != forward.size() ||
		paths.size() != instantiations.size())
		throw std::runtime_error("invalid section dependency cache shape");
	auto decodeEdges = [&](const Json &edges) {
		Json result = Json::array();
		for (const Json &id : edges)
			result.push_back(paths.at(id.get<size_t>()));
		return result;
	};
	Json graph{
		{"forward", Json::object()}, {"reverse", Json::object()}, {"instantiations", Json::object()},
		{"callable_lookups", Json::object()}
	};
	for (size_t index = 0; index < paths.size(); index++) {
		const std::string &path = paths.at(index).get_ref<const std::string &>();
		graph["forward"][path] = decodeEdges(forward.at(index));
		Json variants = Json::object();
		for (const auto &[key, edges] : instantiations.at(index).items())
			variants[key] = decodeEdges(edges);
		graph["instantiations"][path] = std::move(variants);
	}
	for (const Json &entry : encoded.at("reverse"))
		graph["reverse"][paths.at(entry.at(0).get<size_t>()).get<std::string>()] = decodeEdges(entry.at(1));
	for (const auto &[signature, lookup] : encoded.at("callable_lookups").items())
		graph["callable_lookups"][signature] = Json{
			{"owners", decodeEdges(lookup.at("owners"))}, {"candidates", lookup.at("candidates")}
		};
	return graph;
}

std::optional<Json> readProgramManifest(const fs::path &path) {
	auto payload = readEntry(path);
	if (!payload)
		return std::nullopt;
	Json manifest = Json::parse(*payload);
	if (!manifest.contains("payload_keys") || !manifest.at("payload_keys").is_object() ||
		manifest.at("payload_keys").size() != programPayloadFields.size())
		throw std::runtime_error("invalid program cache manifest: " + path.string());
	const Json &keys = manifest.at("payload_keys");
	for (const char *field : programPayloadFields) {
		if (manifest.contains(field) || !keys.contains(field) || !keys.at(field).is_string())
			throw std::runtime_error("invalid program cache manifest: " + path.string());
		if (!recordCacheAccess(programPayloadPath(path.parent_path(), keys.at(field).get_ref<const std::string &>())))
			return std::nullopt;
	}
	return manifest;
}

bool hydrateProgramField(Json &program, const fs::path &directory, const char *field) {
	if (program.contains(field))
		return false;
	std::string key = program.at("payload_keys").at(field).get<std::string>();
	fs::path path = programPayloadPath(directory, key);
	auto payload = readEntry(path);
	if (!payload)
		throw std::runtime_error("program payload disappeared: " + path.string());
	if (digest(*payload) != key)
		throw std::runtime_error("corrupt program payload: " + path.string());
	if (isModulePayload(field))
		program[field] = std::move(*payload);
	else if (std::string_view(field) == "section_dependency_graph")
		program[field] = decodeSectionGraph(Json::parse(*payload));
	else
		program[field] = Json::parse(*payload);
	return true;
}

void writeProgramEntry(const fs::path &path, Json program, const Json *prior = nullptr) {
	Json keys = program.contains("payload_keys") ? program.at("payload_keys") : Json::object();
	for (const char *field : programPayloadFields) {
		if (program.contains(field)) {
			if (prior && prior->contains(field) && prior->at(field) == program.at(field) &&
				prior->at("payload_keys").contains(field)) {
				keys[field] = prior->at("payload_keys").at(field);
			} else {
				std::string payload = isModulePayload(field) ? program.at(field).get<std::string>()
									  : std::string_view(field) == "section_dependency_graph"
										  ? encodeSectionGraph(program.at(field)).dump()
										  : program.at(field).dump();
				std::string key = digest(payload);
				writeEntry(programPayloadPath(path.parent_path(), key), payload);
				keys[field] = std::move(key);
			}
			program.erase(field);
		}
		requireCompilerInvariant(keys.contains(field), "program cache is missing a payload");
	}
	program["payload_keys"] = std::move(keys);
	writeEntry(path, program.dump());
}
