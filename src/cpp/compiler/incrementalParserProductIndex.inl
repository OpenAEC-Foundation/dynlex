// Parser products have their own publication boundary. A failed import must not
// hide completed source units behind an unavailable semantic program snapshot.
class ParserProductIndex {
  public:
	ParserProductIndex(fs::path directory, fs::path index) : directory(std::move(directory)), index(std::move(index)) {}

	std::string load() const {
		auto key = readEntry(index);
		if (!key)
			return {};
		if (key->size() != 64 || key->find_first_not_of("0123456789abcdef") != std::string::npos)
			throw std::runtime_error("invalid parser product index: " + index.string());
		fs::path path = directory / ("parser-" + *key + ".entry");
		auto payload = readEntry(path);
		if (!payload)
			return {}; // The independently evictable product is no longer available.
		if (digest(*payload) != *key)
			throw std::runtime_error("corrupt parser cache entry: " + path.string());
		return std::move(*payload);
	}

	void publish(const IncrementalParsedExpressionCache &cache, IncrementalParsedExpressionCache::SnapshotScope scope) const {
		std::string payload = cache.snapshot(scope);
		std::string key = digest(payload);
		writeEntry(directory / ("parser-" + key + ".entry"), payload);
		writeEntry(index, key);
	}

  private:
	fs::path directory, index;
};
