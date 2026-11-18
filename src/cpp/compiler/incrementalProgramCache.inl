#include "incrementalInferenceSummaryCodec.inl"

constexpr std::array<const char *, 9> programPayloadFields{
	"all_function_projection", "section_dependency_graph", "inference_summaries", "generated_ir", "native_units",
	"optimization_units",	   "validation_units",		   "debug_ir_inputs",	  "root_grouping"
};

bool isModulePayload(std::string_view field) { return field == "generated_ir"; }

fs::path programPayloadPath(const fs::path &directory, std::string_view key) {
	return directory / ("payload-" + std::string(key) + ".entry");
}

fs::path inferenceSummaryPath(const fs::path &directory, const std::string &key) {
	return directory / ("inference-summary-" + key + ".entry");
}

std::optional<Json> readProgramManifest(const fs::path &path) {
	auto payload = readEntry(path);
	if (!payload)
		return std::nullopt;
	Json manifest = Json::parse(*payload);
	if (!manifest.contains("payload_keys") || !manifest.at("payload_keys").is_object() ||
		manifest.at("payload_keys").size() != programPayloadFields.size() || !manifest.contains("inference_summary_keys") ||
		!manifest.at("inference_summary_keys").is_array() || !manifest.contains("inference_body_keys") ||
		!manifest.at("inference_body_keys").is_array() || !manifest.contains("inference_class_keys") ||
		!manifest.at("inference_class_keys").is_array())
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
		program[field] = decodeSectionGraph(parseCachedCbor(*payload));
	else
		program[field] = parseCachedCbor(*payload);
	return true;
}

void recordInferencePayloadKeys(Json &program) {
	if (program.contains("inference_summaries")) {
		std::set<std::string> summaries;
		std::set<std::string> bodies;
		std::set<std::string> classes;
		for (const Json &variants : program.at("inference_summaries"))
			for (const Json &reference : variants) {
				summaries.insert(reference.at("payload").get<std::string>());
				for (const Json &definition : reference.at("classes"))
					classes.insert(definition.at("payload").get<std::string>());
				if (!reference.at("body").is_null()) {
					bodies.insert(reference.at("body").at("payload").get<std::string>());
					for (const Json &definition : reference.at("body").at("classes"))
						classes.insert(definition.at("payload").get<std::string>());
				}
			}
		for (const Json &output : program.at("section_dependency_graph").at("global_type_producers"))
			for (const Json &definition : output.at("classes"))
				classes.insert(definition.at("payload").get<std::string>());
		program["inference_summary_keys"] = summaries;
		program["inference_body_keys"] = bodies;
		program["inference_class_keys"] = classes;
	}
}

using PayloadWriteWork = std::map<std::string, std::array<double, 3>>;
template <typename Fields>
void storeSnapshotPayloads(
	const fs::path &directory, Json &program, const Fields &fields, const Json *prior = nullptr,
	PayloadWriteWork *work = nullptr, GraphEncodingWork *graphWork = nullptr
) {
	Json keys = program.contains("payload_keys") ? program.at("payload_keys") : Json::object();
	for (const char *field : fields) {
		if (program.contains(field)) {
			auto *timings = work ? &(*work)[field] : nullptr;
			IncrementalWorkTimer compareTimer(timings ? &timings->at(0) : nullptr);
			bool unchanged = prior && prior->contains(field) && prior->at(field) == program.at(field) &&
							 prior->at("payload_keys").contains(field);
			compareTimer.stop();
			if (unchanged) {
				keys[field] = prior->at("payload_keys").at(field);
			} else {
				IncrementalWorkTimer encodeTimer(timings ? &timings->at(1) : nullptr);
				std::vector<uint8_t> encoded;
				std::string_view payload;
				if (isModulePayload(field)) {
					payload = program.at(field).get_ref<const std::string &>();
				} else {
					encoded = std::string_view(field) == "section_dependency_graph"
								  ? Json::to_cbor(encodeSectionGraph(program.at(field), graphWork))
								  : Json::to_cbor(program.at(field));
					payload = {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
				}
				encodeTimer.stop();
				IncrementalWorkTimer writeTimer(timings ? &timings->at(2) : nullptr);
				std::string key = digest(payload);
				writeEntry(programPayloadPath(directory, key), payload);
				keys[field] = std::move(key);
			}
			program.erase(field);
		}
		requireCompilerInvariant(keys.contains(field), "program cache is missing a payload");
	}
	program["payload_keys"] = std::move(keys);
}

void writeProgramEntry(
	const fs::path &path, Json program, const Json *prior = nullptr, PayloadWriteWork *work = nullptr,
	GraphEncodingWork *graphWork = nullptr
) {
	recordInferencePayloadKeys(program);
	storeSnapshotPayloads(path.parent_path(), program, programPayloadFields, prior, work, graphWork);
	writeEntry(path, program.dump());
}
