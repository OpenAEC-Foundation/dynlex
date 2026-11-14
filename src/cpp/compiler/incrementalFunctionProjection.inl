Json definitionBodyDigests(const ParseContext &context, const std::map<std::string, Section *> &sections) {
	struct Body {
		const lsp::SourceFile *source = nullptr;
		std::string text;
	};
	std::map<const Section *, Body> bodies;
	for (const auto &[_, section] : sections)
		if (!section->patternDefinitions.empty() || section->type == SectionType::Retain ||
			section->type == SectionType::Release)
			bodies.try_emplace(section);
	for (const CodeLine *line : context.codeLines) {
		if (line->rightTrimmedText.empty())
			continue;
		for (const Section *section = line->section; section; section = section->parent)
			if (auto body = bodies.find(section); body != bodies.end()) {
				Body &content = body->second;
				// Equal text can resolve differently under file-local visibility.
				// Frame source transitions and lines so their boundaries are unambiguous.
				if (content.source != line->sourceFile) {
					content.source = line->sourceFile;
					content.text += 'F' + std::to_string(content.source->uri.size()) + ':' + content.source->uri;
				}
				content.text += 'L' + std::to_string(line->rightTrimmedText.size()) + ':';
				content.text.append(line->rightTrimmedText);
			}
	}
	Json result = Json::object();
	for (const auto &[path, section] : sections)
		if (auto body = bodies.find(section); body != bodies.end())
			result[path] = digest(body->second.text);
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
	ResolvedDeclarationUnits &declarationUnits, IncrementalMatcherCache &matcherCache, std::string_view initialMatcherTree,
	std::string_view finalMatcherTree, const Json *previous, SourceProjectionWork *projectionWork
) {
	auto phaseStart = Clock::now();
	const Json *prior =
		previous && previous->contains("all_function_projection") ? &previous->at("all_function_projection") : nullptr;
	Json selected = Json::object();
	Json matches = Json::object();
	Json matchEntries = Json::object();
	std::map<const Section *, std::string> paths;
	std::unordered_set<const Section *> reusable;
	std::unordered_set<const Section *> constraintDefinitions;
	std::unordered_set<const Section *> selectedSections;
	size_t declarationSections = 0;
	for (const auto &[path, section] : sections) {
		paths.emplace(section, path);
		if (!section->patternDefinitions.empty())
			constraintDefinitions.insert(section);
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
	Json bodyDigests = definitionBodyDigests(context, sections);
	Json currentScopeRuleBodies = scopeRuleBodyPaths(sections);
	if (projectionWork)
		projectionWork->bodyMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	Json maskedSources = sourceProjection(
		context, observations, selectedSections, false, false, false, prior ? &prior->at("sources") : nullptr, projectionWork
	);
	std::string resolvedDeclarations = declarationUnits.digest();
	bool reuseDeclarations = prior && resolvedDeclarations == prior->at("declarations").get_ref<const std::string &>() &&
							 reusable.size() == prior->at("function_declarations").size();
	if (reuseDeclarations) {
		for (const auto &[path, section] : sections) {
			if (reusable.contains(section) && !prior->at("function_declarations").contains(path)) {
				reuseDeclarations = false;
				break;
			}
		}
	}
	Json declarations = Json::object();
	Json currentGlobalResolution = globalResolutionIdentity(*context.mainSection);
	if (reuseDeclarations) {
		declarations = prior->at("function_declarations");
		if (projectionWork)
			projectionWork->declarationSectionsReused += declarationSections;
	} else {
		for (const auto &[path, section] : sections) {
			bool reusableSection = reusable.contains(section);
			if (!reusableSection)
				continue;
			declarations[path] = declarationUnits.record(*section);
		}
		if (projectionWork)
			projectionWork->declarationSectionsReused += declarationUnits.reusedRecords();
	}
	Json projection{
		{"initial_matcher_tree", initialMatcherTree},
		{"final_matcher_tree", finalMatcherTree},
		{"precedence_signature", matcherCache.precedenceSignature()},
		{"precedence_rules", precedenceRulesSignature(*context.mainSection)},
		{"inactive", std::move(selected)},
		{"declarations", std::move(resolvedDeclarations)},
		{"global_resolution", std::move(currentGlobalResolution)},
		{"sources", std::move(maskedSources)},
		{"scope_rule_bodies", std::move(currentScopeRuleBodies)}
	};
	if (projectionWork)
		projectionWork->inactiveMilliseconds += elapsedMilliseconds(phaseStart);
	phaseStart = Clock::now();
	projection["bodies"] = std::move(bodyDigests);
	projection["function_declarations"] = std::move(declarations);
	projection["declaration_fingerprints"] = declarationUnits.fingerprints();
	projection["ambient_declarations"] = declarationUnits.digest(reusable);
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
	projection["constraint_context_sources"] = sourceProjection(
		context, observations, constraintDefinitions, true, true, true,
		prior ? &prior->at("constraint_context_sources") : nullptr, projectionWork
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
		if (context.deferredBodyResolution && context.deferredBodyResolution(section)) {
			matches[path] = prior->at("matches").at(path);
			matchEntries[path] = prior->at("match_entries").at(path);
			continue;
		}
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
				section, paths, matcherCache, oldEntries, oldEntries ? &prior->at("matches").at(path) : nullptr,
				projectionWork ? &projectionWork->matchEntriesReused : nullptr
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
