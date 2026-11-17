// A type output is usable before entry execution only after the execution
// prefix that produced it validates independently of later global readers.
class IncrementalGlobalTypeProducers {
  public:
	IncrementalGlobalTypeProducers(
		const std::map<const Section *, std::string> &paths, const Json &graph, InferenceUnitCheck &queries,
		InferenceGlobalInputs &inputs, BuildStatistics &statistics
	)
		: graph(graph), queries(queries), inputs(inputs), statistics(statistics), classes(classIdentityPaths(paths)) {
		for (const auto &[section, path] : paths)
			for (const auto &[name, variable] : section->variables)
				if (variable->isGlobal)
					variables.emplace(Json::array({path, name}).dump(), variable);
	}
	~IncrementalGlobalTypeProducers() {
		if (!committed)
			for (auto &[variable, original] : originals)
				variable->type = std::move(original);
	}
	void restoreAvailable() {
		bool progress;
		do {
			progress = false;
			for (const auto &[identity, output] : graph.at("global_type_producers").items()) {
				if (restored.contains(identity) || !variables.contains(identity))
					continue;
				statistics.globalTypeProducersChecked++;
				const auto &key = output.at("unit").get_ref<const std::string &>();
				const Json &unit = graph.at("inference_units").at("root").at(key);
				if (!inputs.unchanged(unit.at("global_inputs")) || !queries.unchanged({"root", key}))
					continue;
				auto type = decodeInferenceTypeIdentity(output.at("type"), classes);
				if (!type)
					continue;
				Variable *variable = variables.at(identity);
				if (variable->type.isDeduced() && variable->type != *type)
					continue;
				originals.try_emplace(variable, variable->type);
				variable->type = std::move(*type);
				restored.insert(identity);
				statistics.globalTypeProducersRestored++;
				progress = true;
			}
		} while (progress);
	}
	void commit() { committed = true; }

  private:
	const Json &graph;
	InferenceUnitCheck &queries;
	InferenceGlobalInputs &inputs;
	BuildStatistics &statistics;
	ClassIdentityPaths classes;
	std::map<std::string, Variable *> variables;
	std::map<Variable *, DataType> originals;
	std::set<std::string> restored;
	bool committed = false;
};
