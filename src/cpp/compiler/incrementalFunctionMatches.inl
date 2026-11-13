Json patternNodeIdentity(const PatternTreeNode *node) {
	Json path = Json::array();
	for (const PatternTreeNode *current = node; current && current->parentNode; current = current->parentNode)
		path.push_back(Json::array({static_cast<int>(current->type), current->text, current->isArgumentPack}));
	std::reverse(path.begin(), path.end());
	return path;
}

Json resolvedMatchIdentity(const PatternMatch &match, const std::map<const Section *, std::string> &paths) {
	Json definitions = Json::array();
	for (const PatternDefinition *definition : match.matchingDefinitions)
		definitions.push_back(Json::array({paths.at(definition->section), definition->toString()}));
	Json passed = Json::array();
	for (const PatternTreeNode *node : match.nodesPassed)
		passed.push_back(patternNodeIdentity(node));
	Json variables = Json::array();
	for (const VariableMatch &variable : match.discoveredVariables)
		variables.push_back(Json::array({variable.name, variable.lineStartPos, variable.lineEndPos}));
	Json literals = Json::array();
	for (const AcceptedLiteralMatch &literal : match.acceptedLiterals)
		literals.push_back(patternNodeIdentity(literal.node));
	Json children = Json::array();
	for (const PatternMatch &child : match.subMatches)
		children.push_back(resolvedMatchIdentity(child, paths));
	Json arguments = Json::array();
	for (const MatchedArgument &argument : match.orderedArguments) {
		Json expression = nullptr;
		if (argument.expression)
			expression = Json::array(
				{argument.expression->range.start(), argument.expression->range.end(),
				 std::string(argument.expression->range.subString)}
			);
		arguments.push_back(
			Json::array(
				{argument.argumentIndex, static_cast<int>(argument.kind), argument.itemIndex, argument.isPackElement,
				 expression}
			)
		);
	}
	return Json{
		{"end", patternNodeIdentity(match.matchedEndNode)},
		{"definitions", std::move(definitions)},
		{"start", match.lineStartPos},
		{"finish", match.lineEndPos},
		{"passed", std::move(passed)},
		{"variables", std::move(variables)},
		{"literals", std::move(literals)},
		{"children", std::move(children)},
		{"arguments", std::move(arguments)}
	};
}

struct ResolvedFunctionMatches {
	std::string fingerprint;
	std::string entries;
};

ResolvedFunctionMatches resolvedFunctionMatches(
	const Section *section, const std::map<const Section *, std::string> &paths, IncrementalMatcherCache &matcherCache,
	const Json *priorEntries, const Json *priorFingerprint, size_t *reused
) {
	std::vector<PatternReference *> references;
	std::function<void(const Section *)> visit = [&](const Section *body) {
		references.insert(references.end(), body->patternReferences.begin(), body->patternReferences.end());
		for (const Section *child : body->children)
			if (child->patternDefinitions.empty())
				visit(child);
	};
	visit(section);
	auto oldEntries = priorEntries ? matcherCache.restoreReferenceKeys(priorEntries->get_ref<const std::string &>()) : nullptr;
	auto oldIdentities =
		priorFingerprint ? matcherCache.restoreMatchIdentities(priorFingerprint->get_ref<const std::string &>()) : std::nullopt;
	std::unordered_map<std::string_view, size_t> priorByKey;
	if (oldEntries && oldIdentities) {
		requireCompilerInvariant(oldEntries->size() == oldIdentities->size(), "cached match identities have different shapes");
		for (size_t index = 0; index < oldEntries->size(); index++) {
			const auto &entry = oldEntries->at(index);
			if (entry.finalKey.empty())
				continue;
			auto [found, inserted] = priorByKey.emplace(entry.finalKey, index);
			if (!inserted) {
				const auto &other = oldEntries->at(found->second);
				requireCompilerInvariant(
					oldIdentities->at(found->second) == oldIdentities->at(index) && other.pattern == entry.pattern &&
						other.patternType == entry.patternType && other.purpose == entry.purpose,
					"identical matcher keys have different resolved references"
				);
			}
		}
	}
	std::vector<std::string> newIdentities;
	std::vector<std::string_view> identities;
	newIdentities.reserve(references.size());
	identities.reserve(references.size());
	for (const PatternReference *reference : references) {
		std::optional<std::string_view> key = reference->match ? matcherCache.resultKey(reference) : std::nullopt;
		auto old = key ? priorByKey.find(*key) : priorByKey.end();
		if (old != priorByKey.end()) {
			const auto &saved = oldEntries->at(old->second);
			if (saved.pattern == reference->pattern.text && saved.patternType == static_cast<int>(reference->patternType) &&
				saved.purpose == static_cast<int>(reference->purpose)) {
				identities.push_back(oldIdentities->at(old->second));
				if (reused)
					(*reused)++;
				continue;
			}
		}
		Json identity = Json::array(
			{reference->pattern.text, static_cast<int>(reference->patternType), static_cast<int>(reference->purpose)}
		);
		identity.push_back(reference->match ? resolvedMatchIdentity(*reference->match, paths) : Json(nullptr));
		if (!reference->match && reference->patternElements.size() == 1 &&
			reference->patternElements.front().type == PatternElement::Type::Variable)
			identity.push_back(reference->patternElements.front().text);
		newIdentities.push_back(digest(identity.dump()));
		identities.push_back(newIdentities.back());
	}
	return {matcherCache.recordMatchIdentities(identities), matcherCache.recordReferenceKeys(references)};
}

bool compatibleFunctionProjection(const Json &prior, const Json &current) {
	if (current.at("declarations") == prior.at("declarations") &&
		sourceProjectionsEqual(current.at("sources"), prior.at("sources")) && current.at("inactive") == prior.at("inactive"))
		return true;
	return current.at("ambient_declarations") == prior.at("ambient_declarations") &&
		   sourceProjectionsEqual(current.at("ambient_sources"), prior.at("ambient_sources"));
}

bool compatibleGlobalFreeProjection(const Json &prior, const Json &current) {
	return current.at("ambient_declarations") == prior.at("ambient_declarations") &&
		   sourceProjectionsEqual(current.at("ambient_nonexecution_sources"), prior.at("ambient_nonexecution_sources"));
}

// Resolution units are independent of native function emission: flexes, section
// declarations and class bodies also own references and lexical bindings.
bool reusableResolutionSection(const Section &section) {
	return (!section.patternDefinitions.empty() || section.type == SectionType::Retain ||
			section.type == SectionType::Release) &&
		   !containsNestedDefinition(section);
}

const std::vector<IncrementalMatcherCache::ReferenceMatchKeys> *
savedReferenceKeys(const Json &projection, IncrementalMatcherCache &matcherCache, const std::string &path) {
	const Json &overrides = projection.at("matcher_projection").at("match_overrides");
	if (overrides.contains(path))
		return matcherCache.restoreReferenceKeys(overrides.at(path).get_ref<const std::string &>());
	if (!projection.at("match_entries").contains(path))
		return nullptr;
	return matcherCache.restoreReferenceKeys(projection.at("match_entries").at(path).get_ref<const std::string &>());
}
