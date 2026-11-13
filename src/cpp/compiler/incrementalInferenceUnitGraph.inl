using InferenceUnitKey = std::pair<std::string, std::string>;

Json emptyInferenceUnit() {
	return Json{
		{"sources", Json::object()},
		{"callees", Json::array()},
		{"lookups", Json::object()},
		{"values", Json::array()},
		{"global_inputs", Json::object()}
	};
}

class InferenceUnitGraphCapture {
  public:
	InferenceUnitGraphCapture(
		const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths,
		const IncrementalInferenceReads &reads, const Json &graph, Json *priorUnits,
		const std::map<std::string, std::set<std::string>> &lookupOwners, BuildStatistics &statistics,
		std::map<std::string, std::string> &fingerprints, const IncrementalInferenceProducts *products,
		InferenceGlobalInputs &globalInputs, const std::map<std::string, std::set<std::string>> &retained
	)
		: context(context), paths(paths), identities(paths), reads(reads), graph(graph), priorUnits(priorUnits),
		  inputs(context, projection, paths, statistics, fingerprints), statistics(statistics), products(products),
		  globalInputs(globalInputs), retained(retained) {
		for (const auto &[key, owners] : lookupOwners)
			for (const std::string &owner : owners)
				lookupsByOwner[owner].insert(key);
	}

	Json capture(const Json &summaries, const Json &functionSections, const Json &functionKeys) {
		if (products)
			for (const auto &[scope, key] : products->scopes())
				mappedScopes.emplace(scope, key);
		for (const auto &[section, path] : paths) {
			units[path] = Json::object();
			for (const auto &[key, instantiation] : section->instantiations) {
				if (instantiation.inferring || instantiation.needsReinfer ||
					(!instantiation.failure && (!instantiation.valid || !instantiation.returnType.isDeduced())))
					continue;
				requireCompilerInvariant(instantiation.storageScope != nullptr, "inference graph unit has no storage scope");
				auto identity = inferenceSummaryKey(key, identities);
				if (identity)
					mappedScopes.emplace(instantiation.storageScope, InferenceUnitKey{path, *identity});
			}
		}
		mappedScopes.emplace(IncrementalInferenceReads::Scope{}, InferenceUnitKey{"root", aggregateGraphVariant});
		if (products)
			for (const auto &[scope, key] : products->scopes()) {
				Json &node = units[key.first][key.second];
				if (node.is_null())
					node = emptyInferenceUnit();
				mergeScope(node, scope, inputs.section(key.first), false);
				node["global_inputs"] = products->references().at(key.first).at(key.second).at("global_inputs");
				statistics.inferenceUnitsCaptured++;
			}
		for (const auto &[section, path] : paths)
			for (const auto &[key, instantiation] : section->instantiations) {
				if (instantiation.inferring || instantiation.needsReinfer ||
					(!instantiation.failure && (!instantiation.valid || !instantiation.returnType.isDeduced())))
					continue;
				auto identity = inferenceSummaryKey(key, identities);
				if (!identity)
					continue;
				bool reused = instantiation.inferenceReusedFromCache;
				Json node = units[path].contains(*identity) ? std::move(units[path][*identity])
							: reused						? std::move(priorUnit(path, *identity))
															: emptyInferenceUnit();
				mergeScope(node, instantiation.storageScope, section, reused);
				node["global_inputs"] = globalInputs.capture(instantiation).value_or(Json(nullptr));
				(reused ? statistics.inferenceUnitsReused : statistics.inferenceUnitsCaptured)++;
				units[path][*identity] = std::move(node);
			}
		Json root = context.reuseRootExecution ? std::move(priorUnit("root", aggregateGraphVariant)) : emptyInferenceUnit();
		mergeScope(root, {}, context.mainSection, context.reuseRootExecution);
		if (!context.reuseRootExecution)
			root["global_inputs"] = globalInputs.captureScope({}).value_or(Json(nullptr));
		(context.reuseRootExecution ? statistics.inferenceUnitsReused : statistics.inferenceUnitsCaptured)++;
		units["root"][aggregateGraphVariant] = std::move(root);
		for (const auto &[path, variants] : summaries.items())
			for (const auto &[key, reference] : variants.items()) {
				retain(path, key);
				units[path][key]["global_inputs"] = reference.at("global_inputs");
				for (const Json &target : reference.at("symbolic_dependencies"))
					units[path][key]["callees"].push_back(target);
				if (!reference.at("body").is_null())
					for (const Json &target : reference.at("body").at("dependencies"))
						units[path][key]["callees"].push_back(target);
			}
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
		propagateGlobalInputs();
		return std::move(units);
	}

  private:
	void propagateGlobalInputs() {
		std::map<InferenceUnitKey, std::set<InferenceUnitKey>> callers;
		std::queue<InferenceUnitKey> pending;
		std::set<InferenceUnitKey> queued;
		for (const auto &[path, variants] : units.items())
			for (const auto &[key, unit] : variants.items()) {
				InferenceUnitKey owner{path, key};
				for (const auto &callee : unit.at("callees"))
					callers[{callee.at(0).get<std::string>(), callee.at(1).get<std::string>()}].insert(owner);
				for (const auto &query : unit.at("values"))
					callers[{query.at(0).get<std::string>(), query.at(1).get<std::string>()}].insert(owner);
				if (unit.at("global_inputs").is_null() || !unit.at("global_inputs").empty()) {
					pending.push(owner);
					queued.insert(owner);
				}
			}
		while (!pending.empty()) {
			auto dependency = pending.front();
			pending.pop();
			queued.erase(dependency);
			const Json &inputs = units.at(dependency.first).at(dependency.second).at("global_inputs");
			for (const auto &caller : callers[dependency]) {
				Json &current = units.at(caller.first).at(caller.second).at("global_inputs");
				if (mergeInferenceGlobalInputs(current, inputs) && queued.insert(caller).second)
					pending.push(caller);
			}
		}
	}
	Json &priorUnit(const std::string &path, const std::string &key) const {
		requireCompilerInvariant(priorUnits, "reused inference unit has no prior graph");
		return priorUnits->at(path).at(key);
	}
	void addLookup(Json &unit, const std::string &key) const {
		unit["lookups"][key] = IncrementalInferenceReads::fingerprint(reads.observation(key, &graph.at("callable_lookups")));
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
				if (queries.opaqueArguments)
					unit["values"].push_back(Json::array({target->second.first, target->second.second, nullptr, nullptr}));
				for (const auto &[arguments, value] : queries.results) {
					unit["values"].push_back(Json::array({target->second.first, target->second.second, arguments, value}));
				}
			}
		}
	}
	void retain(const std::string &path, const std::string &key) {
		if (units.at(path).contains(key))
			return;
		if (key == aggregateGraphVariant) {
			Json aggregate = emptyInferenceUnit();
			if (retained.contains(path) && retained.at(path).contains(key))
				aggregate["global_inputs"] = priorUnit(path, key).at("global_inputs");
			for (const auto &[arguments, instance] : inputs.section(path)->instantiations) {
				if ((!instance.valid && !instance.failure) || instance.needsReinfer || instance.inferring)
					continue;
				auto captured = globalInputs.capture(instance);
				mergeInferenceGlobalInputs(aggregate["global_inputs"], captured.value_or(Json(nullptr)));
			}
			if (products && products->references().contains(path))
				for (const auto &[variant, reference] : products->references().at(path).items())
					mergeInferenceGlobalInputs(aggregate["global_inputs"], reference.at("global_inputs"));
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
	InferenceValueIdentities identities;
	const IncrementalInferenceReads &reads;
	const Json &graph;
	Json *priorUnits;
	InferenceUnitInputs inputs;
	BuildStatistics &statistics;
	const IncrementalInferenceProducts *products;
	InferenceGlobalInputs &globalInputs;
	const std::map<std::string, std::set<std::string>> &retained;
	std::map<std::string, IncrementalInferenceReads::Keys> lookupsByOwner;
	Json units = Json::object();
	std::map<IncrementalInferenceReads::Scope, InferenceUnitKey, std::owner_less<IncrementalInferenceReads::Scope>>
		mappedScopes;
};
