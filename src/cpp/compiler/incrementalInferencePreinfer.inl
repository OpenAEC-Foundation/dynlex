struct IncrementalPreinferPlan {
	std::vector<ParseContext::IncrementalFunctionRequest> requests;
	std::unordered_map<const Section *, bool> bodyUnchanged;
	size_t dependencyGraphSectionsChecked = 0;
};

std::set<Section *> matchedSectionDependencies(Section *section) {
	std::vector<Section *> matched;
	collectMatchedSectionDependencies(section, matched);
	std::set<Section *> dependencies(matched.begin(), matched.end());
	dependencies.erase(section);
	return dependencies;
}

IncrementalPreinferPlan incrementalInferenceCandidates(
	const ParseContext &context, const Json &previous, const Json &observations,
	const std::map<const Section *, std::string> &sectionPaths, const Json &current,
	const IncrementalInferenceReads::Keys &changedLookups
) {
	IncrementalPreinferPlan result;
	if (context.options.emitDebugInfo || context.options.traceExecution ||
		!compatibleObservations(previous, observations, context))
		return result;
	const Json &prior = previous.at("all_function_projection");
	bool fullyCompatible = compatibleFunctionProjection(prior, current);
	if (!fullyCompatible && !compatibleGlobalFreeProjection(prior, current))
		return result;
	for (const auto &[section, path] : sectionPaths)
		result.bodyUnchanged[section] = unchangedSectionBody(*section, path, prior, current);
	const Json &summaries = previous.at("inference_summaries");
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	std::map<std::string, size_t> generatedFunctionCounts;
	for (const auto &[name, owner] : previous.at("function_sections").items())
		if (!previous.at("callable_functions").contains(name))
			generatedFunctionCounts[owner.get<std::string>()]++;
	std::unordered_map<const Section *, bool> globalFree;
	for (const auto &[section, _] : sectionPaths)
		globalFree[section] = (fullyCompatible || section != context.mainSection) && hasNoGlobalAccess(section);
	std::optional<std::unordered_set<std::string>> graphDirty;
	if (fullyCompatible && previous.contains("section_dependency_graph")) {
		std::map<std::string, Section *> sections;
		for (const auto &[section, path] : sectionPaths)
			sections.emplace(path, const_cast<Section *>(section));
		graphDirty = dirtySectionsFromSavedGraph(
			previous.at("section_dependency_graph"), sections, globalFree, {},
			prior.at("final_matcher_tree") != current.at("final_matcher_tree"), changedLookups
		);
	}
	std::unordered_map<Section *, bool> globalFreeClosure;
	for (const auto &[section, path] : sectionPaths) {
		if (section->type != SectionType::Function || section->isFlex || !globalFree.at(section) || !summaries.contains(path) ||
			summaries.at(path).empty() || section->patternDefinitions.size() != 1 ||
			section->patternDefinitions.front()->indexedPaths.size() != 1 ||
			!prior.at("function_declarations").contains(path) || !current.at("function_declarations").contains(path) ||
			prior.at("function_declarations").at(path) != current.at("function_declarations").at(path))
			continue;
		if (graphDirty) {
			result.dependencyGraphSectionsChecked++;
			if (graphDirty->contains(path))
				continue;
		} else if (!functionDependenciesUnchanged(const_cast<Section *>(section), globalFree, globalFreeClosure))
			continue;
		if (generatedFunctionCounts[path] != summaries.at(path).size())
			continue;
		std::vector<std::string> parameterNames;
		bool argumentPack = false;
		for (const PatternElement &element : section->patternDefinitions.front()->indexedPaths.front()) {
			if (element.type == PatternElement::Variable) {
				if (element.isArgumentPack)
					argumentPack = true;
				else
					parameterNames.push_back(element.text);
			}
		}
		if (argumentPack)
			continue;
		std::vector<ParseContext::IncrementalFunctionRequest> requests;
		for (const auto &[keyText, summary] : summaries.at(path).items()) {
			if (summary.at("needs_global_types").get<bool>())
				break;
			ParseContext::IncrementalFunctionRequest request{const_cast<Section *>(section), parameterNames, {}};
			for (const Json &encoded : summary.at("argument_types")) {
				auto type = decodeInferenceTypeIdentity(encoded, classPaths);
				if (!type)
					break;
				request.argumentTypes.push_back(std::move(*type));
			}
			InstantiationKey key;
			key.argumentTypes = request.argumentTypes;
			if (request.argumentTypes.size() != summary.at("argument_types").size() ||
				request.parameterNames.size() != request.argumentTypes.size() ||
				keyText != *inferenceSummaryKey(key, classPaths))
				break;
			requests.push_back(std::move(request));
		}
		if (requests.size() == summaries.at(path).size())
			result.requests.insert(result.requests.end(), requests.begin(), requests.end());
	}
	std::map<Section *, std::vector<ParseContext::IncrementalFunctionRequest>> grouped;
	for (const auto &request : result.requests)
		grouped[request.section].push_back(request);
	result.requests.clear();
	std::unordered_set<Section *> visited;
	std::function<void(Section *)> order = [&](Section *section) {
		if (!visited.insert(section).second)
			return;
		for (Section *dependency : matchedSectionDependencies(section)) {
			if (grouped.contains(dependency))
				order(dependency);
		}
		const auto &requests = grouped.at(section);
		result.requests.insert(result.requests.end(), requests.begin(), requests.end());
	};
	for (const auto &[section, _] : grouped)
		order(section);
	return result;
}

std::unordered_set<const Section *> stableIncrementalReplacements(
	const std::vector<ParseContext::IncrementalFunctionRequest> &requests,
	const std::map<const Section *, std::string> &sectionPaths, const Json &currentSummaries, const Json &priorSummaries,
	std::vector<ParseContext::IncrementalFunctionRequest> &replacements
) {
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	std::map<Section *, std::vector<ParseContext::IncrementalFunctionRequest>> grouped;
	for (const auto &request : requests)
		grouped[request.section].push_back(request);
	std::unordered_map<const Section *, bool> globalFree;
	for (const auto &[section, _] : sectionPaths)
		globalFree[section] = hasNoGlobalAccess(section);
	std::unordered_map<Section *, bool> globalFreeClosure;
	std::unordered_set<const Section *> stable;
	for (const auto &[section, variants] : grouped) {
		const std::string &path = sectionPaths.at(section);
		bool same = currentSummaries.contains(path);
		for (const auto &request : variants) {
			InstantiationKey key;
			key.argumentTypes = request.argumentTypes;
			const std::string keyText = *inferenceSummaryKey(key, classPaths);
			if (!same || !currentSummaries.at(path).contains(keyText) ||
				!priorSummaries.at(path).at(keyText).at("pure_values_complete").get<bool>() ||
				currentSummaries.at(path).at(keyText) != priorSummaries.at(path).at(keyText)) {
				same = false;
				break;
			}
		}
		if (!same || !functionDependenciesUnchanged(section, globalFree, globalFreeClosure))
			continue;
		stable.insert(section);
		replacements.insert(replacements.end(), variants.begin(), variants.end());
	}
	return stable;
}
