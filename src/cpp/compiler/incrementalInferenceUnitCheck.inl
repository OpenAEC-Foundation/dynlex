class InferenceUnitCheck {
	struct Proof {
		std::optional<bool> direct;
		bool clean = false;
		std::set<Proof *> consumers;
	};
	using Proofs = std::map<InferenceUnitKey, Proof>;

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
		statistics.inferenceQueryChecks++;
		IncrementalWorkTimer timer(statistics.measureInferenceQueries ? &statistics.inferenceQueryCheckMilliseconds : nullptr);
		const auto &units = graph.at("inference_units");
		if (!units.contains(root.first) || !units.at(root.first).contains(root.second))
			return false;
		std::set<Proof *> visited;
		std::stack<std::pair<Proofs::iterator, bool>> pending;
		pending.emplace(proofs.try_emplace(root).first, false);
		while (!pending.empty()) {
			auto [node, interfaceOnly] = pending.top();
			pending.pop();
			const auto &key = node->first;
			Proof &proof = node->second;
			if (!inputs.contains(key.first))
				return false;
			if (interfaceOnly && stable.contains(inputs.section(key.first), key.second))
				continue;
			if (proof.clean) {
				statistics.inferenceQueryProofHits++;
				continue;
			}
			if (!visited.insert(&proof).second)
				continue;
			statistics.inferenceQueryUnitsChecked++;
			if (unavailableValues.contains(key))
				return false;
			const Json &unit = graph.at("inference_units").at(key.first).at(key.second);
			if (!directInputsUnchanged(key))
				return false;
			for (const Json &callee : unit.at("callees")) {
				auto child =
					proofs.try_emplace(InferenceUnitKey{callee.at(0).get<std::string>(), callee.at(1).get<std::string>()})
						.first;
				child->second.consumers.insert(&proof);
				pending.emplace(child, true);
			}
			for (const Json &query : unit.at("values")) {
				InferenceUnitKey callee{query.at(0).get<std::string>(), query.at(1).get<std::string>()};
				auto child = proofs.try_emplace(callee).first;
				child->second.consumers.insert(&proof);
				auto verified = verifiedValues.find(callee);
				if (query.at(2).is_null() || query.at(3).is_null() || verified == verifiedValues.end()) {
					pending.emplace(child, false);
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
		for (Proof *proof : visited)
			if (!proof->clean) {
				proof->clean = true;
				cleanProofs++;
			}
		return true;
	}

	bool directInputsUnchanged(const InferenceUnitKey &key) {
		const Json &unit = graph.at("inference_units").at(key.first).at(key.second);
		auto &direct = proofs.try_emplace(key).first->second.direct;
		if (!direct.has_value()) {
			direct = true;
			for (const auto &[source, fingerprint] : unit.at("sources").items())
				if (!inputs.contains(source) || inputs.fingerprint(source) != fingerprint.get_ref<const std::string &>())
					direct = false;
			for (const auto &[lookup, fingerprint] : unit.at("lookups").items())
				if (reads.changedKeys().contains(lookup) ||
					IncrementalInferenceReads::fingerprint(reads.observation(lookup, &graph.at("callable_lookups"))) !=
						fingerprint.get_ref<const std::string &>())
					direct = false;
		}
		return *direct;
	}

	void interfacesChanged(const std::set<InferenceUnitKey> &changed) {
		IncrementalWorkTimer timer(
			statistics.measureInferenceQueries ? &statistics.inferenceQueryInvalidationMilliseconds : nullptr
		);
		std::stack<Proof *> pending;
		std::set<Proof *> visited;
		for (const auto &key : changed)
			if (auto found = proofs.find(key); found != proofs.end())
				pending.push(&found->second);
		while (!pending.empty()) {
			Proof &proof = *pending.top();
			pending.pop();
			if (!visited.insert(&proof).second)
				continue;
			if (proof.clean) {
				proof.clean = false;
				cleanProofs--;
				statistics.inferenceQueryProofsInvalidated++;
			}
			for (Proof *consumer : proof.consumers) {
				statistics.inferenceQueryConsumerEdgesVisited++;
				pending.push(consumer);
			}
		}
		statistics.inferenceQueryProofsRetained += cleanProofs;
	}

  private:
	InferenceUnitInputs inputs;
	const Json &graph;
	const IncrementalInferenceReads &reads;
	const IncrementalUnitSelection &stable;
	const std::set<InferenceUnitKey> &unavailableValues;
	const std::map<InferenceUnitKey, Json> &verifiedValues;
	BuildStatistics &statistics;
	Proofs proofs;
	size_t cleanProofs = 0;
};
