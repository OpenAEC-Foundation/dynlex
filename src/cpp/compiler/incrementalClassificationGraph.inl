void IncrementalClassification::prepare() {
	requireCompilerInvariant(!prepared, "classification inputs were prepared twice");
	auto stageStarted = Clock::now();
	const auto &sections = counts.sections();
	const Json &bodies = counts.bodies();
	for (const auto &[_, section] : sections)
		if (definitionPatternTreeType(section) == SectionType::Function)
			for (const PatternDefinition *definition : section->patternDefinitions)
				zeroWidthFunctions |= patternCanBeEmpty(definition->patternElements);
	if (previous && compatibleObservations(*previous, observations, context)) {
		const Json &projection = previous->at("all_function_projection").at("matcher_projection");
		prior = cache.restoreClassificationData(projection.at("classifications").get_ref<const std::string &>());
		if (prior)
			for (const Json &unit : prior->at("units")) {
				// The index can outlive one of its independently evicted products.
				// Such a unit computes its metadata normally; intact units still replay.
				if (!std::ranges::all_of(std::array{Queries, Provides, Outputs}, [&](Field field) {
					return cache.contains(unit.at(field).get_ref<const std::string &>());
				}))
					continue;
				oldUnits.emplace(unit.at(Path).get<std::string>(), &unit);
				// A failed build may publish its matcher payload instead of this
				// successful program's payload. Retain the topology's complete closure.
				for (Field field : {Queries, Provides, Outputs}) {
					const std::string &key = unit.at(field).get_ref<const std::string &>();
					cache.retain(key);
				}
			}
	}
	work.loadMilliseconds += elapsedMilliseconds(stageStarted);
	stageStarted = Clock::now();
	units.reserve(sections.size() * 2);
	std::unordered_set<const Section *> masked;
	for (const auto &[path, section] : sections) {
		if (section->patternDefinitions.empty())
			continue;
		Unit unit;
		unit.path = path;
		unit.section = section;
		if (auto old = oldUnits.find(path); old != oldUnits.end())
			unit.old = old->second;
		const auto collect = [&](auto &&self, Section *body) -> void {
			unit.references.insert(unit.references.end(), body->patternReferences.begin(), body->patternReferences.end());
			for (Section *child : body->children)
				if (child->patternDefinitions.empty())
					self(self, child);
		};
		collect(collect, section);
		std::string input = declarationInput(unit);
		for (const Section *ancestor = section; ancestor; ancestor = ancestor->parent)
			unit.hasCandidates |= std::ranges::any_of(ancestor->variableLikeCounts, [](const auto &candidate) {
				return candidate.second != 0;
			});
		input += unit.hasCandidates ? '1' : '0';
		if (unit.hasCandidates) {
			unit.bodyInput = unit.old && unit.old->at(HasCandidates).get<bool>() && sameBody(path)
								 ? unit.old->at(BodyInput).get<std::string>()
								 : classificationBodyInput(unit);
			input += unit.bodyInput;
		}
		for (const Section *ancestor = section->parent; ancestor && ancestor != context.mainSection;
			 ancestor = ancestor->parent)
			if (!ancestor->patternDefinitions.empty()) {
				auto identity = sectionUnits.find(ancestor);
				requireCompilerInvariant(identity != sectionUnits.end(), "classification scope has no section identity");
				input += bodies.at(units.at(identity->second).path).get_ref<const std::string &>();
			}
		unit.input = digest(input);
		if (unit.old && prior->at("zero_width_functions") == zeroWidthFunctions &&
			unit.old->at(HasCandidates) == unit.hasCandidates && unit.old->at(BodyInput) == unit.bodyInput) {
			unit.queries = unit.old->at(Queries).get<std::string>();
			work.queryUnitsReused++;
		} else {
			unit.queries =
				save(unit.hasCandidates ? queried(unit) : Json{{"types", Json::array()}, {"literals", Json::array()}});
			work.queryUnitsBuilt++;
		}
		unit.provides =
			unit.old && unit.old->at(Input) == unit.input ? unit.old->at(Provides).get<std::string>() : save(provided(unit));
		indices.emplace(path, units.size());
		sectionUnits.emplace(section, units.size());
		if (section->parent == context.mainSection)
			masked.insert(section);
		units.push_back(std::move(unit));
	}
	work.inputMilliseconds += elapsedMilliseconds(stageStarted);
	stageStarted = Clock::now();
	prepareEnvironments();
	work.environmentMilliseconds += elapsedMilliseconds(stageStarted);
	stageStarted = Clock::now();
	sources = sourceProjection(context, observations, masked, true, true, true, prior ? &prior->at("sources") : nullptr);
	rules = Json::object();
	Json scopePaths = scopeRuleBodyPaths(sections);
	for (const auto &[path, _] : scopePaths.items())
		rules[path] = bodies.at(path);
	work.scopeMilliseconds += elapsedMilliseconds(stageStarted);
	stageStarted = Clock::now();
	dependencies();
	if (!prior || rules != prior->at("rules") || !sourceProjectionsEqual(sources, prior->at("sources")))
		for (const Unit &unit : units)
			dirty.insert(unit.path);
	size_t vertexCount = units.size();
	std::vector<size_t> oldVertices;
	if (prior)
		for (const Json &unit : prior->at("units")) {
			const std::string &path = unit.at(Path).get_ref<const std::string &>();
			auto current = indices.find(path);
			oldVertices.push_back(current == indices.end() ? vertexCount++ : current->second);
		}
	std::vector<std::vector<size_t>> reverse(vertexCount);
	for (size_t index = 0; index < units.size(); index++)
		for (size_t dependency : units[index].edges)
			reverse.at(dependency).push_back(index);
	if (prior)
		for (size_t index = 0; index < oldVertices.size(); index++)
			for (const Json &dependency : prior->at("units").at(index).at(Edges))
				reverse.at(oldVertices.at(dependency.get<size_t>())).push_back(oldVertices[index]);
	std::vector<bool> invalidated(vertexCount);
	std::stack<size_t> pending;
	for (size_t index = 0; index < vertexCount; index++)
		if (index >= units.size() || dirty.contains(units[index].path)) {
			invalidated[index] = true;
			pending.push(index);
		}
	const std::vector<bool> inputsChanged = invalidated;
	while (!pending.empty()) {
		size_t changed = pending.top();
		pending.pop();
		for (size_t caller : reverse[changed])
			if (!invalidated[caller]) {
				invalidated[caller] = true;
				pending.push(caller);
			}
	}
	for (size_t index = 0; index < units.size(); index++)
		if (invalidated[index]) {
			dirty.insert(units[index].path);
			work.unitsPotentiallyInvalidated++;
		}
	preparePropagation(std::move(reverse), inputsChanged);
	work.dependencyMilliseconds += elapsedMilliseconds(stageStarted);
	prepared = true;
}

void IncrementalClassification::dependencies() {
	// Index each possible path by its rarest required literal among readers
	// whose queries changed. A path missing from every such reader needs no edge.
	std::unordered_map<std::string_view, size_t> frequencies;
	bool hasNewQueries = false;
	for (const Unit &unit : units)
		if (unit.hasCandidates && !unit.references.empty() && (!unit.old || unit.old->at(Queries) != unit.queries)) {
			hasNewQueries = true;
			for (const Json &literal : data(unit.queries).at("literals"))
				frequencies[literal.get_ref<const std::string &>()]++;
		}
	std::unordered_map<std::string_view, std::vector<size_t>> targetsByLiteral;
	std::vector<size_t> wildcardTargets;
	if (hasNewQueries)
		for (size_t target = 0; target < units.size(); target++)
			for (const Json &signature : data(units[target].provides)) {
				const Json &path = signature.at(0);
				if (path.empty()) {
					wildcardTargets.push_back(target);
					continue;
				}
				std::string_view anchor;
				size_t minimum = units.size() + 1;
				for (const Json &literal : path) {
					const std::string &word = literal.get_ref<const std::string &>();
					auto frequency = frequencies.find(word);
					if (frequency == frequencies.end()) {
						minimum = 0;
						break;
					}
					if (frequency->second < minimum) {
						minimum = frequency->second;
						anchor = word;
					}
				}
				if (minimum != 0)
					targetsByLiteral[anchor].push_back(target);
			}
	std::vector<size_t> changedTargets;
	for (size_t index = 0; index < units.size(); index++) {
		const Unit &unit = units[index];
		if (!unit.old || unit.old->at(Input) != unit.input)
			dirty.insert(unit.path);
		if (!unit.old || unit.old->at(Input) != unit.input || unit.old->at(Provides) != unit.provides)
			changedTargets.push_back(index);
	}
	for (Unit &unit : units) {
		if (!unit.hasCandidates)
			continue;
		if (unit.old && unit.old->at(Queries) == unit.queries) {
			for (const Json &edge : unit.old->at(Edges)) {
				const std::string &path = prior->at("units").at(edge.get<size_t>()).at(Path).get_ref<const std::string &>();
				if (auto target = indices.find(path); target != indices.end())
					unit.edges.push_back(target->second);
			}
			for (size_t target : changedTargets) {
				std::erase(unit.edges, target);
				if (couldRead(unit, units[target]))
					unit.edges.push_back(target);
			}
			work.dependencyUnitsReused++;
		} else if (!unit.references.empty()) {
			std::vector<size_t> candidates = wildcardTargets;
			for (const Json &literal : data(unit.queries).at("literals"))
				if (auto found = targetsByLiteral.find(literal.get_ref<const std::string &>()); found != targetsByLiteral.end())
					candidates.insert(candidates.end(), found->second.begin(), found->second.end());
			std::sort(candidates.begin(), candidates.end());
			candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
			for (size_t target : candidates)
				if (couldRead(unit, units[target]))
					unit.edges.push_back(target);
		}
		for (const Section *ancestor = unit.section->parent; ancestor; ancestor = ancestor->parent)
			if (auto environment = environmentUnits.find(ancestor); environment != environmentUnits.end())
				unit.edges.push_back(environment->second);
		if (unit.environment)
			continue;
		for (const Section *ancestor = unit.section->parent; ancestor; ancestor = ancestor->parent)
			if (auto parent = sectionUnits.find(ancestor); parent != sectionUnits.end()) {
				unit.edges.push_back(parent->second);
				if (units[parent->second].hasCandidates)
					units[parent->second].edges.push_back(indices.at(unit.path));
				break;
			}
	}
	for (Unit &unit : units) {
		std::sort(unit.edges.begin(), unit.edges.end());
		unit.edges.erase(std::unique(unit.edges.begin(), unit.edges.end()), unit.edges.end());
		work.edges += unit.edges.size();
	}
}
