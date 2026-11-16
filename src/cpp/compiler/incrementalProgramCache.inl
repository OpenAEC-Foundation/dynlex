#include "incrementalInferenceSummaryCodec.inl"

constexpr std::array<const char *, 7> programPayloadFields{
	"all_function_projection", "section_dependency_graph", "inference_summaries", "generated_ir", "native_units",
	"optimization_units",	   "validation_units"
};

bool isModulePayload(std::string_view field) { return field == "generated_ir"; }

fs::path programPayloadPath(const fs::path &directory, std::string_view key) {
	return directory / ("payload-" + std::string(key) + ".entry");
}

fs::path inferenceSummaryPath(const fs::path &directory, const std::string &key) {
	return directory / ("inference-summary-" + key + ".entry");
}

struct GraphEncodingWork {
	size_t columnsReused = 0, columnsBuilt = 0, recordsBuilt = 0;
};

Json encodeSectionGraph(Json &graph, GraphEncodingWork *work) {
	const Json &forward = graph.at("forward");
	requireCompilerInvariant(
		graph.at("instantiations").size() == forward.size(),
		"section dependency graph has unequal forward and instantiation entries"
	);
	Json encoded = graph.contains("cached_encoding") ? std::move(graph.at("cached_encoding")) : Json::object();
	bool samePaths = encoded.contains("paths") && encoded.at("paths").size() == forward.size();
	size_t pathIndex = 0;
	for (const auto &[path, _] : forward.items())
		if (samePaths)
			samePaths = encoded.at("paths").at(pathIndex++).get_ref<const std::string &>() == path;
	if (!samePaths) {
		encoded["paths"] = Json::array();
		for (const auto &[path, _] : forward.items())
			encoded["paths"].push_back(path);
	}
	auto recordColumn = [&](bool reused, size_t records) {
		if (work) {
			(reused ? work->columnsReused : work->columnsBuilt)++;
			if (!reused)
				work->recordsBuilt += records;
		}
	};
	recordColumn(samePaths, forward.size());
	auto reuseColumn = [&](const char *name) {
		return samePaths && graph.at("encoding_reuse").at(name).get<bool>();
	};
	bool reuseForward = reuseColumn("forward"), reuseReverse = reuseColumn("reverse"),
		 reuseInstantiations = reuseColumn("instantiations");
	const Json &paths = encoded.at("paths");
	std::unordered_map<std::string_view, size_t> ids;
	ids.reserve(forward.size());
	for (size_t index = 0; index < paths.size(); index++)
		ids.emplace(paths.at(index).get_ref<const std::string &>(), index);
	auto encodeEdges = [&](const Json &edges) {
		Json encoded = Json::array();
		for (const Json &edge : edges)
			encoded.push_back(ids.at(edge.get_ref<const std::string &>()));
		return encoded;
	};
	Json encodedForward = Json::array();
	Json encodedMatched = Json::array();
	Json encodedInstantiations = Json::array();
	Json encodedUnits = Json::array();
	Json unitKeys = Json::array(), unitSources = Json::array();
	std::map<std::string, size_t> unitKeyIds;
	std::map<std::pair<size_t, std::string>, size_t> unitSourceIds;
	auto keyId = [&](const std::string &key) {
		auto [found, inserted] = unitKeyIds.try_emplace(key, unitKeys.size());
		if (inserted)
			unitKeys.push_back(key);
		return found->second;
	};
	for (const Json &path : paths) {
		const std::string &name = path.get_ref<const std::string &>();
		if (!reuseForward)
			encodedForward.push_back(encodeEdges(forward.at(name)));
		const Json &matched = graph.at("matched").at(name);
		encodedMatched.push_back(Json::array({matched.at("input"), encodeEdges(matched.at("dependencies"))}));
		if (!reuseInstantiations) {
			Json variants = Json::object();
			for (const auto &[key, edges] : graph.at("instantiations").at(name).items())
				variants[key] = encodeEdges(edges);
			encodedInstantiations.push_back(std::move(variants));
		}
		Json units = Json::array();
		for (const auto &[key, unit] : graph.at("inference_units").at(name).items()) {
			Json sources = Json::array(), callees = Json::array(), values = Json::array();
			for (const auto &[source, fingerprint] : unit.at("sources").items()) {
				auto [found, inserted] =
					unitSourceIds.try_emplace(std::pair{ids.at(source), fingerprint.get<std::string>()}, unitSources.size());
				if (inserted)
					unitSources.push_back(Json::array({ids.at(source), fingerprint}));
				sources.push_back(found->second);
			}
			for (const Json &callee : unit.at("callees"))
				callees.push_back(
					Json::array({ids.at(callee.at(0).get<std::string>()), keyId(callee.at(1).get<std::string>())})
				);
			for (const Json &query : unit.at("values"))
				values.push_back(
					Json::array(
						{ids.at(query.at(0).get<std::string>()), keyId(query.at(1).get<std::string>()), query.at(2),
						 query.at(3)}
					)
				);
			units.push_back(
				Json::array(
					{keyId(key), std::move(sources), std::move(callees), unit.at("lookups"), std::move(values),
					 unit.at("global_inputs"), unit.at("direct_global_inputs")}
				)
			);
		}
		encodedUnits.push_back(std::move(units));
	}
	Json encodedReverse = Json::array();
	if (!reuseReverse)
		for (const auto &[path, callers] : graph.at("reverse").items())
			encodedReverse.push_back(Json::array({ids.at(path), encodeEdges(callers)}));
	Json encodedLookups = Json::object();
	for (const auto &[key, lookup] : graph.at("callable_lookups").items()) {
		encodedLookups[key] = lookup;
		encodedLookups[key]["owners"] = encodeEdges(lookup.at("owners"));
	}
	Json encodedReads = Json::array();
	for (const auto &[owner, reads] : graph.at("inference_reads").items())
		encodedReads.push_back(Json::array({ids.at(owner), encodeEdges(reads)}));
	if (!reuseForward)
		encoded["forward"] = std::move(encodedForward);
	if (!reuseReverse)
		encoded["reverse"] = std::move(encodedReverse);
	if (!reuseInstantiations)
		encoded["instantiations"] = std::move(encodedInstantiations);
	recordColumn(reuseForward, forward.size());
	recordColumn(reuseReverse, graph.at("reverse").size());
	recordColumn(reuseInstantiations, forward.size());
	encoded["matched"] = std::move(encodedMatched);
	encoded["inference_units"] = std::move(encodedUnits);
	encoded["inference_unit_keys"] = std::move(unitKeys);
	encoded["inference_unit_sources"] = std::move(unitSources);
	encoded["callable_lookups"] = std::move(encodedLookups);
	encoded["inference_reads"] = std::move(encodedReads);
	for (const char *name :
		 {"matched", "inference_units", "inference_unit_keys", "inference_unit_sources", "callable_lookups", "inference_reads"})
		recordColumn(false, encoded.at(name).size());
	return encoded;
}

Json decodeSectionGraph(Json encoded) {
	const Json &paths = encoded.at("paths");
	const Json &forward = encoded.at("forward");
	const Json &instantiations = encoded.at("instantiations");
	const Json &unitKeys = encoded.at("inference_unit_keys"), &unitSources = encoded.at("inference_unit_sources");
	if (!paths.is_array() || !forward.is_array() || !instantiations.is_array() || paths.size() != forward.size() ||
		paths.size() != instantiations.size())
		throw std::runtime_error("invalid section dependency cache shape");
	auto decodeEdges = [&](const Json &edges) {
		Json result = Json::array();
		for (const Json &id : edges)
			result.push_back(paths.at(id.get<size_t>()));
		return result;
	};
	Json graph{{"forward", Json::object()},		   {"reverse", Json::object()},			 {"instantiations", Json::object()},
			   {"matched", Json::object()},		   {"callable_lookups", Json::object()}, {"inference_reads", Json::object()},
			   {"inference_units", Json::object()}};
	for (size_t index = 0; index < paths.size(); index++) {
		const std::string &path = paths.at(index).get_ref<const std::string &>();
		graph["forward"][path] = decodeEdges(forward.at(index));
		const Json &matched = encoded.at("matched").at(index);
		graph["matched"][path] = Json{{"input", matched.at(0)}, {"dependencies", decodeEdges(matched.at(1))}};
		Json variants = Json::object();
		for (const auto &[key, edges] : instantiations.at(index).items())
			variants[key] = decodeEdges(edges);
		graph["instantiations"][path] = std::move(variants);
		Json units = Json::object();
		for (const Json &unit : encoded.at("inference_units").at(index)) {
			Json sources = Json::object(), callees = Json::array(), values = Json::array();
			for (const Json &id : unit.at(1)) {
				const Json &source = unitSources.at(id.get<size_t>());
				sources[paths.at(source.at(0).get<size_t>()).get<std::string>()] = source.at(1);
			}
			for (const Json &callee : unit.at(2))
				callees.push_back(Json::array({paths.at(callee.at(0).get<size_t>()), unitKeys.at(callee.at(1).get<size_t>())}));
			for (const Json &query : unit.at(4))
				values.push_back(
					Json::array(
						{paths.at(query.at(0).get<size_t>()), unitKeys.at(query.at(1).get<size_t>()), query.at(2), query.at(3)}
					)
				);
			units[unitKeys.at(unit.at(0).get<size_t>()).get<std::string>()] =
				Json{{"sources", std::move(sources)}, {"callees", std::move(callees)}, {"lookups", unit.at(3)},
					 {"values", std::move(values)},	  {"global_inputs", unit.at(5)},   {"direct_global_inputs", unit.at(6)}};
		}
		graph["inference_units"][path] = std::move(units);
	}
	for (const Json &entry : encoded.at("reverse"))
		graph["reverse"][paths.at(entry.at(0).get<size_t>()).get<std::string>()] = decodeEdges(entry.at(1));
	for (const auto &[key, lookup] : encoded.at("callable_lookups").items()) {
		graph["callable_lookups"][key] = lookup;
		graph["callable_lookups"][key]["owners"] = decodeEdges(lookup.at("owners"));
	}
	for (const Json &entry : encoded.at("inference_reads"))
		graph["inference_reads"][paths.at(entry.at(0).get<size_t>()).get<std::string>()] = decodeEdges(entry.at(1));
	graph["cached_encoding"] = std::move(encoded);
	return graph;
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
