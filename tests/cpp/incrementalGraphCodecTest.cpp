#include "compiler/incrementalGraphCodec.h"
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>

using Json = nlohmann::json;

static void require(bool condition, std::source_location location = std::source_location::current()) {
	if (!condition)
		throw std::runtime_error("incremental graph encoding mismatch at line " + std::to_string(location.line()));
}

static Json unit(const char *fingerprint) {
	return Json{
		{"sources", {{"alpha", fingerprint}}},
		{"callees", Json::array({Json::array({"beta", "key"})})},
		{"lookups", {{"query", "lookup version"}}},
		{"values", Json::array({Json::array({"beta", "key", "args", "value"})})},
		{"global_inputs", {{"global", "global version"}}},
		{"direct_global_inputs", Json::object()}
	};
}

static Json graph() {
	Json result{
		{"forward", {{"alpha", Json::array({"beta"})}, {"beta", Json::array()}}},
		{"reverse", {{"beta", Json::array({"alpha"})}}},
		{"instantiations", {{"alpha", {{"key", Json::array({"beta"})}}}, {"beta", Json::object()}}},
		{"matched",
		 {{"alpha", {{"input", "first"}, {"dependencies", Json::array({"beta"})}}},
		  {"beta", {{"input", "second"}, {"dependencies", Json::array()}}}}},
		{"inference_units", {{"alpha", {{"key", unit("first")}}}, {"beta", Json::object()}}},
		{"callable_lookups", {{"query", {{"owners", Json::array({"alpha"})}, {"result", "selected"}}}}},
		{"inference_reads", {{"alpha", Json::array({"beta"})}}},
		{"global_type_producers", Json::object()},
		{"encoding_reuse", graphEncodingReuse(false, false)}
	};
	return result;
}

static Json check(Json &value, size_t expectedReuse) {
	Json fresh = value;
	fresh.erase("cached_encoding");
	fresh["encoding_reuse"] = graphEncodingReuse(false, false);
	auto expected = encodeSectionGraph(fresh, nullptr);
	GraphEncodingWork work;
	auto encoded = encodeSectionGraph(value, &work);
	require(encoded == expected);
	require(work.recordsReused == expectedReuse);
	auto decoded = decodeSectionGraph(encoded);
	decoded.erase("cached_encoding");
	decoded["encoding_reuse"] = value.at("encoding_reuse");
	auto semantic = value;
	semantic.erase("cached_encoding");
	require(decoded == semantic);
	return decodeSectionGraph(std::move(encoded));
}

void testIncrementalGraphCodec() {
	auto value = graph();
	value = check(value, 0);
	value = check(value, 4);
	value["matched"]["beta"]["input"] = nullptr;
	value = check(value, 3);
	value["matched"]["beta"]["dependencies"].push_back("alpha");
	value = check(value, 3);
	// Shared-table versions can change without changing encoded row IDs.
	value["inference_units"]["alpha"]["key"]["sources"]["alpha"] = "changed";
	value = check(value, 4);
	value["inference_units"]["alpha"]["key"]["values"][0][3] = "different";
	value = check(value, 3);
	value["inference_units"]["alpha"]["key"]["callees"][0][0] = "alpha";
	value = check(value, 3);
	for (const char *field : {"lookups", "global_inputs", "direct_global_inputs"}) {
		value["inference_units"]["alpha"]["key"][field]["new observation"] = "changed version";
		value = check(value, 3);
	}
	// A new earlier key shifts the numeric IDs of existing callee/value keys.
	value["inference_units"]["alpha"]["aardvark"] = unit("earlier");
	value = check(value, 3);
	value["inference_units"]["alpha"].erase("aardvark");
	value = check(value, 3);
	// Path removal invalidates all path-indexed rows.
	value["forward"].erase("alpha");
	value["reverse"] = Json::object();
	value["instantiations"].erase("alpha");
	value["matched"].erase("alpha");
	value["matched"]["beta"]["dependencies"] = Json::array();
	value["inference_units"].erase("alpha");
	value["callable_lookups"] = Json::object();
	value["inference_reads"] = Json::object();
	value["encoding_reuse"] = graphEncodingReuse(false, false);
	value = check(value, 0);
	auto added = graph();
	added["cached_encoding"] = std::move(value.at("cached_encoding"));
	check(added, 0);
}
