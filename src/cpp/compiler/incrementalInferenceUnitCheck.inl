class InferenceUnitCheck {
  public:
	InferenceUnitCheck(
		const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths,
		const Json &graph, const IncrementalInferenceReads &reads, const IncrementalUnitSelection &stable,
		BuildStatistics &statistics, std::map<std::string, std::string> &fingerprints,
		const std::set<InferenceUnitKey> &unavailableValues
	)
		: inputs(context, projection, paths, statistics, fingerprints), graph(graph), reads(reads), stable(stable),
		  unavailableValues(unavailableValues) {}

	bool unchanged(const InferenceUnitKey &root) {
		std::set<InferenceUnitKey> visited;
		std::stack<InferenceUnitKey> pending;
		pending.push(root);
		while (!pending.empty()) {
			auto key = pending.top();
			pending.pop();
			if (!inputs.contains(key.first))
				return false;
			if (stable.contains(inputs.section(key.first), key.second) || clean.contains(key) || !visited.insert(key).second)
				continue;
			if (unavailableValues.contains(key))
				return false;
			const Json &unit = graph.at("inference_units").at(key.first).at(key.second);
			auto [direct, inserted] = directUnchanged.try_emplace(key, true);
			if (inserted) {
				for (const auto &[source, fingerprint] : unit.at("sources").items())
					if (!inputs.contains(source) || inputs.fingerprint(source) != fingerprint.get_ref<const std::string &>())
						direct->second = false;
				for (const auto &[lookup, fingerprint] : unit.at("lookups").items()) {
					if (reads.changedKeys().contains(lookup)) {
						direct->second = false;
						continue;
					}
					if (digest(reads.observation(lookup, &graph.at("callable_lookups")).at("candidates").dump()) !=
						fingerprint.get_ref<const std::string &>())
						direct->second = false;
				}
			}
			if (!direct->second)
				return false;
			for (const Json &callee : unit.at("callees"))
				pending.emplace(callee.at(0).get<std::string>(), callee.at(1).get<std::string>());
		}
		// Commit positive results only after checking the entire reachable cycle.
		clean.insert(visited.begin(), visited.end());
		return true;
	}

  private:
	InferenceUnitInputs inputs;
	const Json &graph;
	const IncrementalInferenceReads &reads;
	const IncrementalUnitSelection &stable;
	const std::set<InferenceUnitKey> &unavailableValues;
	std::map<InferenceUnitKey, bool> directUnchanged;
	std::set<InferenceUnitKey> clean;
};
