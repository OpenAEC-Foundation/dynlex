constexpr std::array<const char *, 4> programPayloadFields{
	"all_function_projection", "section_dependency_graph", "inference_summaries", "ir"
};

fs::path programPayloadPath(const fs::path &directory, std::string_view key) {
	return directory / ("payload-" + std::string(key) + ".entry");
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
		if (!fs::exists(programPayloadPath(path.parent_path(), keys.at(field).get_ref<const std::string &>())))
			return std::nullopt;
	}
	return manifest;
}

void hydrateProgramField(Json &program, const fs::path &directory, const char *field) {
	if (program.contains(field))
		return;
	std::string key = program.at("payload_keys").at(field).get<std::string>();
	fs::path path = programPayloadPath(directory, key);
	auto payload = readEntry(path);
	if (!payload)
		throw std::runtime_error("program payload disappeared: " + path.string());
	if (digest(*payload) != key)
		throw std::runtime_error("corrupt program payload: " + path.string());
	program[field] = std::string_view(field) == "ir" ? Json(std::move(*payload)) : Json::parse(*payload);
}

void hydrateProgramEntry(Json &program, const fs::path &directory) {
	for (const char *field : programPayloadFields)
		hydrateProgramField(program, directory, field);
}

void writeProgramEntry(const fs::path &path, Json program, const Json *prior = nullptr) {
	Json keys = program.contains("payload_keys") ? program.at("payload_keys") : Json::object();
	for (const char *field : programPayloadFields) {
		if (program.contains(field)) {
			if (prior && prior->contains(field) && prior->at(field) == program.at(field) &&
				prior->at("payload_keys").contains(field)) {
				keys[field] = prior->at("payload_keys").at(field);
			} else {
				std::string payload = std::string_view(field) == "ir" ? program.at(field).get<std::string>()
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
