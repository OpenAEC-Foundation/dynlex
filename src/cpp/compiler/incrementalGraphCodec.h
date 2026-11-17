#pragma once
#include <cstddef>
#include <nlohmann/json_fwd.hpp>

struct GraphEncodingWork {
	size_t columnsReused = 0, columnsBuilt = 0, recordsBuilt = 0, recordsReused = 0;
};

nlohmann::json graphEncodingReuse(bool edges, bool variants);
nlohmann::json encodeSectionGraph(nlohmann::json &graph, GraphEncodingWork *work);
nlohmann::json decodeSectionGraph(nlohmann::json encoded);
