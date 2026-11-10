Json functionBodyDigests(const ParseContext &context, const std::map<std::string, Section *> &sections) {
	std::map<const Section *, std::string> bodies;
	for (const auto &[_, section] : sections)
		if ((section->type == SectionType::Function && !section->patternDefinitions.empty()) ||
			section->type == SectionType::Retain || section->type == SectionType::Release)
			bodies.emplace(section, "");
	for (const CodeLine *line : context.codeLines) {
		if (line->rightTrimmedText.empty())
			continue;
		for (const Section *section = line->section; section; section = section->parent)
			if (auto body = bodies.find(section); body != bodies.end()) {
				body->second.append(line->rightTrimmedText);
				body->second += '\n';
			}
	}
	Json result = Json::object();
	for (const auto &[path, section] : sections)
		if (auto body = bodies.find(section); body != bodies.end())
			result[path] = digest(body->second);
	return result;
}

Json globalResolutionIdentity(const Section &root) {
	auto location = [](const Range &range) {
		requireCompilerInvariant(range.line && range.line->sourceFile, "global declaration has no source location");
		return Json::array({range.line->sourceFile->uri, range.line->sourceFileLineIndex, range.start()});
	};
	Json explicitDeclarations = Json::object();
	for (const auto &[name, range] : root.explicitVariableDeclarations)
		explicitDeclarations[name] = location(range);
	Json variableDefinitions = Json::object();
	for (const auto &[name, definition] : root.variableDefinitions) {
		requireCompilerInvariant(definition != nullptr, "global variable definition is null");
		variableDefinitions[name] = location(definition->range);
	}
	std::vector<std::string> dataDeclarations(root.dataVariableDeclarations.begin(), root.dataVariableDeclarations.end());
	std::ranges::sort(dataDeclarations);
	return Json::array({std::move(explicitDeclarations), std::move(variableDefinitions), dataDeclarations});
}

bool containsScopeRuleSection(const Section &section) {
	for (const Section *child : section.children)
		if (child->type == SectionType::ScopeRules || containsScopeRuleSection(*child))
			return true;
	return false;
}

Json scopeRuleBodyPaths(const std::map<std::string, Section *> &sections) {
	Json paths = Json::object();
	for (const auto &[path, section] : sections)
		if (section->type == SectionType::Function && !section->patternDefinitions.empty() &&
			!containsNestedDefinition(*section) && containsScopeRuleSection(*section))
			paths[path] = true;
	return paths;
}

Json captureAllFunctionProjection(
	const ParseContext &context, const Json &observations, const std::map<std::string, Section *> &sections,
	IncrementalMatcherCache &matcherCache, std::string_view initialMatcherTree, const Json &initialMatcherRoots,
	const Json *previous, SourceProjectionWork *projectionWork, bool compatibleSyntax
) {
	auto phaseStart = Clock::now();
	const Json *prior =
		previous && previous->contains("all_function_projection") ? &previous->at("all_function_projection") : nullptr;
	Json selected = Json::object();
	Json matches = Json::object();
	Json matchEntries = Json::object();
	std::map<const Section *, std::string> paths;
	std::unordered_set<const Section *> reusable;
	std::unordered_set<const Section *> selectedSections;
	size_t declarationSections = 0;
	for (const auto &[path, section] : sections) {
		paths.emplace(section, path);
		if (section->type == SectionType::Function && !section->patternDefinitions.empty()) {
			if (!containsNestedDefinition(*section)) {
				selected[path] = sectionIdentity(*section);
				selectedSections.insert(section);
			}
		}
		bool reusableSection = reusableCodegenSection(*section);
		if (reusableSection)
			reusable.insert(section);
		if (reusableSection || !section->patternDefinitions.empty())
			declarationSections++;
	}
	if (projectionWork)
		projectionWork->setupMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	Json bodyDigests = functionBodyDigests(context, sections);
	Json currentScopeRuleBodies = scopeRuleBodyPaths(sections);
	if (projectionWork)
		projectionWork->bodyMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	Json maskedSources = sourceProjection(
		context, observations, selectedSections, false, false, false, prior ? &prior->at("sources") : nullptr, projectionWork
	);
	bool reuseDeclarations =
		prior && selected == prior->at("inactive") && sourceProjectionsEqual(maskedSources, prior->at("sources")) &&
		currentScopeRuleBodies == prior->at("scope_rule_bodies") &&
		reusable.size() == prior->at("function_declarations").size() && bodyDigests.size() == prior->at("bodies").size();
	size_t changedDeclarations = 0;
	if (reuseDeclarations) {
		for (const auto &[path, section] : sections) {
			if (reusable.contains(section) && !prior->at("function_declarations").contains(path)) {
				reuseDeclarations = false;
				break;
			}
			if (!bodyDigests.contains(path))
				continue;
			if (!prior->at("bodies").contains(path)) {
				reuseDeclarations = false;
				break;
			}
			if (bodyDigests.at(path) == prior->at("bodies").at(path))
				continue;
			if (!reusable.contains(section) || currentScopeRuleBodies.contains(path) ||
				sectionDeclaration(*section) != prior->at("function_declarations").at(path)) {
				reuseDeclarations = false;
				break;
			}
			changedDeclarations++;
		}
	}
	Json declarations = Json::object();
	Json allDeclarationEntries = Json::array();
	Json ambientDeclarationEntries = Json::array();
	Json currentGlobalResolution = globalResolutionIdentity(*context.mainSection);
	std::string finalMatcherTree = matcherTreeSignature(context);
	Json finalMatcherRoots;
	const Json *currentFinalMatcherRoots = &initialMatcherRoots;
	if (finalMatcherTree != initialMatcherTree) {
		finalMatcherRoots = prior && prior->at("final_matcher_tree") == finalMatcherTree
								? finalMatcherRootSnapshot(*prior)
								: matcherTreeMutationSnapshot(context);
		currentFinalMatcherRoots = &finalMatcherRoots;
	}
	if (reuseDeclarations) {
		declarations = prior->at("function_declarations");
		if (projectionWork)
			projectionWork->declarationSectionsReused += declarationSections - changedDeclarations;
	} else {
		bool stableDeclarationContext = prior && compatibleSyntax && currentScopeRuleBodies == prior->at("scope_rule_bodies");
		bool stableRootBindings = stableDeclarationContext && currentGlobalResolution == prior->at("global_resolution");
		std::optional<MatcherTreeChanges> initialChanges, finalChanges;
		if (stableDeclarationContext) {
			if (initialMatcherTree != prior->at("initial_matcher_tree").get_ref<const std::string &>())
				initialChanges.emplace(prior->at("initial_matcher_roots"), initialMatcherRoots);
			if (finalMatcherTree != prior->at("final_matcher_tree").get_ref<const std::string &>())
				finalChanges.emplace(finalMatcherRootSnapshot(*prior), *currentFinalMatcherRoots);
		}
		for (const auto &[path, section] : sections) {
			bool reusableSection = reusable.contains(section);
			if (!reusableSection && section->patternDefinitions.empty())
				continue;
			const Json *priorDeclaration =
				stableDeclarationContext && reusableSection && section->type == SectionType::Function &&
						section->parent == context.mainSection && (stableRootBindings || hasNoGlobalAccess(section)) &&
						prior->at("function_declarations").contains(path) && prior->at("bodies").contains(path) &&
						bodyDigests.contains(path) && bodyDigests.at(path) == prior->at("bodies").at(path)
					? &prior->at("function_declarations").at(path)
					: nullptr;
			bool reuseSection = priorDeclaration && priorDeclaration->at("identity") == selected.at(path) &&
								(!initialChanges || !initialChanges->mayAffect(*section)) &&
								(!finalChanges || !finalChanges->mayAffect(*section));
			Json entry = reuseSection ? *priorDeclaration : sectionDeclaration(*section);
			if (reuseSection && projectionWork)
				projectionWork->declarationSectionsReused++;
			if (reusableSection)
				declarations[path] = entry;
			if (!section->patternDefinitions.empty()) {
				entry["path"] = path;
				if (reusableSection)
					allDeclarationEntries.push_back(std::move(entry));
				else {
					allDeclarationEntries.push_back(entry);
					ambientDeclarationEntries.push_back(std::move(entry));
				}
			}
		}
	}
	Json projection{
		{"initial_matcher_tree", initialMatcherTree},
		{"initial_matcher_roots", initialMatcherRoots},
		{"final_matcher_tree", std::move(finalMatcherTree)},
		{"precedence_signature", matcherCache.precedenceSignature()},
		{"precedence_rules", precedenceRulesSignature(*context.mainSection)},
		{"inactive", std::move(selected)},
		{"declarations", reuseDeclarations ? prior->at("declarations") : Json(digest(allDeclarationEntries.dump()))},
		{"global_resolution", std::move(currentGlobalResolution)},
		{"sources", std::move(maskedSources)},
		{"scope_rule_bodies", std::move(currentScopeRuleBodies)}
	};
	if (!finalMatcherRoots.is_null())
		projection["final_matcher_roots"] = std::move(finalMatcherRoots);
	if (projectionWork)
		projectionWork->inactiveMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	projection["bodies"] = std::move(bodyDigests);
	projection["function_declarations"] = std::move(declarations);
	projection["ambient_declarations"] =
		reuseDeclarations ? prior->at("ambient_declarations") : Json(digest(ambientDeclarationEntries.dump()));
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
	bool stableGlobalResolutionEnvironment =
		stableGlobalFreeEnvironment && projection.at("global_resolution") == prior->at("global_resolution");
	if (projectionWork)
		projectionWork->compatibilityMilliseconds += elapsedMilliseconds(phaseStart);
	for (const Section *section : reusable) {
		const std::string &path = paths.at(section);
		bool reuseGlobalMatches = stableGlobalResolutionEnvironment && section->parent == context.mainSection;
		if ((stableMatchingEnvironment || (stableGlobalFreeEnvironment && hasNoGlobalAccess(section)) || reuseGlobalMatches) &&
			prior->at("bodies").contains(path) && prior->at("bodies").at(path) == projection.at("bodies").at(path) &&
			prior->at("function_declarations").at(path) == projection.at("function_declarations").at(path)) {
			matches[path] = prior->at("matches").at(path);
			matchEntries[path] = prior->at("match_entries").at(path);
			if (reuseGlobalMatches && !stableMatchingEnvironment && !hasNoGlobalAccess(section) && projectionWork)
				projectionWork->globalMatchesReused++;
		} else {
			const Json *oldEntries =
				prior && prior->at("match_entries").contains(path) ? &prior->at("match_entries").at(path) : nullptr;
			ResolvedFunctionMatches result = resolvedFunctionMatches(
				section, paths, matcherCache, oldEntries, projectionWork ? &projectionWork->matchEntriesReused : nullptr
			);
			matches[path] = std::move(result.fingerprint);
			matchEntries[path] = std::move(result.entries);
		}
	}
	if (projectionWork)
		projectionWork->matchMilliseconds += elapsedMilliseconds(phaseStart);
	projection["matches"] = std::move(matches);
	projection["match_entries"] = std::move(matchEntries);
	return projection;
}
