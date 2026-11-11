// Projected references need shapes and initial/final lookup keys, independently
// of the expanded-function identity records used by native IR reuse.
std::string IncrementalMatcherCache::recordReferenceKeys(const std::vector<PatternReference *> &references) {
	std::string packed(1, '\3');
	appendLength(packed, references.size());
	for (const PatternReference *reference : references) {
		appendString(packed, reference->pattern.text);
		appendLength(packed, static_cast<int>(reference->patternType));
		appendLength(packed, static_cast<int>(reference->purpose));
		appendString(packed, reference->match ? std::string_view(reference->incrementalMatcherKey) : std::string_view{});
		appendString(packed, reference->match ? std::string_view(reference->initialMatcherKey) : std::string_view{});
	}
	std::string key = digest(packed);
	if (auto previous = previousEntries.find(key); previous != previousEntries.end())
		recorded[key] = previous->second;
	else
		recorded[key] = std::move(packed);
	return key;
}

std::optional<std::vector<IncrementalMatcherCache::ReferenceMatchKeys>>
IncrementalMatcherCache::restoreReferenceKeys(std::string_view key) {
	auto entry = previousEntries.find(key);
	if (entry == previousEntries.end())
		return std::nullopt;
	std::string_view packed = entry->second;
	if (packed.empty() || packed.front() != '\3')
		throw std::runtime_error("invalid projected reference cache kind");
	size_t position = 1;
	std::uint64_t count = readLength(packed, position);
	if (count > (packed.size() - position) / 5)
		throw std::runtime_error("invalid projected reference count");
	std::vector<ReferenceMatchKeys> result;
	result.reserve(count);
	for (std::uint64_t index = 0; index < count; index++) {
		std::string_view pattern = readString(packed, position);
		std::uint64_t type = readLength(packed, position);
		std::uint64_t purpose = readLength(packed, position);
		if (type >= static_cast<size_t>(SectionType::Count) ||
			purpose > static_cast<size_t>(PatternReference::Purpose::TypeConstraint))
			throw std::runtime_error("invalid projected reference shape");
		std::string_view finalKey = readString(packed, position);
		std::string_view initialKey = readString(packed, position);
		result.push_back({pattern, static_cast<int>(type), static_cast<int>(purpose), finalKey, initialKey});
	}
	if (position != packed.size())
		throw std::runtime_error("trailing projected reference cache data");
	retain(key);
	return result;
}
