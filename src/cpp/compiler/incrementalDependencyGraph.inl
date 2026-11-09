Json captureSectionDependencyGraph(
	const std::map<const Section *, std::string> &paths, const Json *previous = nullptr,
	const std::unordered_set<const Section *> &stable = {}, size_t *reused = nullptr
) {
	Json forward = Json::object();
	Json reverse = Json::object();
	Json instantiations = Json::object();
	Json unknown = Json::array();
	ClassIdentityPaths classPaths = classIdentityPaths(paths);
	std::unordered_set<std::string> priorUnknown;
	if (previous)
		for (const Json &path : previous->at("unknown"))
			priorUnknown.insert(path.get<std::string>());
	for (const auto &[section, path] : paths) {
		if (previous && stable.contains(section) && previous->at("forward").contains(path) &&
			previous->at("instantiations").contains(path) &&
			previous->at("instantiations").at(path).size() == section->instantiations.size() &&
			std::ranges::all_of(section->instantiations, [](const auto &entry) {
			return entry.second.inferenceReusedFromCache;
		})) {
			forward[path] = previous->at("forward").at(path);
			instantiations[path] = previous->at("instantiations").at(path);
			if (priorUnknown.contains(path))
				unknown.push_back(path);
			if (reused)
				(*reused)++;
			continue;
		}
		std::set<std::string> direct;
		std::vector<Section *> matched;
		collectMatchedSectionDependencies(section, matched);
		for (Section *dependency : matched)
			direct.insert(paths.at(dependency));
		Json variants = Json::object();
		bool incomplete = false;
		for (const auto &[key, instantiation] : section->instantiations) {
			std::optional<std::string> keyText = inferenceSummaryKey(key, classPaths);
			Json selected = Json::array();
			if (instantiation.inferenceReusedFromCache) {
				if (!keyText || !previous || !previous->at("instantiations").contains(path) ||
					!previous->at("instantiations").at(path).contains(*keyText)) {
					incomplete = true;
					continue;
				}
				selected = previous->at("instantiations").at(path).at(*keyText);
			} else {
				std::vector<Section *> inferred;
				collectInferredSectionDependencies(instantiation.body.get(), inferred);
				std::set<std::string> selectedPaths;
				for (Section *dependency : inferred)
					selectedPaths.insert(paths.at(dependency));
				selected = selectedPaths;
			}
			for (const Json &dependency : selected)
				direct.insert(dependency.get<std::string>());
			if (keyText)
				variants[*keyText] = std::move(selected);
		}
		if (incomplete)
			unknown.push_back(path);
		forward[path] = direct;
		instantiations[path] = std::move(variants);
	}
	std::map<std::string, std::set<std::string>> callers;
	for (const auto &[path, dependencies] : forward.items())
		for (const Json &dependency : dependencies)
			callers[dependency.get<std::string>()].insert(path);
	for (const auto &[path, dependents] : callers)
		reverse[path] = dependents;
	return Json{
		{"forward", std::move(forward)},
		{"reverse", std::move(reverse)},
		{"instantiations", std::move(instantiations)},
		{"unknown", std::move(unknown)}
	};
}
