Json captureSectionDependencyGraph(
	const std::map<const Section *, std::string> &paths, Json *previous = nullptr,
	const std::unordered_set<const Section *> &stable = {}, size_t *reused = nullptr, size_t *reverseUpdated = nullptr
) {
	const bool hadPrevious = previous != nullptr;
	Json graph = hadPrevious ? std::move(*previous)
							 : Json{
								   {"forward", Json::object()},
								   {"reverse", Json::object()},
								   {"instantiations", Json::object()},
								   {"unknown", Json::array()}
							   };
	Json &forward = graph.at("forward");
	Json &reverse = graph.at("reverse");
	Json &instantiations = graph.at("instantiations");
	ClassIdentityPaths classPaths = classIdentityPaths(paths);
	std::unordered_set<std::string> priorUnknown;
	if (hadPrevious)
		for (const Json &path : graph.at("unknown"))
			priorUnknown.insert(path.get<std::string>());
	Json unknown = Json::array();
	std::unordered_set<std::string> currentPaths;
	struct DependencyChange {
		std::string caller;
		std::set<std::string> before;
		std::set<std::string> after;
	};
	std::vector<DependencyChange> changes;
	auto dependencySet = [](const Json &entries) {
		std::set<std::string> result;
		for (const Json &entry : entries)
			result.insert(entry.get<std::string>());
		return result;
	};
	for (const auto &[section, path] : paths) {
		currentPaths.insert(path);
		bool reuseEntry = hadPrevious && stable.contains(section) && forward.contains(path) && instantiations.contains(path) &&
						  instantiations.at(path).size() == section->instantiations.size();
		if (reuseEntry)
			for (const auto &[key, instantiation] : section->instantiations) {
				std::optional<std::string> identity = inferenceSummaryKey(key, classPaths);
				if (!instantiation.inferenceReusedFromCache || !identity || !instantiations.at(path).contains(*identity)) {
					reuseEntry = false;
					break;
				}
			}
		if (reuseEntry) {
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
				if (!keyText || !hadPrevious || !instantiations.contains(path) || !instantiations.at(path).contains(*keyText)) {
					incomplete = true;
					continue;
				}
				selected = instantiations.at(path).at(*keyText);
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
		std::set<std::string> before = forward.contains(path) ? dependencySet(forward.at(path)) : std::set<std::string>{};
		if (before != direct)
			changes.push_back({path, std::move(before), direct});
		forward[path] = direct;
		instantiations[path] = std::move(variants);
	}
	if (hadPrevious) {
		for (auto entry = forward.begin(); entry != forward.end();) {
			if (currentPaths.contains(entry.key())) {
				++entry;
				continue;
			}
			std::string path = entry.key();
			changes.push_back({path, dependencySet(entry.value()), {}});
			entry = forward.erase(entry);
			instantiations.erase(path);
		}
		std::map<std::string, std::set<std::string>> changed;
		auto callersFor = [&](const std::string &dependency) -> std::set<std::string> & {
			auto [found, inserted] = changed.try_emplace(dependency);
			if (inserted && reverse.contains(dependency))
				found->second = dependencySet(reverse.at(dependency));
			return found->second;
		};
		for (const DependencyChange &change : changes) {
			for (const std::string &dependency : change.before)
				if (!change.after.contains(dependency))
					callersFor(dependency).erase(change.caller);
			for (const std::string &dependency : change.after)
				if (!change.before.contains(dependency))
					callersFor(dependency).insert(change.caller);
		}
		for (const auto &[path, callers] : changed) {
			if (callers.empty())
				reverse.erase(path);
			else
				reverse[path] = callers;
			if (reverseUpdated)
				(*reverseUpdated)++;
		}
	} else {
		std::map<std::string, std::set<std::string>> callers;
		for (const auto &[path, dependencies] : forward.items())
			for (const Json &dependency : dependencies)
				callers[dependency.get<std::string>()].insert(path);
		for (const auto &[path, dependents] : callers)
			reverse[path] = dependents;
	}
	graph["unknown"] = std::move(unknown);
	return graph;
}
