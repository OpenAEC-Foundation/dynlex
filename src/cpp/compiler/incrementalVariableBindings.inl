// Logical binding products contain ownership decisions, never process pointers.
// Scope identities describe the current tree after receiver lowering.
class IncrementalVariableBindings {
	ParseContext &context;
	IncrementalMatcherCache &cache;
	BuildStatistics &statistics;
	VariableBindingHooks hooks;
	Json records, current = Json::object();
	const Json empty = Json::object();
	IncrementalSourceScopes &sourceScopes;
	const InitialMatcherProjection &matcher;
	bool prepared = false;
	std::unordered_set<std::string> recorded;
	std::map<const Section *, std::string> proofs;

	const std::string &path(Section *section) { return sourceScopes.identities().at(section); }

	Json proof(const VariableBindingUnit &query) {
		if (!prepared) {
			const Json &projection = matcher.projection();
			Json ambient = Json::object(), semanticBodies = Json::object();
			for (const auto &[uri, source] : projection.at("ambient_nonexecution_sources").items())
				ambient[uri] = source.at("digest");
			for (const auto &[id, scope] : sourceScopes.canonical())
				if (projection.at("bodies").contains(id) &&
					(scope->type != SectionType::Function || scope->dataSection || containsNestedDefinition(*scope)))
					semanticBodies[id] = projection.at("bodies").at(id);
			std::string environment = digest(
				Json::array({context.builtinSyntax.nonMessageIdentity, context.projectSyntax.nonMessageIdentity,
							 std::move(ambient), std::move(semanticBodies), projection.at("global_resolution"),
							 projection.at("declaration_digests"), cache.precedenceSignature()})
					.dump()
			);
			for (const auto &[id, scope] : sourceScopes.canonical())
				if (scope->type == SectionType::Function && !scope->isFlex && !containsNestedDefinition(*scope) &&
					projection.at("body_units").contains(id))
					proofs.emplace(
						scope,
						digest(
							Json::array({environment, projection.at("bodies").at(id), projection.at("body_units").at(id)})
								.dump()
						)
					);
			prepared = true;
		}
		auto found = proofs.find(query.domain);
		return found == proofs.end() ? Json(nullptr) : Json::array({found->second, query.references.size()});
	}

	void store(const std::string &domain, const std::string &id, Json record) {
		auto started = Clock::now();
		std::string key = cache.recordClassificationData(record);
		recorded.insert(key);
		current[domain][id] = std::move(key);
		statistics.bindingRecordMilliseconds += elapsedMilliseconds(started);
	}

	std::string input(const VariableBindingUnit &unit) {
		Json scopes = Json::object(), events = Json::array(), referenceScopes = Json::array();
		using Event = std::tuple<int, int, bool, std::string>;
		std::vector<Event> ordered;
		std::unordered_set<Section *> visited;
		for (VariableReference *reference : unit.references) {
			Section *scope = reference->range.section();
			referenceScopes.push_back(path(scope));
			ordered.emplace_back(reference->range.line->mergedLineIndex, reference->range.start(), true, path(scope));
			for (; scope; scope = scope == unit.domain ? nullptr : scope->parent) {
				if (!visited.insert(scope).second)
					break;
				auto declaration = scope->explicitVariableDeclarations.find(unit.name);
				bool data = scope->dataVariableDeclarations.contains(unit.name);
				scopes[path(scope)] = Json::array(
					{scope == unit.domain ? Json(nullptr) : Json(path(scope->parent)), static_cast<int>(scope->type),
					 scope->isFlex, std::ranges::find(scope->globalVariables, unit.name) != scope->globalVariables.end(),
					 declaration != scope->explicitVariableDeclarations.end(), data}
				);
				if (declaration != scope->explicitVariableDeclarations.end() && !data) {
					const Range &range = declaration->second;
					ordered.emplace_back(range.line->mergedLineIndex, range.start(), false, path(scope));
				}
			}
		}
		std::stable_sort(ordered.begin(), ordered.end());
		for (const auto &[line, column, reference, scope] : ordered)
			events.push_back(Json::array({scope, reference}));
		return digest(Json::array({unit.global, std::move(scopes), std::move(events), std::move(referenceScopes)}).dump());
	}

	VariableBindingHooks::Groups
	unit(const VariableBindingUnit &query, const std::function<VariableBindingHooks::Groups()> &evaluate) {
		auto started = Clock::now();
		const std::string &domain = path(query.domain);
		std::string id = digest(Json::array({domain, query.name}).dump());
		Json currentProof = proof(query);
		statistics.bindingInputMilliseconds += elapsedMilliseconds(started);
		started = Clock::now();
		const Json &domainRecords = records.contains(domain) ? records.at(domain) : empty;
		auto found = domainRecords.find(id);
		auto record =
			found == domainRecords.end() ? std::optional<Json>{} : cache.restoreClassificationData(found->get<std::string>());
		statistics.bindingLookupMilliseconds += elapsedMilliseconds(started);
		started = Clock::now();
		bool sameInputs = record && !currentProof.is_null() && record->at("proof") == currentProof;
		std::string fingerprint;
		if (sameInputs) {
			fingerprint = record->at("input").get<std::string>();
			statistics.bindingInputsReused++;
		} else
			fingerprint = input(query);
		statistics.bindingInputMilliseconds += elapsedMilliseconds(started);
		if (record && record->at("input") == fingerprint) {
			started = Clock::now();
			VariableBindingHooks::Groups groups;
			for (const Json &group : record->at("groups")) {
				VariableBindingGroup restored{
					sourceScopes.compact().at(group.at(0).get<std::string>()),
					query.references.at(group.at(1).get<size_t>()),
					{}
				};
				for (const Json &index : group.at(2))
					restored.references.push_back(query.references.at(index.get<size_t>()));
				groups.push_back(std::move(restored));
			}
			statistics.bindingLookupMilliseconds += elapsedMilliseconds(started);
			if (record->at("proof") != currentProof) {
				(*record)["proof"] = std::move(currentProof);
				store(domain, id, std::move(*record));
			} else
				current[domain][id] = *found;
			hooks.work.reused++;
			return groups;
		}
		started = Clock::now();
		auto groups = evaluate();
		statistics.bindingEvaluationMilliseconds += elapsedMilliseconds(started);
		started = Clock::now();
		std::map<const VariableReference *, size_t> indices;
		for (size_t index = 0; index < query.references.size(); index++)
			indices.emplace(query.references[index], index);
		Json outputs = Json::array();
		for (const auto &group : groups) {
			Json references = Json::array();
			for (VariableReference *reference : group.references)
				references.push_back(indices.at(reference));
			outputs.push_back(Json::array({path(group.owner), indices.at(group.definition), std::move(references)}));
		}
		statistics.bindingRecordMilliseconds += elapsedMilliseconds(started);
		store(domain, id, Json{{"input", fingerprint}, {"proof", std::move(currentProof)}, {"groups", std::move(outputs)}});
		return groups;
	}

  public:
	IncrementalVariableBindings(
		ParseContext &context, IncrementalMatcherCache &cache, BuildStatistics &statistics,
		IncrementalSourceScopes &sourceScopes, const InitialMatcherProjection &matcher, const Json *previous,
		const Json *failure
	)
		: context(context), cache(cache), statistics(statistics), hooks{{}, {}, statistics.bindings},
		  records(previous ? previous->at("all_function_projection").at("binding_units") : Json::object()),
		  sourceScopes(sourceScopes), matcher(matcher) {
		if (failure)
			for (const auto &[domain, entries] : failure->at("binding_units").items())
				for (const auto &[id, key] : entries.items())
					records[domain][id] = key;
		hooks.unit = [&](const VariableBindingUnit &query, const std::function<VariableBindingHooks::Groups()> &evaluate) {
			return unit(query, evaluate);
		};
		hooks.complete = [&](Section *domain) {
			if (domain) {
				const std::string &id = path(domain);
				if (current.contains(id)) {
					records[id] = std::move(current.at(id));
					current.erase(id);
				}
			} else {
				for (const auto &[id, entries] : records.items())
					if (auto scope = sourceScopes.compact().find(id); scope != sourceScopes.compact().end() &&
																	  context.deferredBodyResolution &&
																	  context.deferredBodyResolution(scope->second)) {
						current[id] = entries;
						hooks.work.reused += entries.size();
						statistics.bindingInputsReused += entries.size();
					}
				records = std::move(current);
				current = Json::object();
			}
		};
		context.variableBindingHooks = &hooks;
	}
	~IncrementalVariableBindings() { context.variableBindingHooks = nullptr; }
	Json snapshot() {
		for (auto &[domain, entries] : records.items()) {
			(void)domain;
			for (auto record = entries.begin(); record != entries.end();)
				if (recorded.contains(record->get_ref<const std::string &>()))
					++record;
				else if (cache.contains(record->get_ref<const std::string &>())) {
					cache.retain(record->get_ref<const std::string &>());
					++record;
				} else
					record = entries.erase(record);
		}
		return records;
	}
};
