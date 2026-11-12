// A digest prefix partitions matcher metadata independently of source locations.
// Only live entries enter the next index; retaining a segment requires exactly
// its former keys and values, so removed bodies do not accumulate old records.
namespace {
size_t matcherSegmentIndex(std::string_view prefix) {
	if (prefix.size() < 2 || !std::all_of(prefix.begin(), prefix.end(), [](char character) {
		return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
	}))
		throw std::runtime_error("invalid incremental matcher cache key");
	auto digit = [](char character) {
		return character <= '9' ? character - '0' : character - 'a' + 10;
	};
	return digit(prefix[0]) * 16 + digit(prefix[1]);
}
} // namespace

IncrementalMatcherCache::IncrementalMatcherCache(ParseContext &context, std::string_view previous, LoadSegment loadSegment)
	: context(context), loadSegment(std::move(loadSegment)) {
	if (previous.empty())
		return;
	Json index = Json::parse(previous);
	if (!index.is_object())
		throw std::runtime_error("invalid incremental matcher index");
	for (const auto &[prefix, descriptor] : index.items()) {
		if (prefix.size() != 2)
			throw std::runtime_error("invalid incremental matcher segment prefix");
		Segment &segment = segments.at(matcherSegmentIndex(prefix));
		segment.key = descriptor.at("payload").get<std::string>();
		segment.entries = descriptor.at("entries").get<size_t>();
		if (segment.key.size() != 64 || !segment.entries)
			throw std::runtime_error("invalid incremental matcher segment descriptor");
		matcherSegmentIndex(segment.key);
	}
}

decltype(IncrementalMatcherCache::previousEntries)::iterator IncrementalMatcherCache::findPrevious(std::string_view key) {
	if (key.size() != 64)
		throw std::runtime_error("invalid incremental matcher cache key length");
	size_t index = matcherSegmentIndex(key);
	Segment &segment = segments[index];
	if (!segment.loaded) {
		segment.loaded = true;
		if (!segment.key.empty()) {
			if (auto bytes = loadSegment(segment.key)) {
				if (digest(*bytes) != segment.key)
					throw std::runtime_error("corrupt incremental matcher segment");
				segment.bytes = std::move(*bytes);
				std::string_view input(segment.bytes);
				if (!input.starts_with(matcherCacheHeader))
					throw std::runtime_error("invalid incremental matcher segment header");
				size_t position = matcherCacheHeader.size(), count = 0;
				while (position < input.size()) {
					std::string_view cacheKey = input.substr(position, 64);
					if (cacheKey.size() != 64 || matcherSegmentIndex(cacheKey) != index)
						throw std::runtime_error("invalid incremental matcher segment entry");
					position += 64;
					std::string_view value = readString(input, position);
					if (!previousEntries.emplace(cacheKey, value).second)
						throw std::runtime_error("duplicate incremental matcher cache key");
					count++;
				}
				if (count != segment.entries)
					throw std::runtime_error("invalid incremental matcher segment count");
				storage.loaded++;
				storage.loadedBytes += input.size();
			} else
				storage.missing++;
		}
	}
	return previousEntries.find(key);
}

void IncrementalMatcherCache::recordValue(const std::string &key, std::string value) {
	if (auto previous = findPrevious(key); previous != previousEntries.end() && previous->second == value)
		recorded[key] = previous->second;
	else
		recorded[key] = std::move(value);
}

IncrementalMatcherCache::Snapshot IncrementalMatcherCache::snapshot() {
	Snapshot result;
	Json index = Json::object();
	for (auto begin = recorded.begin(); begin != recorded.end();) {
		auto end = begin;
		std::string prefix = begin->first.substr(0, 2);
		size_t count = 0;
		bool unchanged = true;
		while (end != recorded.end() && end->first.starts_with(prefix)) {
			unchanged &= std::holds_alternative<std::string_view>(end->second);
			count++;
			++end;
		}
		const Segment &previous = segments.at(matcherSegmentIndex(prefix));
		std::string key;
		if (unchanged && count == previous.entries) {
			key = previous.key;
			storage.reused++;
		} else {
			std::string bytes(matcherCacheHeader);
			for (auto entry = begin; entry != end; ++entry) {
				bytes += entry->first;
				const auto &value = entry->second;
				appendString(
					bytes, std::holds_alternative<std::string_view>(value) ? std::get<std::string_view>(value)
																		   : std::string_view(std::get<std::string>(value))
				);
			}
			key = digest(bytes);
			result.segments.emplace_back(key, std::move(bytes));
			storage.built++;
		}
		index[prefix] = Json{{"payload", key}, {"entries", count}};
		begin = end;
	}
	result.index = index.dump();
	return result;
}
