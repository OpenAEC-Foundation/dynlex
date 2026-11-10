bool stableConstraintProbePrelude(const Json &prior, const Json &current) {
	for (const char *field :
		 {"initial_matcher_tree", "final_matcher_tree", "precedence_signature", "precedence_rules", "declarations",
		  "ambient_declarations", "global_resolution", "scope_rule_bodies", "inactive"})
		if (prior.at(field) != current.at(field))
			return false;
	return sourceProjectionsEqual(prior.at("sources"), current.at("sources"));
}

bool constraintDefinitionMayMatch(const Section &section, std::string_view loweredText) {
	for (const PatternDefinition *definition : section.patternDefinitions)
		for (const auto &path : canonicalPatternPaths(definition->patternElements))
			for (const DefinitionPatternElement &element : path) {
				if (element.type == PatternElement::Type::Variable || element.promotedFromVariableLike)
					return true;
				if (element.type == PatternElement::Type::Other) {
					if (std::ranges::all_of(element.text, [](unsigned char character) {
						return std::isspace(character);
					}))
						continue;
					return true;
				}
				std::string literal;
				for (unsigned char character : element.text) {
					if (character >= 128)
						return true;
					literal.push_back(static_cast<char>(std::tolower(character)));
				}
				if (literal.empty())
					continue;
				if (loweredText.find(literal) != std::string_view::npos)
					return true;
				break;
			}
	return false;
}

std::vector<bool> reusableConstraintProbes(
	const Json &prior, const Json &current, const Json &graph, const std::map<std::string, Section *> &sections
) {
	if (!stableConstraintProbePrelude(prior, current))
		return {};
	const Json &schedule = prior.at("constraint_probe_schedule");
	const Json &forward = graph.at("forward");
	const Json &reverse = graph.at("reverse");
	if (!graph.at("unknown").empty())
		return {};
	const auto items = schedule.at("items").get<std::vector<std::string>>();
	const auto texts = schedule.at("texts").get<std::vector<std::string>>();
	const auto passes = schedule.at("passes").get<std::vector<int>>();
	const Json &roots = schedule.at("roots");
	requireCompilerInvariant(
		items.size() == texts.size() && items.size() == passes.size() && items.size() == roots.size(),
		"saved type-constraint probe schedule has mismatched item records"
	);
	std::vector<std::string> loweredTexts;
	loweredTexts.reserve(texts.size());
	for (const std::string &text : texts) {
		std::string lowered;
		lowered.reserve(text.size());
		for (unsigned char character : text)
			lowered.push_back(static_cast<char>(std::tolower(character)));
		loweredTexts.push_back(std::move(lowered));
	}
	std::vector<std::string> changedBodies;
	for (const auto &[path, oldBody] : prior.at("bodies").items()) {
		if (!current.at("bodies").contains(path))
			return {};
		if (oldBody != current.at("bodies").at(path))
			changedBodies.push_back(path);
	}
	if (prior.at("bodies").size() != current.at("bodies").size())
		return {};
	// Reverse reachability identifies both committed dependencies and candidate
	// functions that an earlier speculative grouping may have rejected.
	std::unordered_set<std::string> dirtyClosure;
	std::vector<std::string> pending = std::move(changedBodies);
	while (!pending.empty()) {
		std::string path = std::move(pending.back());
		pending.pop_back();
		if (!dirtyClosure.insert(path).second)
			continue;
		if (!sections.contains(path) || !forward.contains(path))
			return {};
		if (reverse.contains(path))
			for (const Json &caller : reverse.at(path))
				pending.push_back(caller.get<std::string>());
	}
	std::vector<bool> dirty(items.size(), false);
	for (size_t index = 0; index < items.size(); index++)
		for (const Json &root : roots.at(index)) {
			std::string path = root.get<std::string>();
			if (!forward.contains(path))
				return {};
			if (dirtyClosure.contains(path))
				dirty[index] = true;
		}
	for (const std::string &path : dirtyClosure) {
		const Section &section = *sections.at(path);
		if (section.patternDefinitions.empty())
			continue;
		for (size_t index = 0; index < items.size(); index++)
			if (!dirty[index] && constraintDefinitionMayMatch(section, loweredTexts[index]))
				dirty[index] = true;
	}
	size_t firstDirty = items.size();
	for (size_t index = 0; index < items.size(); index++)
		if (dirty[index])
			firstDirty = std::min(firstDirty, index);
	std::vector<bool> reusable(items.size(), false);
	for (size_t index = 0; index < items.size(); index++)
		reusable[index] = !dirty[index] && (firstDirty == items.size() || (passes[index] == 1 && index < firstDirty));
	return reusable;
}

void prepareConstraintProbeReuse(
	ParseContext &context, const Json &prior, const Json &current, const Json &graph,
	const std::map<std::string, Section *> &sections
) {
	std::vector<bool> reusable = reusableConstraintProbes(prior, current, graph, sections);
	if (std::ranges::none_of(reusable, [](bool value) {
		return value;
	}))
		return;
	const Json &schedule = prior.at("constraint_probe_schedule");
	context.priorConstraintProbeItems = schedule.at("items").get<std::vector<std::string>>();
	context.priorConstraintProbePasses = schedule.at("passes").get<std::vector<int>>();
	context.priorConstraintProbeReusable = std::move(reusable);
	context.reuseDeclaredConstraintProbes = true;
}

Json captureConstraintProbeSchedule(const ParseContext &context, const std::map<const Section *, std::string> &sectionPaths) {
	requireCompilerInvariant(
		context.constraintProbeItems.size() == context.constraintProbePasses.size(),
		"type-constraint probe schedule has mismatched item and pass counts"
	);
	for (int pass : context.constraintProbePasses)
		requireCompilerInvariant(pass > 0, "successful type-constraint probe has no pass");
	requireCompilerInvariant(
		context.constraintProbeItems.size() == context.constraintProbeTexts.size(),
		"type-constraint probe schedule has mismatched item and text counts"
	);
	requireCompilerInvariant(
		context.constraintProbeItems.size() == context.constraintProbeDependencies.size(),
		"type-constraint probe schedule has mismatched item and dependency counts"
	);
	Json roots = Json::array();
	for (const auto &dependencies : context.constraintProbeDependencies) {
		std::set<std::string> paths;
		for (const Section *section : dependencies)
			paths.insert(sectionPaths.at(section));
		roots.push_back(paths);
	}
	return Json{
		{"items", context.constraintProbeItems},
		{"texts", context.constraintProbeTexts},
		{"passes", context.constraintProbePasses},
		{"roots", roots}
	};
}
