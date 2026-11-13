bool stableConstraintProbePrelude(const Json &prior, const Json &current) {
	for (const char *field : {"precedence_signature", "precedence_rules", "scope_rule_bodies"})
		if (prior.at(field) != current.at(field))
			return false;
	return sourceProjectionsEqual(prior.at("constraint_context_sources"), current.at("constraint_context_sources"));
}

// Necessary matcher literals exclude captures and surrounding source. Words are
// complete tokens; punctuation can be split by the matcher, so retain its bytes.
class ConstraintProbeQuery {
  public:
	ConstraintProbeQuery() = default;
	explicit ConstraintProbeQuery(std::string_view text) { append(text); }
	static ConstraintProbeQuery requiredBy(const std::vector<DefinitionPatternElement> &path) {
		ConstraintProbeQuery result;
		for (const DefinitionPatternElement &element : path)
			if (element.type != PatternElement::Type::Variable && !element.promotedFromVariableLike)
				result.append(element.text);
		return result;
	}
	bool containsAll(const ConstraintProbeQuery &required) const {
		return std::ranges::all_of(required.literals, [&](const std::string &literal) {
			return literals.contains(literal);
		});
	}

  private:
	static std::string lowered(std::string text) {
		for (char &character : text)
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		return text;
	}
	void append(std::string_view text) {
		for (const PatternElement &element : getPatternElements(text))
			if (element.type == PatternElement::Type::VariableLike)
				literals.insert(lowered(element.text));
			else if (element.type == PatternElement::Type::Other)
				for (unsigned char character : element.text)
					if (!std::isspace(character))
						literals.insert(lowered(std::string(1, character)));
	}
	std::unordered_set<std::string> literals;
};

class ConstraintProbePattern {
  public:
	explicit ConstraintProbePattern(const Section &section) {
		for (const PatternDefinition *definition : section.patternDefinitions) {
			if (definition->indexedTreeType != SectionType::Function)
				continue;
			for (const auto &path : canonicalPatternPaths(definition->patternElements))
				requirements.emplace_back(definition, ConstraintProbeQuery::requiredBy(path));
		}
	}
	bool mayMatch(const ConstraintProbeQuery &query, const lsp::SourceFile &source) const {
		return std::ranges::any_of(requirements, [&](const auto &path) {
			return isPatternDefinitionVisibleFromSource(*path.first, source) && query.containsAll(path.second);
		});
	}

  private:
	std::vector<std::pair<const PatternDefinition *, ConstraintProbeQuery>> requirements;
};

std::vector<bool> reusableConstraintProbes(
	ParseContext &context, const Json &prior, const Json &current, const Json &graph,
	const std::map<std::string, Section *> &sections, const IncrementalInferenceReads::Keys &changedLookups,
	std::set<std::string> &reusableClasses
) {
	if (!stableConstraintProbePrelude(prior, current))
		return {};
	const Json &schedule = prior.at("constraint_probe_schedule");
	const Json &reverse = graph.at("reverse");
	std::vector<std::string> pending;
	std::unordered_set<std::string> changedDeclarations;
	// Both sides of a declaration edit participate: readers of a removed
	// provider still depend on its previous signature.
	for (const char *field : {"pattern_declarations", "inactive", "bodies", "matches"}) {
		for (const auto &[path, value] : prior.at(field).items())
			if (!current.at(field).contains(path) || current.at(field).at(path) != value) {
				pending.push_back(path);
				if (std::string_view(field) == "pattern_declarations")
					changedDeclarations.insert(path);
			}
		for (const auto &[path, value] : current.at(field).items())
			if (!prior.at(field).contains(path))
				pending.push_back(path);
	}
	std::vector<ConstraintProbePattern> addedPatterns;
	for (const auto &[path, value] : current.at("pattern_declarations").items())
		if (!prior.at("pattern_declarations").contains(path) || prior.at("pattern_declarations").at(path) != value)
			addedPatterns.emplace_back(*sections.at(path));
	const auto items = schedule.at("items").get<std::vector<std::string>>();
	const auto texts = schedule.at("texts").get<std::vector<std::string>>();
	const Json &owners = schedule.at("owners");
	const Json &sources = schedule.at("sources");
	const Json &roots = schedule.at("roots");
	const Json &lookupReads = schedule.at("callable_lookups");
	requireCompilerInvariant(
		items.size() == texts.size() && items.size() == schedule.at("passes").size() && items.size() == roots.size() &&
			items.size() == lookupReads.size() && items.size() == owners.size() && items.size() == sources.size(),
		"saved type-constraint probe schedule has mismatched item records"
	);
	bool topLevelExecutionChanged = !sourceProjectionsEqual(prior.at("ambient_sources"), current.at("ambient_sources"));
	if (topLevelExecutionChanged || prior.at("global_resolution") != current.at("global_resolution"))
		for (const auto &[path, section] : sections)
			if (!section->parent || !section->globalVariables.empty())
				pending.push_back(path);
	std::unordered_map<std::string_view, const lsp::SourceFile *> currentSources;
	for (const auto &[_, source] : context.importedFiles)
		currentSources.emplace(source->uri, source);
	// A constraint result changes its declaration's signature. Link each
	// recorded read through that declaration before following ordinary callers,
	// including candidates observed by deferred or rejected trials.
	std::map<std::string, std::vector<size_t>> readers;
	std::vector<bool> dirty(items.size(), false);
	for (size_t index = 0; index < items.size(); index++) {
		const std::string &owner = owners.at(index).get_ref<const std::string &>();
		for (const Json &root : roots.at(index))
			readers[root.get<std::string>()].push_back(index);
		auto source = currentSources.find(sources.at(index).get_ref<const std::string &>());
		dirty[index] = !sections.contains(owner) || changedDeclarations.contains(owner) || source == currentSources.end() ||
					   std::ranges::any_of(lookupReads.at(index), [&](const Json &key) {
			return changedLookups.contains(key.get_ref<const std::string &>());
		});
		if (!dirty[index]) {
			ConstraintProbeQuery query(texts[index]);
			dirty[index] = std::ranges::any_of(addedPatterns, [&](const ConstraintProbePattern &added) {
				return added.mayMatch(query, *source->second);
			});
		}
		if (dirty[index])
			pending.push_back(owner);
	}
	for (const std::string &key : changedLookups)
		for (const Json &owner : graph.at("callable_lookups").at(key).at("owners"))
			pending.push_back(owner.get<std::string>());
	std::unordered_set<std::string> dirtyClosure;
	while (!pending.empty()) {
		std::string path = std::move(pending.back());
		pending.pop_back();
		if (!dirtyClosure.insert(path).second)
			continue;
		if (reverse.contains(path))
			for (const Json &caller : reverse.at(path))
				pending.push_back(caller.get<std::string>());
		if (auto found = readers.find(path); found != readers.end())
			for (size_t index : found->second) {
				dirty[index] = true;
				pending.push_back(owners.at(index).get<std::string>());
			}
	}
	std::vector<bool> reusable(items.size(), false);
	for (size_t index = 0; index < items.size(); index++)
		reusable[index] = !dirty[index];
	for (const auto &[path, section] : sections)
		if (section->type == SectionType::Class && !dirtyClosure.contains(path))
			reusableClasses.insert(path);
	return reusable;
}

Json reusableClassInstantiations(const Json &saved, const std::set<std::string> &reusable) {
	std::set<std::string> clean;
	for (const auto &[path, state] : saved.items())
		if (reusable.contains(path) && !state.at("requests").is_null())
			clean.insert(path);
	std::map<std::string, std::vector<std::string>> callers;
	std::stack<std::string> pending;
	for (const auto &path : clean)
		for (const auto &dependency : classInstantiationDependencies(saved.at(path))) {
			callers[dependency].push_back(path);
			if (!clean.contains(dependency))
				pending.push(path);
		}
	while (!pending.empty()) {
		std::string path = std::move(pending.top());
		pending.pop();
		if (!clean.erase(path))
			continue;
		if (auto found = callers.find(path); found != callers.end())
			for (const auto &caller : found->second)
				pending.push(caller);
	}
	Json result = Json::object();
	for (const auto &path : clean)
		result[path] = saved.at(path);
	return result;
}

void prepareConstraintProbeReuse(
	ParseContext &context, const Json &prior, const Json &current, const Json &graph,
	const std::map<std::string, Section *> &sections, const IncrementalInferenceReads::Keys &changedLookups
) {
	std::set<std::string> reusableClasses;
	std::vector<bool> reusable =
		reusableConstraintProbes(context, prior, current, graph, sections, changedLookups, reusableClasses);
	if (std::ranges::none_of(reusable, [](bool value) {
		return value;
	}))
		return;
	if (!restoreClassInstantiations(reusableClassInstantiations(prior.at("class_instantiations"), reusableClasses), sections))
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

Json captureConstraintProbeSchedule(
	const ParseContext &context, const std::map<const Section *, std::string> &sectionPaths,
	const IncrementalInferenceReads &inferenceReads, const Json *prior
) {
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
	requireCompilerInvariant(
		context.constraintProbeItems.size() == context.constraintProbeReadsReused.size(),
		"type-constraint probe schedule has mismatched lookup observations"
	);
	const Json *priorSchedule = prior ? &prior->at("all_function_projection").at("constraint_probe_schedule") : nullptr;
	const Json *priorLookups = prior ? &prior->at("section_dependency_graph").at("callable_lookups") : nullptr;
	std::unordered_map<std::string, size_t> priorIndices;
	if (priorSchedule)
		for (size_t index = 0; index < priorSchedule->at("items").size(); index++)
			requireCompilerInvariant(
				priorIndices.emplace(priorSchedule->at("items").at(index).get<std::string>(), index).second,
				"saved constraint queries have duplicate identities"
			);
	std::map<std::string, const Section *> sections;
	for (const auto &[section, path] : sectionPaths)
		sections.emplace(path, section);
	Json owners = Json::array(), sources = Json::array();
	Json roots = Json::array();
	Json values = Json::array();
	Json lookupReads = Json::array();
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	for (size_t index = 0; index < context.constraintProbeDependencies.size(); index++) {
		const auto &dependencies = context.constraintProbeDependencies[index];
		std::set<std::string> paths;
		for (const Section *section : dependencies)
			paths.insert(sectionPaths.at(section));
		const auto &direct = inferenceReads.dependenciesOfProbe(index);
		paths.insert(direct.begin(), direct.end());
		Json identity = Json::parse(context.constraintProbeItems[index]);
		const Section *owner = sections.at(identity.at(2).get<std::string>());
		while (owner->parent && owner->patternDefinitions.empty())
			owner = owner->parent;
		owners.push_back(sectionPaths.at(owner));
		sources.push_back(identity.back());
		std::optional<size_t> previousIndex;
		if (context.constraintProbeReadsReused.at(index))
			previousIndex = priorIndices.at(context.constraintProbeItems[index]);
		auto keys = inferenceReads.readsOfProbe(index, priorSchedule, previousIndex);
		if (previousIndex)
			for (const Json &path : priorSchedule->at("roots").at(*previousIndex))
				paths.insert(path.get_ref<const std::string &>());
		for (const std::string &key : keys) {
			const Json &candidates = inferenceReads.observation(key, priorLookups).at("candidates");
			if (candidates.size() == 1)
				paths.insert(candidates.at(0).at(0).get<std::string>());
		}
		lookupReads.push_back(std::move(keys));
		roots.push_back(paths);
		const auto &value = context.constraintProbeValues[index];
		values.push_back(value ? encodePortableConstraintValue(*value, classPaths).value_or(Json(nullptr)) : Json(nullptr));
	}
	return Json{
		{"items", context.constraintProbeItems},
		{"texts", context.constraintProbeTexts},
		{"owners", std::move(owners)},
		{"sources", std::move(sources)},
		{"passes", context.constraintProbePasses},
		{"roots", roots},
		{"values", values},
		{"callable_lookups", std::move(lookupReads)}
	};
}
