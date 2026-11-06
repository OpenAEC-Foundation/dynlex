std::vector<ParseContext::IncrementalFunctionRequest> scalarArgumentIncrementalCandidates(
	const ParseContext &context, const Json &previous, const Json &observations,
	const std::map<const Section *, std::string> &sectionPaths
) {
	std::vector<ParseContext::IncrementalFunctionRequest> result;
	if (context.options.optimizationLevel || context.options.emitDebugInfo || context.options.traceExecution ||
		!compatibleObservations(previous, observations, context))
		return result;
	Json current = captureAllFunctionProjection(context);
	const Json &prior = previous.at("all_function_projection");
	if (current.at("declarations") != prior.at("declarations") || current.at("sources") != prior.at("sources") ||
		current.at("inactive") != prior.at("inactive"))
		return result;
	const Json &summaries = previous.at("inference_summaries");
	std::map<std::string, size_t> generatedFunctionCounts;
	for (const auto &[_, owner] : previous.at("function_sections").items())
		generatedFunctionCounts[owner.get<std::string>()]++;
	std::unordered_map<const Section *, bool> globalFree;
	for (const auto &[section, _] : sectionPaths)
		globalFree[section] = hasNoGlobalAccess(section);
	std::unordered_map<Section *, bool> globalFreeClosure;
	std::unordered_set<Section *> visiting;
	for (const auto &[section, path] : sectionPaths) {
		if (section->type != SectionType::Function || section->isFlex || section->isConversion || !globalFree.at(section) ||
			!current.at("bodies").contains(path) || current.at("bodies").at(path) == prior.at("bodies").at(path) ||
			!summaries.contains(path) || summaries.at(path).empty() || section->patternDefinitions.size() != 1 ||
			section->patternDefinitions.front()->indexedPaths.size() != 1)
			continue;
		if (!functionDependenciesUnchanged(const_cast<Section *>(section), globalFree, globalFreeClosure, visiting))
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
				DataType type{
					static_cast<DataType::Kind>(encoded.at(0).get<int>()), encoded.at(1).get<int>(), encoded.at(2).get<int>()
				};
				if (type.pointerDepth != 0)
					break;
				request.argumentTypes.push_back(type);
			}
			InstantiationKey key;
			key.argumentTypes = request.argumentTypes;
			if (request.argumentTypes.size() != summary.at("argument_types").size() ||
				request.parameterNames.size() != request.argumentTypes.size() || keyText != *inferenceSummaryKey(key))
				break;
			requests.push_back(std::move(request));
		}
		if (requests.size() == summaries.at(path).size())
			result.insert(result.end(), requests.begin(), requests.end());
	}
	return result;
}

std::unordered_set<const Section *> stableIncrementalReplacements(
	const std::vector<ParseContext::IncrementalFunctionRequest> &requests,
	const std::map<const Section *, std::string> &sectionPaths, const Json &currentSummaries, const Json &priorSummaries,
	std::vector<ParseContext::IncrementalFunctionRequest> &replacements
) {
	std::map<Section *, std::vector<ParseContext::IncrementalFunctionRequest>> grouped;
	for (const auto &request : requests)
		grouped[request.section].push_back(request);
	std::unordered_map<const Section *, bool> globalFree;
	for (const auto &[section, _] : sectionPaths)
		globalFree[section] = hasNoGlobalAccess(section);
	std::unordered_map<Section *, bool> globalFreeClosure;
	std::unordered_set<Section *> visiting;
	std::unordered_set<const Section *> stable;
	for (const auto &[section, variants] : grouped) {
		const std::string &path = sectionPaths.at(section);
		bool same = currentSummaries.contains(path);
		for (const auto &request : variants) {
			InstantiationKey key;
			key.argumentTypes = request.argumentTypes;
			const std::string keyText = *inferenceSummaryKey(key);
			if (!same || !currentSummaries.at(path).contains(keyText) ||
				currentSummaries.at(path).at(keyText) != priorSummaries.at(path).at(keyText)) {
				same = false;
				break;
			}
		}
		if (!same || !functionDependenciesUnchanged(section, globalFree, globalFreeClosure, visiting))
			continue;
		stable.insert(section);
		replacements.insert(replacements.end(), variants.begin(), variants.end());
	}
	return stable;
}
