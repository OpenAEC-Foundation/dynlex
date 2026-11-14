// Environments depend on declaration-call inputs and potentially competing
// patterns, including patterns which do not themselves declare names.
void IncrementalClassification::prepareEnvironments() {
	const auto &sections = counts.sections();
	std::unordered_set<const Section *> needed;
	std::vector<size_t> declaring;
	for (size_t index = 0; index < units.size(); index++) {
		const Unit &unit = units[index];
		if (unit.section->dataSection && !unit.section->dataSection->declarationParameter.empty())
			declaring.push_back(index);
		if (unit.hasCandidates)
			for (const Section *scope = unit.section->parent; scope; scope = scope->parent)
				needed.insert(scope);
	}
	for (const auto &[path, section] : sections) {
		if (!needed.contains(section))
			continue;
		Unit environment;
		environment.path = "env:" + path;
		environment.section = section;
		environment.environment = true;
		environment.hasCandidates = true;
		if (auto old = oldUnits.find(environment.path); old != oldUnits.end())
			environment.old = old->second;
		Json inputs = Json::array({globalResolutionIdentity(*section), zeroWidthFunctions});
		if (!declaring.empty())
			for (PatternReference *reference : section->patternReferences) {
				Unit query;
				query.references.push_back(reference);
				Json literals = queried(query);
				if (std::ranges::none_of(declaring, [&](size_t target) {
					return couldReadQueries(literals, query, units[target]);
				}))
					continue;
				environment.references.push_back(reference);
				inputs.push_back(
					Json::array(
						{reference->range().line->sourceFile->uri, reference->pattern.text,
						 std::string(reference->range().subString), static_cast<int>(reference->patternType),
						 static_cast<int>(reference->purpose)}
					)
				);
			}
		environment.input = digest(inputs.dump());
		if (environment.old && environment.old->at(Input) == environment.input) {
			environment.queries = environment.old->at(Queries).get<std::string>();
			work.queryUnitsReused++;
		} else {
			environment.queries = save(queried(environment));
			work.queryUnitsBuilt++;
		}
		environment.provides = save(Json::array());
		indices.emplace(environment.path, units.size());
		environmentUnits.emplace(section, units.size());
		units.push_back(std::move(environment));
	}
}
