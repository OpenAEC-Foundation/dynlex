#pragma once
#include <nlohmann/json_fwd.hpp>
#include <string_view>

// Definite-length CBOR emitted by nlohmann::json::to_cbor.
// Reads complete cache buffers with bounded string spans and iterative containers.
nlohmann::json parseCachedCbor(std::string_view input);
