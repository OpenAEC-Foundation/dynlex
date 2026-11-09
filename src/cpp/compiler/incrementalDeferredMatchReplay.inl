// The program snapshot owns these entries throughout frontend compilation.
using PriorMatchKeys = std::unordered_map<const PatternReference *, const Json *>;

void prepareDeferredMatchReplay(
	ParseContext &context, const Json &previous, const Json &observations, std::string_view initialMatcherTree,
	const std::list<PatternReference *> &deferredReferences, PriorMatchKeys &precedenceKeys
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
		std::vector<PatternReference *> references;
		const auto collect = [&](auto &&self, Section *body) -> void {
			references.insert(references.end(), body->patternReferences.begin(), body->patternReferences.end());
			for (Section *child : body->children)
				if (child->patternDefinitions.empty())
					self(self, child);
		};
		collect(collect, section);
		if (references.size() != entries.size())
			continue;
		bool sameShapes = true;
		for (size_t index = 0; index < references.size(); index++) {
			const Json &shape = entries.at(index).at("shape");
			PatternReference *reference = references[index];
			if (shape.at(0).get_ref<const std::string &>() != reference->pattern.text ||
				shape.at(1).get<int>() != static_cast<int>(reference->patternType) ||
				shape.at(2).get<int>() != static_cast<int>(reference->purpose)) {
				sameShapes = false;
				break;
			}
		}
		if (!sameShapes)
			continue;
		for (size_t index = 0; index < references.size(); index++) {
			PatternReference *reference = references[index];
			const Json &entry = entries.at(index);
			if (!entry.at("initial_key").is_string())
				continue;
			const std::string &initial = entry.at("initial_key").get_ref<const std::string &>();
			if (deferred.contains(reference))
				reference->prevalidatedMatcherKey = initial;
			if (entry.at("key").is_string() && entry.at("key") != entry.at("initial_key"))
				precedenceKeys.emplace(reference, &entry);
		}
	}
}

void preparePrecedenceMatchReplay(
	ParseContext &context, const Json &prior, IncrementalMatcherCache &matcherCache, const PriorMatchKeys &precedenceKeys,
	const std::vector<PatternReference *> &candidates
) {
	if (precedenceKeys.empty() || context.patternPrecedenceRevision != 1 ||
		matcherTreeSignature(context) != prior.at("final_matcher_tree").get<std::string>() ||
		matcherCache.precedenceSignature() != prior.at("precedence_signature").get<std::string>())
		return;
	for (PatternReference *reference : candidates) {
		auto old = precedenceKeys.find(reference);
		if (old == precedenceKeys.end())
			continue;
		const Json &entry = *old->second;
		if (reference->incrementalMatcherKey == entry.at("initial_key").get_ref<const std::string &>())
			reference->prevalidatedMatcherKey = entry.at("key").get_ref<const std::string &>();
	}
}
