// The manifest contains small references; type tables and observed values are
// decoded only for the concrete variants consumed by this build.
class InferenceSummaryStore {
  public:
	InferenceSummaryStore(const fs::path &directory, BuildStatistics &statistics)
		: directory(directory), statistics(statistics) {}

	void observe(const Json &keys) {
		for (const Json &key : keys) {
			const std::string &identity = key.get_ref<const std::string &>();
			available[identity] = recordCacheAccess(inferenceSummaryPath(directory, identity));
		}
	}

	const Json *load(const Json &reference) {
		const std::string &key = reference.at("payload").get_ref<const std::string &>();
		if (auto found = loaded.find(key); found != loaded.end())
			return &found->second;
		if (!available.at(key))
			return nullptr;
		auto start = Clock::now();
		auto payload = readEntry(inferenceSummaryPath(directory, key));
		if (!payload)
			throw std::runtime_error("inference summary disappeared: " + key);
		if (digest(*payload) != key)
			throw std::runtime_error("corrupt inference summary: " + key);
		Json summary = decodeInferenceSummary(Json::parse(*payload));
		statistics.inferenceSummaryUnitsLoaded++;
		statistics.inferenceSummaryLoadMilliseconds += elapsedMilliseconds(start);
		return &loaded.emplace(key, std::move(summary)).first->second;
	}

	bool retain(const Json &reference) const { return available.at(reference.at("payload").get_ref<const std::string &>()); }

	Json save(const Json &summary) {
		auto start = Clock::now();
		std::string payload = encodeInferenceSummary(summary).dump();
		std::string key = digest(payload);
		statistics.inferenceSummaryUnitsEncoded++;
		fs::path path = inferenceSummaryPath(directory, key);
		if (!recordCacheAccess(path)) {
			writeEntry(path, payload);
			statistics.inferenceSummaryUnitsWritten++;
		}
		available[key] = true;
		statistics.inferenceSummaryEncodeMilliseconds += elapsedMilliseconds(start);
		return Json{
			{"payload", std::move(key)},
			{"purity", summary.at("purity")},
			{"needs_global_types", summary.at("needs_global_types")},
			{"observed_values", !summary.at("pure_values").empty()},
			{"symbolic_dependencies", captureSymbolicInferenceDependencies(summary)},
			{"pure_values_complete", summary.at("pure_values_complete")}
		};
	}

  private:
	const fs::path &directory;
	BuildStatistics &statistics;
	std::map<std::string, Json> loaded;
	std::map<std::string, bool> available;
};
