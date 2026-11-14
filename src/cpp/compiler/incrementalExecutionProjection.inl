// A top-level executable edit can change runtime behavior without changing any
// function declaration, body, or matching environment. Refresh the source views
// that include execution, and carry the function projections forward.
std::optional<Json> refreshTopLevelExecutionProjection(
	const ParseContext &context, const Json &observations, const std::map<std::string, Section *> &sections,
	const ResolvedDeclarationUnits &declarationUnits, IncrementalMatcherCache &matcherCache,
	std::string_view initialMatcherTree, const Json &prior, SourceProjectionWork *work, size_t &reusedSections
) {
	Json bodies = definitionBodyDigests(context, sections);
	if (bodies != prior.at("bodies"))
		return std::nullopt;
	if (declarationUnits.digest() != prior.at("declarations").get_ref<const std::string &>())
		return std::nullopt;
	if (initialMatcherTree != prior.at("initial_matcher_tree").get<std::string>() ||
		matcherTreeSignature(context) != prior.at("final_matcher_tree").get<std::string>() ||
		precedenceRulesSignature(*context.mainSection) != prior.at("precedence_rules").get<std::string>() ||
		matcherCache.precedenceSignature() != prior.at("precedence_signature").get<std::string>() ||
		globalResolutionIdentity(*context.mainSection) != prior.at("global_resolution"))
		return std::nullopt;
	Json selected = Json::object();
	std::unordered_set<const Section *> selectedSections;
	std::unordered_set<const Section *> reusable;
	for (const auto &[path, section] : sections) {
		if (section->type == SectionType::Function && !section->patternDefinitions.empty() &&
			!containsNestedDefinition(*section)) {
			selected[path] = sectionIdentity(*section);
			selectedSections.insert(section);
		}
		if (reusableCodegenSection(*section)) {
			if (!prior.at("matches").contains(path) || !prior.at("match_entries").contains(path))
				return std::nullopt;
			reusable.insert(section);
		}
	}
	if (selected != prior.at("inactive") || reusable.size() != prior.at("matches").size())
		return std::nullopt;
	Json nonexecution =
		sourceProjection(context, observations, reusable, true, true, true, &prior.at("ambient_nonexecution_sources"), work);
	if (!sourceProjectionsEqual(nonexecution, prior.at("ambient_nonexecution_sources")))
		return std::nullopt;
	Json refreshed = prior;
	refreshed["sources"] =
		sourceProjection(context, observations, selectedSections, false, false, false, &prior.at("sources"), work);
	refreshed["ambient_sources"] =
		sourceProjection(context, observations, reusable, true, true, false, &prior.at("ambient_sources"), work);
	refreshed["ambient_nonexecution_sources"] = std::move(nonexecution);
	reusedSections = reusable.size();
	return refreshed;
}
