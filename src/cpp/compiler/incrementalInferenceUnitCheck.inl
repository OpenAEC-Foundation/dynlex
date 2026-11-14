class InferenceUnitCheck {
  public:
	InferenceUnitCheck(
		const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths,
		const Json &graph, const IncrementalInferenceReads &reads, const IncrementalUnitSelection &stable,
		BuildStatistics &statistics, std::map<std::string, std::string> &fingerprints,
		const std::set<InferenceUnitKey> &unavailableValues, const std::map<InferenceUnitKey, Json> &verifiedValues
	)
		: inputs(context, projection, paths, statistics, fingerprints), graph(graph), reads(reads), stable(stable),
		  unavailableValues(unavailableValues), verifiedValues(verifiedValues), statistics(statistics) {}

	bool unchanged(const InferenceUnitKey &root) {
		const auto &units = graph.at("inference_units");
		if (!units.contains(root.first) || !units.at(root.first).contains(root.second))
			return false;
		std::set<InferenceUnitKey> visited;
		std::stack<std::pair<InferenceUnitKey, bool>> pending;
		pending.emplace(root, false);
		while (!pending.empty()) {
			auto [key, interfaceOnly] = pending.top();
			pending.pop();
			if (!inputs.contains(key.first))
				return false;
			if ((interfaceOnly && stable.contains(inputs.section(key.first), key.second)) || clean.contains(key) ||
				!visited.insert(key).second)
				continue;
			if (unavailableValues.contains(key))
				return false;
			const Json &unit = graph.at("inference_units").at(key.first).at(key.second);
			if (!directInputsUnchanged(key))
				return false;
			for (const Json &callee : unit.at("callees"))
				pending.emplace(InferenceUnitKey{callee.at(0).get<std::string>(), callee.at(1).get<std::string>()}, true);
			for (const Json &query : unit.at("values")) {
				InferenceUnitKey callee{query.at(0).get<std::string>(), query.at(1).get<std::string>()};
				auto verified = verifiedValues.find(callee);
				if (query.at(2).is_null() || query.at(3).is_null() || verified == verifiedValues.end()) {
					pending.emplace(std::move(callee), false);
					continue;
				}
				statistics.valueDependencyQueriesChecked++;
				auto result = verified->second.find(query.at(2).get_ref<const std::string &>());
				if (result == verified->second.end() || *result != query.at(3))
					return false;
				statistics.valueDependencyQueriesReused++;
			}
		}
		// Commit positive results only after checking the entire reachable cycle.
		clean.insert(visited.begin(), visited.end());
		return true;
	}

	bool directInputsUnchanged(const InferenceUnitKey &key) {
		const Json &unit = graph.at("inference_units").at(key.first).at(key.second);
		auto [direct, inserted] = directUnchanged.try_emplace(key, true);
		if (inserted) {
			for (const auto &[source, fingerprint] : unit.at("sources").items())
				if (!inputs.contains(source) || inputs.fingerprint(source) != fingerprint.get_ref<const std::string &>())
					direct->second = false;
			for (const auto &[lookup, fingerprint] : unit.at("lookups").items())
				if (reads.changedKeys().contains(lookup) ||
					IncrementalInferenceReads::fingerprint(reads.observation(lookup, &graph.at("callable_lookups"))) !=
						fingerprint.get_ref<const std::string &>())
					direct->second = false;
		}
		return direct->second;
	}

	void interfacesChanged() { clean.clear(); }

  private:
	InferenceUnitInputs inputs;
	const Json &graph;
	const IncrementalInferenceReads &reads;
	const IncrementalUnitSelection &stable;
	const std::set<InferenceUnitKey> &unavailableValues;
	const std::map<InferenceUnitKey, Json> &verifiedValues;
	BuildStatistics &statistics;
	std::map<InferenceUnitKey, bool> directUnchanged;
	std::set<InferenceUnitKey> clean;
};
