// Inference summaries require portable argument values. Dependency edges do
// not: variants without such keys retain their complete union under this key.
constexpr const char *aggregateGraphVariant = "*";

struct RetainedGraphVariants {
	std::map<std::string, std::set<std::string>> keysByPath;
	std::set<std::string> unkeyedPaths;
};

RetainedGraphVariants retainedGraphVariants(const IncrementalIRReuse *reuse, const Json *previous) {
	RetainedGraphVariants result;
	if (!reuse)
		return result;
	requireCompilerInvariant(previous != nullptr, "reused IR has no previous dependency graph");
	const Json &owners = previous->at("function_sections");
	const Json &keys = previous->at("function_instantiations");
	const Json &callables = previous->at("callable_functions");
	for (const std::string &name : reuse->reusedFunctionNames()) {
		std::string path = owners.at(name).get<std::string>();
		if (keys.contains(name))
			result.keysByPath[path].insert(keys.at(name).get<std::string>());
		else if (!callables.contains(name))
			result.unkeyedPaths.insert(std::move(path));
	}
	return result;
}

Json captureSectionDependencyGraph(
	const std::map<const Section *, std::string> &paths, const IncrementalInferenceReads &inferenceReads,
	const Json &probeLookups, Json *previous = nullptr,
	const std::unordered_set<const Section *> &stable = {}, const RetainedGraphVariants &retained = {},
	size_t *reused = nullptr, size_t *reverseUpdated = nullptr
) {
	const bool hadPrevious = previous != nullptr;
	Json graph = hadPrevious
					 ? std::move(*previous)
					 : Json{
						   {"forward", Json::object()}, {"reverse", Json::object()}, {"instantiations", Json::object()},
						   {"callable_lookups", Json::object()}, {"inference_reads", Json::object()}
					   };
	Json &forward = graph.at("forward");
	Json &reverse = graph.at("reverse");
	Json &instantiations = graph.at("instantiations");
	const Json &priorLookups = graph.at("callable_lookups");
	const Json &priorReads = graph.at("inference_reads");
	Json sectionReads = Json::object();
	std::map<std::string, std::set<std::string>> lookupOwners;
	std::unordered_map<std::string, std::vector<std::string_view>> priorLookupsByOwner;
	for (const auto &[key, lookup] : priorLookups.items())
		for (const Json &owner : lookup.at("owners"))
			priorLookupsByOwner[owner.get<std::string>()].push_back(key);
	auto retainLookups = [&](const std::string &path, IncrementalInferenceReads::Keys &keys) {
		if (auto found = priorLookupsByOwner.find(path); found != priorLookupsByOwner.end())
			for (std::string_view signature : found->second)
				keys.emplace(signature);
	};
	ClassIdentityPaths classPaths = classIdentityPaths(paths);
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
		std::set<std::string> retainedKeys;
		if (auto keys = retained.keysByPath.find(path); keys != retained.keysByPath.end())
			retainedKeys = keys->second;
		if (retained.unkeyedPaths.contains(path))
			retainedKeys.insert(aggregateGraphVariant);
		IncrementalInferenceReads::Keys sectionLookups = inferenceReads.readsOf(path);
		IncrementalInferenceReads::Keys reads = inferenceReads.dependenciesOf(path);
		bool reuseEntry = hadPrevious && stable.contains(section) && forward.contains(path) &&
						  instantiations.contains(path) && sectionLookups.empty() && reads.empty();
		std::set<std::string> currentKeys = retainedKeys;
		if (reuseEntry)
			for (const auto &[key, instantiation] : section->instantiations) {
				std::optional<std::string> identity = inferenceSummaryKey(key, classPaths);
				if (!instantiation.inferenceReusedFromCache || !identity || !instantiations.at(path).contains(*identity)) {
					reuseEntry = false;
					break;
				}
				currentKeys.insert(*identity);
			}
		if (reuseEntry)
			reuseEntry = currentKeys.size() == instantiations.at(path).size() &&
						 std::ranges::all_of(currentKeys, [&](const std::string &key) {
				return instantiations.at(path).contains(key);
			});
		if (reuseEntry) {
			retainLookups(path, sectionLookups);
			for (const std::string &key : sectionLookups)
				lookupOwners[key].insert(path);
			if (priorReads.contains(path))
				sectionReads[path] = priorReads.at(path);
			if (reused)
				(*reused)++;
			continue;
		}
		std::set<std::string> direct;
		std::vector<Section *> matched;
		collectMatchedSectionDependencies(section, matched);
		if (!section->parent)
			InferredSectionDependencies(matched).collectExecution(*section);
		for (Section *dependency : matched)
			direct.insert(paths.at(dependency));
		Json variants = Json::object();
		std::set<std::string> aggregateDependencies;
		bool hasAggregateVariant = retained.unkeyedPaths.contains(path);
		bool retainPriorLookups = !retainedKeys.empty();
		for (const auto &[key, instantiation] : section->instantiations) {
			std::optional<std::string> keyText = inferenceSummaryKey(key, classPaths);
			Json selected = Json::array();
			if (instantiation.inferenceReusedFromCache) {
				retainPriorLookups = true;
				requireCompilerInvariant(
					keyText && hadPrevious && instantiations.contains(path) && instantiations.at(path).contains(*keyText),
					"reused inference has no saved dependency variant"
				);
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
			else {
				hasAggregateVariant = true;
				for (const Json &dependency : selected)
					aggregateDependencies.insert(dependency.get<std::string>());
			}
		}
		for (const std::string &key : retainedKeys) {
			if (variants.contains(key))
				continue;
			requireCompilerInvariant(
				hadPrevious && instantiations.contains(path) && instantiations.at(path).contains(key),
				"reused IR has no saved dependency variant"
			);
			const Json &selected = instantiations.at(path).at(key);
			for (const Json &dependency : selected)
				direct.insert(dependency.get<std::string>());
			if (key == aggregateGraphVariant)
				for (const Json &dependency : selected)
					aggregateDependencies.insert(dependency.get<std::string>());
			else
				variants[key] = selected;
		}
		if (hasAggregateVariant)
			variants[aggregateGraphVariant] = aggregateDependencies;
		if (retainPriorLookups) {
			retainLookups(path, sectionLookups);
			if (priorReads.contains(path))
				for (const Json &dependency : priorReads.at(path))
					reads.insert(dependency.get_ref<const std::string &>());
		}
		if (!reads.empty()) {
			direct.insert(reads.begin(), reads.end());
			sectionReads[path] = std::move(reads);
		}
		for (const std::string &key : sectionLookups) {
			lookupOwners[key].insert(path);
			const Json &candidates = inferenceReads.observation(key, &priorLookups).at("candidates");
			if (candidates.size() == 1)
				direct.insert(candidates.at(0).at(0).get<std::string>());
		}
		std::set<std::string> before = forward.contains(path) ? dependencySet(forward.at(path)) : std::set<std::string>{};
		if (before != direct)
			changes.push_back({path, std::move(before), direct});
		forward[path] = direct;
		instantiations[path] = std::move(variants);
	}
	for (const Json &keys : probeLookups)
		for (const Json &key : keys)
			lookupOwners.try_emplace(key.get<std::string>());
	Json callableLookups = Json::object();
	for (auto &[key, owners] : lookupOwners) {
		Json lookup = inferenceReads.observation(key, &priorLookups);
		lookup["owners"] = std::move(owners);
		callableLookups[key] = std::move(lookup);
	}
	graph["callable_lookups"] = std::move(callableLookups);
	graph["inference_reads"] = std::move(sectionReads);
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
	return graph;
}
