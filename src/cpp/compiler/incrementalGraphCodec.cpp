#include "incrementalGraphCodec.h"
#include "compilerUtils.h"
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

using Json = nlohmann::json;

Json graphEncodingReuse(bool edges, bool variants) {
	return Json{{"forward", edges}, {"reverse", edges}, {"instantiations", variants}};
}

namespace {
struct UnitTables {
	const std::unordered_map<std::string_view, size_t> &paths;
	Json keys = Json::array(), sources = Json::array();
	std::unordered_map<std::string_view, size_t> keyIds;
	std::map<std::pair<size_t, std::string_view>, size_t> sourceIds;

	explicit UnitTables(const std::unordered_map<std::string_view, size_t> &paths) : paths(paths) {}

	size_t keyId(std::string_view key) const { return keyIds.at(key); }
	size_t sourceId(const std::string &path, const Json &fingerprint) const {
		return sourceIds.at({paths.at(path), fingerprint.get_ref<const std::string &>()});
	}
	void index(const std::string &key, const Json &unit) {
		auto addKey = [&](const Json &key) {
			const auto &text = key.get_ref<const std::string &>();
			if (keyIds.try_emplace(text, keys.size()).second)
				keys.push_back(text);
		};
		for (const auto &[path, fingerprint] : unit.at("sources").items())
			if (sourceIds
					.try_emplace(
						std::pair{paths.at(path), std::string_view(fingerprint.get_ref<const std::string &>())}, sources.size()
					)
					.second)
				sources.push_back(Json::array({paths.at(path), fingerprint}));
		for (const auto &callee : unit.at("callees"))
			addKey(callee.at(1));
		for (const auto &value : unit.at("values"))
			addKey(value.at(1));
		// The owner key comes last, preserving the canonical dictionary order.
		if (keyIds.try_emplace(key, keys.size()).second)
			keys.push_back(key);
	}
	bool unchanged(const std::string &key, const Json &unit, const Json &encoded) const {
		if (encoded.at(0) != keyId(key) || encoded.at(1).size() != unit.at("sources").size() ||
			encoded.at(2).size() != unit.at("callees").size() || encoded.at(4).size() != unit.at("values").size() ||
			encoded.at(3) != unit.at("lookups") || encoded.at(5) != unit.at("global_inputs") ||
			encoded.at(6) != unit.at("direct_global_inputs"))
			return false;
		size_t index = 0;
		for (const auto &[path, fingerprint] : unit.at("sources").items())
			if (encoded.at(1).at(index++) != sourceId(path, fingerprint))
				return false;
		auto equalCalls = [&](const Json &calls, const Json &previous, bool values) {
			for (size_t i = 0; i < calls.size(); i++) {
				const auto &call = calls.at(i), &old = previous.at(i);
				if (old.at(0) != paths.at(call.at(0).get_ref<const std::string &>()) ||
					old.at(1) != keyId(call.at(1).get_ref<const std::string &>()) ||
					(values && (old.at(2) != call.at(2) || old.at(3) != call.at(3))))
					return false;
			}
			return true;
		};
		return equalCalls(unit.at("callees"), encoded.at(2), false) && equalCalls(unit.at("values"), encoded.at(4), true);
	}
};
} // namespace

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
	Json encodedInstantiations = Json::array();
	Json encodedUnits = samePaths ? std::move(encoded.at("inference_units")) : Json::array();
	Json encodedMatchedRows = samePaths ? std::move(encoded.at("matched")) : Json::array();
	UnitTables tables{ids};
	for (const Json &path : paths)
		for (const auto &[key, unit] : graph.at("inference_units").at(path.get_ref<const std::string &>()).items())
			tables.index(key, unit);
	size_t matchedReused = 0, unitsReused = 0, rowIndex = 0;
	for (const Json &path : paths) {
		const std::string &name = path.get_ref<const std::string &>();
		if (!reuseForward)
			encodedForward.push_back(encodeEdges(forward.at(name)));
		const Json &matched = graph.at("matched").at(name);
		bool sameMatched = samePaths && encodedMatchedRows.at(rowIndex).at(0) == matched.at("input") &&
						   encodedMatchedRows.at(rowIndex).at(1).size() == matched.at("dependencies").size();
		if (sameMatched) {
			size_t dependency = 0;
			for (const Json &path : matched.at("dependencies"))
				if (encodedMatchedRows.at(rowIndex).at(1).at(dependency++) != ids.at(path.get_ref<const std::string &>())) {
					sameMatched = false;
					break;
				}
		}
		if (sameMatched)
			++matchedReused;
		else {
			auto row = Json::array({matched.at("input"), encodeEdges(matched.at("dependencies"))});
			if (samePaths)
				encodedMatchedRows.at(rowIndex) = std::move(row);
			else
				encodedMatchedRows.push_back(std::move(row));
		}
		if (!reuseInstantiations) {
			Json variants = Json::object();
			for (const auto &[key, edges] : graph.at("instantiations").at(name).items())
				variants[key] = encodeEdges(edges);
			encodedInstantiations.push_back(std::move(variants));
		}
		const auto &currentUnits = graph.at("inference_units").at(name);
		bool sameUnits = samePaths && encodedUnits.at(rowIndex).size() == currentUnits.size();
		size_t unitIndex = 0;
		if (sameUnits)
			for (const auto &[key, unit] : currentUnits.items())
				if (!tables.unchanged(key, unit, encodedUnits.at(rowIndex).at(unitIndex++))) {
					sameUnits = false;
					break;
				}
		if (sameUnits) {
			++unitsReused;
			++rowIndex;
			continue;
		}
		Json units = Json::array();
		for (const auto &[key, unit] : currentUnits.items()) {
			Json sources = Json::array(), callees = Json::array(), values = Json::array();
			for (const auto &[source, fingerprint] : unit.at("sources").items()) {
				sources.push_back(tables.sourceId(source, fingerprint));
			}
			for (const Json &callee : unit.at("callees"))
				callees.push_back(
					Json::array(
						{ids.at(callee.at(0).get_ref<const std::string &>()),
						 tables.keyId(callee.at(1).get_ref<const std::string &>())}
					)
				);
			for (const Json &query : unit.at("values"))
				values.push_back(
					Json::array(
						{ids.at(query.at(0).get_ref<const std::string &>()),
						 tables.keyId(query.at(1).get_ref<const std::string &>()), query.at(2), query.at(3)}
					)
				);
			units.push_back(
				Json::array(
					{tables.keyId(key), std::move(sources), std::move(callees), unit.at("lookups"), std::move(values),
					 unit.at("global_inputs"), unit.at("direct_global_inputs")}
				)
			);
		}
		if (samePaths)
			encodedUnits.at(rowIndex) = std::move(units);
		else
			encodedUnits.push_back(std::move(units));
		++rowIndex;
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
	encoded["matched"] = std::move(encodedMatchedRows);
	encoded["inference_units"] = std::move(encodedUnits);
	encoded["inference_unit_keys"] = std::move(tables.keys);
	encoded["inference_unit_sources"] = std::move(tables.sources);
	encoded["callable_lookups"] = std::move(encodedLookups);
	encoded["inference_reads"] = std::move(encodedReads);
	encoded["global_type_producers"] = graph.at("global_type_producers");
	recordColumn(matchedReused == paths.size(), paths.size() - matchedReused);
	recordColumn(unitsReused == paths.size(), paths.size() - unitsReused);
	if (work)
		work->recordsReused += matchedReused + unitsReused;
	for (const char *name :
		 {"inference_unit_keys", "inference_unit_sources", "callable_lookups", "inference_reads", "global_type_producers"})
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
	graph["global_type_producers"] = encoded.at("global_type_producers");
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
	graph["encoding_reuse"] = graphEncodingReuse(true, true);
	return graph;
}
