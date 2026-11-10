bool stableConstraintProbePrelude(const Json &prior, const Json &current) {
	for (const char *field :
		 {"precedence_signature", "precedence_rules", "ambient_declarations", "global_resolution", "scope_rule_bodies"})
		if (prior.at(field) != current.at(field))
			return false;
	return sourceProjectionsEqual(prior.at("sources"), current.at("sources")) ||
		   sourceProjectionsEqual(prior.at("ambient_sources"), current.at("ambient_sources")) ||
		   sourceProjectionsEqual(prior.at("ambient_nonexecution_sources"), current.at("ambient_nonexecution_sources"));
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
	for (const char *field : {"function_declarations", "inactive"}) {
		for (const auto &[path, value] : prior.at(field).items())
			if (!current.at(field).contains(path) || current.at(field).at(path) != value)
				return {};
	}
	std::vector<const Section *> addedDefinitions;
	for (const auto &[path, _] : current.at("function_declarations").items()) {
		if (prior.at("function_declarations").contains(path))
			continue;
		if (!sections.contains(path))
			return {};
		addedDefinitions.push_back(sections.at(path));
	}
	for (const auto &[path, _] : current.at("inactive").items())
		if (!prior.at("inactive").contains(path) && !current.at("function_declarations").contains(path))
			return {};
	const auto items = schedule.at("items").get<std::vector<std::string>>();
	const auto texts = schedule.at("texts").get<std::vector<std::string>>();
	const auto passes = schedule.at("passes").get<std::vector<int>>();
	const Json &roots = schedule.at("roots");
	bool topLevelExecutionChanged = !sourceProjectionsEqual(prior.at("ambient_sources"), current.at("ambient_sources"));
	std::unordered_set<std::string> topLevelDependent;
	if (topLevelExecutionChanged) {
		std::vector<std::string> pending;
		for (const auto &[path, section] : sections)
			if (!section->parent || !section->globalVariables.empty())
				pending.push_back(path);
		while (!pending.empty()) {
			std::string path = std::move(pending.back());
			pending.pop_back();
			if (!topLevelDependent.insert(path).second)
				continue;
			if (reverse.contains(path))
				for (const Json &caller : reverse.at(path))
					pending.push_back(caller.get<std::string>());
		}
	}
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
	for (const Section *added : addedDefinitions) {
		auto mayMatchText = [&](std::string_view source) {
			std::string lowered;
			lowered.reserve(source.size());
			for (unsigned char character : source)
				lowered.push_back(static_cast<char>(std::tolower(character)));
			return constraintDefinitionMayMatch(*added, lowered);
		};
		for (const std::string &text : loweredTexts)
			if (constraintDefinitionMayMatch(*added, text))
				return {};
		for (const auto &[_, section] : sections)
			for (const PatternReference *reference : section->patternReferences) {
				if (mayMatchText(reference->pattern.text) || mayMatchText(reference->range().line->fullText))
					return {};
				if (reference->expression)
					for (size_t index = 0; index < reference->expression->arguments.size(); index++)
						if (std::optional<std::string> spelling = numericPatternArgumentSpelling(reference, index);
							spelling && mayMatchText(*spelling))
							return {};
			}
	}
	std::vector<std::string> changedBodies;
	for (const auto &[path, oldBody] : prior.at("bodies").items()) {
		if (!current.at("bodies").contains(path))
			return {};
		if (oldBody != current.at("bodies").at(path) ||
			(prior.at("matches").contains(path) &&
			 (!current.at("matches").contains(path) || prior.at("matches").at(path) != current.at("matches").at(path))))
			changedBodies.push_back(path);
	}
	for (const auto &[path, _] : current.at("bodies").items())
		if (!prior.at("bodies").contains(path) && !current.at("function_declarations").contains(path))
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
			if (dirtyClosure.contains(path) || topLevelDependent.contains(path))
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
	const Json &values = schedule.at("values");
	const Json &roots = schedule.at("roots");
	requireCompilerInvariant(
		values.size() == reusable.size() && roots.size() == reusable.size(),
		"saved type-constraint values and dependencies do not match the probe schedule"
	);
	context.priorConstraintProbeValues.resize(reusable.size());
	context.priorConstraintProbeDependencies.resize(reusable.size());
	for (size_t index = 0; index < reusable.size(); index++) {
		if (!reusable[index])
			continue;
		for (const Json &root : roots.at(index)) {
			auto section = sections.find(root.get<std::string>());
			if (section == sections.end()) {
				reusable[index] = false;
				context.priorConstraintProbeDependencies[index].clear();
				break;
			}
			context.priorConstraintProbeDependencies[index].insert(section->second);
		}
		if (reusable[index] && !values.at(index).is_null())
			context.priorConstraintProbeValues[index] = decodePortableConstraintValue(values.at(index), sections);
	}
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
	requireCompilerInvariant(
		context.constraintProbeItems.size() == context.constraintProbeValues.size(),
		"type-constraint probe schedule has mismatched item and value counts"
	);
	Json roots = Json::array();
	Json values = Json::array();
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	for (size_t index = 0; index < context.constraintProbeDependencies.size(); index++) {
		const auto &dependencies = context.constraintProbeDependencies[index];
		std::set<std::string> paths;
		for (const Section *section : dependencies)
			paths.insert(sectionPaths.at(section));
		roots.push_back(paths);
		const auto &value = context.constraintProbeValues[index];
		values.push_back(value ? encodePortableConstraintValue(*value, classPaths).value_or(Json(nullptr)) : Json(nullptr));
	}
	return Json{
		{"items", context.constraintProbeItems},
		{"texts", context.constraintProbeTexts},
		{"passes", context.constraintProbePasses},
		{"roots", roots},
		{"values", values}
	};
}
