Json functionBodyDigests(const ParseContext &context, const std::map<std::string, Section *> &sections) {
	std::map<const Section *, std::string> bodies;
	for (const auto &[_, section] : sections)
		if ((section->type == SectionType::Function && !section->patternDefinitions.empty()) ||
			section->type == SectionType::Retain || section->type == SectionType::Release)
			bodies.emplace(section, "");
	for (const CodeLine *line : context.codeLines)
		for (const Section *section = line->section; section; section = section->parent)
			if (auto body = bodies.find(section); body != bodies.end()) {
				body->second.append(line->rightTrimmedText);
				body->second += '\n';
			}
	Json result = Json::object();
	for (const auto &[path, section] : sections)
		if (auto body = bodies.find(section); body != bodies.end())
			result[path] = digest(body->second);
	return result;
}

Json captureAllFunctionProjection(
	const ParseContext &context, const Json &observations, const std::map<std::string, Section *> &sections,
	const Json *previous = nullptr, SourceProjectionWork *projectionWork = nullptr
) {
	auto phaseStart = Clock::now();
	const Json *prior =
		previous && previous->contains("all_function_projection") ? &previous->at("all_function_projection") : nullptr;
	Json selected = Json::object();
	Json declarations = Json::object();
	Json matches = Json::object();
	std::map<const Section *, std::string> paths;
	std::unordered_set<const Section *> reusable;
	std::unordered_set<const Section *> selectedSections;
	for (const auto &[path, section] : sections) {
		paths.emplace(section, path);
		if (section->type == SectionType::Function && !section->patternDefinitions.empty()) {
			if (!containsNestedDefinition(*section)) {
				selected[path] = sectionIdentity(*section);
				selectedSections.insert(section);
			}
		}
		if (reusableCodegenSection(*section)) {
			reusable.insert(section);
			declarations[path] = sectionDeclaration(*section);
		}
	}
	if (projectionWork)
		projectionWork->setupMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	Json bodyDigests = functionBodyDigests(context, sections);
	if (projectionWork)
		projectionWork->bodyMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	Json projection{
		{"inactive", std::move(selected)},
		{"declarations", declarationDigest(sections)},
		{"sources", sourceProjection(
						context, observations, selectedSections, false, false, false, prior ? &prior->at("sources") : nullptr,
						projectionWork
					)}
	};
	if (projectionWork)
		projectionWork->inactiveMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	projection["bodies"] = std::move(bodyDigests);
	projection["function_declarations"] = std::move(declarations);
	projection["ambient_declarations"] = declarationDigest(sections, reusable);
	if (projectionWork)
		projectionWork->declarationMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	projection["ambient_sources"] = sourceProjection(
		context, observations, reusable, true, true, false, prior ? &prior->at("ambient_sources") : nullptr, projectionWork
	);
	projection["ambient_nonexecution_sources"] = sourceProjection(
		context, observations, reusable, true, true, true, prior ? &prior->at("ambient_nonexecution_sources") : nullptr,
		projectionWork
	);
	if (projectionWork)
		projectionWork->ambientMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	bool stableMatchingEnvironment = prior && projection.at("declarations") == prior->at("declarations") &&
									 sourceProjectionsEqual(projection.at("sources"), prior->at("sources")) &&
									 projection.at("inactive") == prior->at("inactive");
	bool stableGlobalFreeEnvironment = prior && projection.at("declarations") == prior->at("declarations") &&
									   compatibleGlobalFreeProjection(*prior, projection);
	if (projectionWork)
		projectionWork->compatibilityMilliseconds += elapsedMilliseconds(phaseStart);
	for (const Section *section : reusable) {
		const std::string &path = paths.at(section);
		if ((stableMatchingEnvironment || (stableGlobalFreeEnvironment && hasNoGlobalAccess(section))) &&
			prior->at("bodies").contains(path) && prior->at("bodies").at(path) == projection.at("bodies").at(path) &&
			prior->at("function_declarations").at(path) == projection.at("function_declarations").at(path))
			matches[path] = prior->at("matches").at(path);
		else
			matches[path] = resolvedFunctionMatches(section, paths);
	}
	if (projectionWork)
		projectionWork->matchMilliseconds += elapsedMilliseconds(phaseStart);
	projection["matches"] = std::move(matches);
	return projection;
}
