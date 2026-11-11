// Topology, query literals, candidate signatures and promoted parameters are
// independent metadata units. CBOR preserves their structured values portably.
std::string IncrementalMatcherCache::recordClassificationData(const Json &value) {
	auto bytes = Json::to_cbor(value);
	std::string packed(1, '\5');
	packed.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	std::string key = digest(packed);
	if (auto previous = previousEntries.find(key); previous != previousEntries.end())
		recorded[key] = previous->second;
	else
		recorded[key] = std::move(packed);
	return key;
}

std::optional<IncrementalMatcherCache::Json> IncrementalMatcherCache::restoreClassificationData(std::string_view key) {
	auto entry = previousEntries.find(key);
	if (entry == previousEntries.end())
		return std::nullopt;
	std::string_view packed = entry->second;
	if (packed.empty() || packed.front() != '\5')
		throw std::runtime_error("invalid classification cache kind");
	Json value = Json::from_cbor(packed.begin() + 1, packed.end());
	retain(key);
	return value;
}
