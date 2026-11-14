public:
using Unit = std::pair<std::string, std::string>;
using Scope = std::shared_ptr<const VariableStorageScope>;
std::function<void(const Unit &, const Scope &, const Json &, const Json &, const Json *, const std::string &, const Json &)>
	retainRestored;
std::string productIdentity(const Unit &unit) const {
	return digest(Json::array({unit.first, unit.second, *reference(unit), *inputProof(unit)}).dump());
}
const Json &selection(const Unit &unit) const {
	return refreshed(unit.first, unit.second) ? currentSelections.at(unit) : selections.at(unit.first).at(unit.second);
}
Section *section(const Unit &unit) const { return identities.sections.at(unit.first); }
std::optional<Unit> identify(const Section *section, const InstantiationKey &key) const {
	auto identity = inferenceSummaryKey(key, identities);
	return identity ? std::optional<Unit>{{sectionPaths.at(section), *identity}} : std::nullopt;
}
bool refreshed(const std::string &path, const std::string &key) const {
	return refreshedSummaries.contains(path) && refreshedSummaries.at(path).contains(key);
}
const Json *reference(const Unit &unit) const {
	if (refreshed(unit.first, unit.second))
		return &refreshedSummaries.at(unit.first).at(unit.second);
	return summaries.contains(unit.first) && summaries.at(unit.first).contains(unit.second)
			   ? &summaries.at(unit.first).at(unit.second)
			   : nullptr;
}
const Json *inputProof(const Unit &unit) const {
	if (auto found = validatedInputs.find(unit); found != validatedInputs.end())
		return &found->second;
	return units.contains(unit.first) && units.at(unit.first).contains(unit.second)
			   ? &units.at(unit.first).at(unit.second).at("global_inputs")
			   : nullptr;
}
bool inputsUnchanged(const Unit &unit) const {
	const Json *proof = inputProof(unit);
	return proof && globalInputs.unchanged(*proof);
}
void validateInputs(const Unit &unit, Json proof) {
	validatedInputs[unit] = std::move(proof);
	runtimeValidated.erase({identities.sections.at(unit.first), unit.second});
}
bool accept(const Unit &unit, const Json &record, const Json &proof, const Json &selected) {
	if (!globalInputs.unchanged(proof) || !summaryStore.retain(record))
		return false;
	refreshedSummaries[unit.first][unit.second] = record;
	currentSelections[unit] = selected;
	validateInputs(unit, proof);
	return true;
}
void nativeInterface(const Unit &unit, bool equivalent) {
	if (equivalent)
		nativeEquivalent.insert(unit);
	else
		nativeEquivalent.erase(unit);
}
bool valueUnchanged(const Unit &unit, const Json &query) {
	const Json *record = reference(unit);
	if (!record || record->at("failed").get<bool>() || query.at(2).is_null() || query.at(3).is_null())
		return false;
	const Json *summary = summaryStore.load(*record);
	if (!summary)
		return false;
	for (const Json &observation : summary->at("pure_values")) {
		Json identity = inferenceValueFingerprints(observation.at(0), observation.at(1));
		if (identity.at(0) == query.at(2))
			return identity.at(1) == query.at(3);
	}
	return false;
}
bool nativeInputsUnchanged(const Section *section, const std::string &key) {
	if (!native)
		return false;
	const Json &graph = native->at("section_dependency_graph").at("inference_units");
	std::set<Unit> visited;
	std::stack<Unit> pending;
	pending.emplace(sectionPaths.at(section), key);
	while (!pending.empty()) {
		Unit unit = pending.top();
		pending.pop();
		if (!visited.insert(unit).second)
			continue;
		if (!graph.contains(unit.first) || !graph.at(unit.first).contains(unit.second))
			return false;
		const Json &inputs = graph.at(unit.first).at(unit.second);
		if (globalInputs.unchanged(inputs.at("global_inputs")))
			continue;
		if (!globalInputs.unchanged(inputs.at("direct_global_inputs")))
			return false;
		for (const Json &edge : inputs.at("callees")) {
			Unit child{edge.at(0).get<std::string>(), edge.at(1).get<std::string>()};
			if (!nativeEquivalent.contains(child) || !inputsUnchanged(child))
				pending.push(std::move(child));
		}
		for (const Json &query : inputs.at("values")) {
			Unit child{query.at(0).get<std::string>(), query.at(1).get<std::string>()};
			if (!inputsUnchanged(child) || !valueUnchanged(child, query))
				return false;
		}
	}
	return true;
}

private:
Json refreshedSummaries = Json::object();
std::map<Unit, Json> validatedInputs, currentSelections;
std::set<Unit> nativeEquivalent;
