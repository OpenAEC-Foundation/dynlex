void prepareDeferredMatchReplay(
	ParseContext &context, const Json &previous, const Json &observations, std::string_view initialMatcherTree,
	const std::list<PatternReference *> &deferredReferences
) {
	if (!previous.contains("all_function_projection") || !compatibleObservations(previous, observations, context))
		return;
	const Json &prior = previous.at("all_function_projection");
	if (prior.at("initial_matcher_tree").get_ref<const std::string &>() != initialMatcherTree)
		return;
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	std::unordered_set<const Section *> reusable;
	for (const auto &[_, section] : sections)
		if (reusableCodegenSection(*section))
			reusable.insert(section);
	const Json &priorDeclarations = prior.at("function_declarations");
	if (reusable.size() != priorDeclarations.size())
		return;
	for (const auto &[path, section] : sections)
		if (reusable.contains(section) &&
			(!priorDeclarations.contains(path) || sectionDeclaration(*section) != priorDeclarations.at(path)))
			return;
	Json ambient = sourceProjection(context, observations, reusable, true, true, false, &prior.at("ambient_sources"));
	if (!sourceProjectionsEqual(ambient, prior.at("ambient_sources")))
		return;
	Json bodies = functionBodyDigests(context, sections);
	std::unordered_set<const PatternReference *> deferred(deferredReferences.begin(), deferredReferences.end());
	for (const auto &[path, section] : sections) {
		if (!reusable.contains(section) || !prior.at("bodies").contains(path) || !bodies.contains(path) ||
			bodies.at(path) != prior.at("bodies").at(path) || !prior.at("match_entries").contains(path))
			continue;
		const Json &entries = prior.at("match_entries").at(path);
		size_t index = 0;
		const auto visit = [&](auto &&self, Section *body) -> void {
			for (PatternReference *reference : body->patternReferences) {
				if (index >= entries.size())
					return;
				const Json &entry = entries.at(index++);
				Json shape = Json::array(
					{reference->pattern.text, static_cast<int>(reference->patternType), static_cast<int>(reference->purpose)}
				);
				if (deferred.contains(reference) && entry.at("shape") == shape && entry.contains("initial_key") &&
					entry.at("initial_key").is_string())
					reference->prevalidatedMatcherKey = entry.at("initial_key").get<std::string>();
			}
			for (Section *child : body->children)
				if (child->patternDefinitions.empty())
					self(self, child);
		};
		visit(visit, section);
	}
}
