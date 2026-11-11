using InferenceUnitKey = std::pair<std::string, std::string>;

Json emptyInferenceUnit() {
	return Json{
		{"sources", Json::object()}, {"callees", Json::array()}, {"lookups", Json::object()}, {"values", Json::array()}
	};
}

class InferenceUnitGraphCapture {
  public:
	InferenceUnitGraphCapture(
		const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths,
		const IncrementalInferenceReads &reads, const Json &graph, Json *priorUnits,
		const std::map<std::string, std::set<std::string>> &lookupOwners, BuildStatistics &statistics,
		std::map<std::string, std::string> &fingerprints
	)
		: context(context), paths(paths), classes(classIdentityPaths(paths)), reads(reads), graph(graph),
		  priorUnits(priorUnits), inputs(context, projection, paths, statistics, fingerprints), statistics(statistics) {
		for (const auto &[key, owners] : lookupOwners)
			for (const std::string &owner : owners)
				lookupsByOwner[owner].insert(key);
	}

	Json capture(const Json &summaries, const Json &functionSections, const Json &functionKeys) {
		for (const auto &[section, path] : paths) {
			units[path] = Json::object();
			for (const auto &[key, instantiation] : section->instantiations) {
				if (!instantiation.valid || instantiation.inferring || instantiation.needsReinfer || !instantiation.body)
					continue;
				auto identity = inferenceSummaryKey(key, classes);
				if (identity)
					mappedScopes.emplace(instantiation.body->storageScope, InferenceUnitKey{path, *identity});
			}
		}
		mappedScopes.emplace(IncrementalInferenceReads::Scope{}, InferenceUnitKey{"root", aggregateGraphVariant});
		for (const auto &[section, path] : paths)
			for (const auto &[key, instantiation] : section->instantiations) {
				if (!instantiation.valid || instantiation.inferring || instantiation.needsReinfer || !instantiation.body)
					continue;
				auto identity = inferenceSummaryKey(key, classes);
				if (!identity)
					continue;
				bool reused = instantiation.inferenceReusedFromCache;
				Json node = reused ? std::move(priorUnit(path, *identity)) : emptyInferenceUnit();
				mergeScope(node, instantiation.body->storageScope, section, reused);
				(reused ? statistics.inferenceUnitsReused : statistics.inferenceUnitsCaptured)++;
				units[path][*identity] = std::move(node);
			}
		Json root = context.reuseRootExecution ? std::move(priorUnit("root", aggregateGraphVariant)) : emptyInferenceUnit();
		mergeScope(root, {}, context.mainSection, context.reuseRootExecution);
		(context.reuseRootExecution ? statistics.inferenceUnitsReused : statistics.inferenceUnitsCaptured)++;
		units["root"][aggregateGraphVariant] = std::move(root);
		for (const auto &[path, variants] : summaries.items())
			for (const auto &[key, _] : variants.items())
				retain(path, key);
		for (const auto &[name, owner] : functionSections.items())
			retain(owner.get<std::string>(), functionKeys.at(name).get<std::string>());
		std::vector<InferenceUnitKey> referenced;
		for (const auto &[path, variants] : units.items())
			for (const auto &[key, unit] : variants.items())
				for (const Json &callee : unit.at("callees"))
					referenced.emplace_back(callee.at(0).get<std::string>(), callee.at(1).get<std::string>());
		for (const auto &[path, key] : referenced)
			retain(path, key);
		for (auto &[path, variants] : units.items())
			for (auto &[key, unit] : variants.items()) {
				std::set<InferenceUnitKey> unique;
				for (const Json &callee : unit.at("callees"))
					unique.emplace(callee.at(0).get<std::string>(), callee.at(1).get<std::string>());
				unit["callees"] = unique;
				std::set<Json> values(unit.at("values").begin(), unit.at("values").end());
				unit["values"] = values;
			}
		return std::move(units);
	}

  private:
	Json &priorUnit(const std::string &path, const std::string &key) const {
		requireCompilerInvariant(priorUnits, "reused inference unit has no prior graph");
		return priorUnits->at(path).at(key);
	}
	void addLookup(Json &unit, const std::string &key) const {
		unit["lookups"][key] = digest(reads.observation(key, &graph.at("callable_lookups")).at("candidates").dump());
	}
	void mergeScope(Json &unit, const IncrementalInferenceReads::Scope &scope, const Section *section, bool reused) {
		if (!reused)
			unit["sources"][paths.at(section)] = inputs.fingerprint(paths.at(section));
		std::set<IncrementalInferenceReads::Scope, std::owner_less<IncrementalInferenceReads::Scope>> visited;
		std::stack<IncrementalInferenceReads::Scope> pending;
		pending.push(scope);
		while (!pending.empty()) {
			auto current = pending.top();
			pending.pop();
			if (!visited.insert(current).second)
				continue;
			auto found = reads.scopes().find(current);
			if (found == reads.scopes().end())
				continue; // A body containing no pattern calls or lookups has no extra reads.
			for (const std::string &source : found->second.sources) {
				if (!reused || !unit.at("sources").contains(source))
					unit["sources"][source] = inputs.fingerprint(source);
				if (inputs.section(source)->type == SectionType::Class)
					unit["callees"].push_back(Json::array({source, aggregateGraphVariant}));
			}
			for (const std::string &lookup : found->second.lookups)
				addLookup(unit, lookup);
			for (const auto &callee : found->second.callees) {
				if (auto target = mappedScopes.find(callee); target != mappedScopes.end())
					unit["callees"].push_back(Json::array({target->second.first, target->second.second}));
				else
					pending.push(callee); // Discarded trials still contribute the inputs they read.
			}
			for (const auto &[callee, queries] : found->second.values) {
				auto target = mappedScopes.find(callee);
				if (target == mappedScopes.end()) {
					pending.push(callee);
					continue;
				}
				for (const auto &[arguments, value] : queries) {
					Json encodedArguments = Json::array();
					for (const CompileTimeValue &argument : arguments) {
						auto encoded = encodeInferenceValue(argument, classes);
						if (!encoded)
							break;
						encodedArguments.push_back(std::move(*encoded));
					}
					auto result = encodeInferenceValue(value, classes);
					bool portable = result && encodedArguments.size() == arguments.size();
					Json identity =
						portable ? inferenceValueFingerprints(encodedArguments, *result) : Json::array({nullptr, nullptr});
					unit["values"].push_back(
						Json::array(
							{target->second.first, target->second.second, std::move(identity.at(0)), std::move(identity.at(1))}
						)
					);
				}
			}
		}
	}
	void retain(const std::string &path, const std::string &key) {
		if (units.at(path).contains(key))
			return;
		if (key == aggregateGraphVariant) {
			Json aggregate = emptyInferenceUnit();
			aggregate["sources"][path] = inputs.fingerprint(path);
			for (const Json &dependency : graph.at("forward").at(path))
				aggregate["callees"].push_back(Json::array({dependency, aggregateGraphVariant}));
			if (auto lookups = lookupsByOwner.find(path); lookups != lookupsByOwner.end())
				for (const std::string &lookup : lookups->second)
					addLookup(aggregate, lookup);
			if (priorUnits && priorUnits->contains(path) && priorUnits->at(path).contains(key) &&
				priorUnit(path, key) == aggregate) {
				units[path][key] = std::move(priorUnit(path, key));
				statistics.aggregateInferenceUnitsReused++;
			} else {
				units[path][key] = std::move(aggregate);
				statistics.aggregateInferenceUnitsCaptured++;
			}
		} else {
			units[path][key] = std::move(priorUnit(path, key));
			statistics.inferenceUnitsReused++;
		}
		for (const Json &callee : units.at(path).at(key).at("callees"))
			retain(callee.at(0).get<std::string>(), callee.at(1).get<std::string>());
	}
	const ParseContext &context;
	const std::map<const Section *, std::string> &paths;
	ClassIdentityPaths classes;
	const IncrementalInferenceReads &reads;
	const Json &graph;
	Json *priorUnits;
	InferenceUnitInputs inputs;
	BuildStatistics &statistics;
	std::map<std::string, IncrementalInferenceReads::Keys> lookupsByOwner;
	Json units = Json::object();
	std::map<IncrementalInferenceReads::Scope, InferenceUnitKey, std::owner_less<IncrementalInferenceReads::Scope>>
		mappedScopes;
};
