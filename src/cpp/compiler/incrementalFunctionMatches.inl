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
	Json entries;
};

ResolvedFunctionMatches resolvedFunctionMatches(
	const Section *section, const std::map<const Section *, std::string> &paths,
	const IncrementalMatcherCache &matcherCache, const Json *priorEntries, size_t *reused
) {
	Json entries = Json::array();
	Json identities = Json::array();
	std::unordered_map<std::string, const Json *> priorByKey;
	if (priorEntries)
		for (const Json &entry : *priorEntries) {
			if (entry.at("key").is_null())
				continue;
			const std::string &key = entry.at("key").get_ref<const std::string &>();
			auto [found, inserted] = priorByKey.emplace(key, &entry);
			if (!inserted)
				requireCompilerInvariant(
					found->second->at("identity") == entry.at("identity") &&
					found->second->at("shape") == entry.at("shape"),
					"identical matcher keys have different resolved references"
				);
		}
	std::function<void(const Section *)> visit = [&](const Section *body) {
		for (const PatternReference *reference : body->patternReferences) {
			Json shape = Json::array(
				{reference->pattern.text, static_cast<int>(reference->patternType), static_cast<int>(reference->purpose)}
			);
			std::optional<std::string_view> key =
				reference->match ? matcherCache.resultKey(reference) : std::nullopt;
			auto old = key ? priorByKey.find(std::string(*key)) : priorByKey.end();
			Json entry;
			if (old != priorByKey.end() && old->second->at("shape") == shape) {
				entry = *old->second;
				if (reused)
					(*reused)++;
			} else {
				Json identity = shape;
				identity.push_back(reference->match ? resolvedMatchIdentity(*reference->match, paths) : Json(nullptr));
				entry = Json{{"key", key ? Json(*key) : Json(nullptr)},
							 {"shape", std::move(shape)}, {"identity", digest(identity.dump())}};
			}
			identities.push_back(entry.at("identity"));
			entries.push_back(std::move(entry));
		}
		for (const Section *child : body->children)
			if (child->patternDefinitions.empty())
				visit(child);
	};
	visit(section);
	return {digest(identities.dump()), std::move(entries)};
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

bool unchangedFunctionSource(const std::string &path, const Json &prior, const Json &current) {
	const Json &oldDeclarations = prior.at("function_declarations");
	const Json &newDeclarations = current.at("function_declarations");
	const Json &oldBodies = prior.at("bodies");
	const Json &newBodies = current.at("bodies");
	if (!oldDeclarations.contains(path) || !newDeclarations.contains(path) ||
		oldDeclarations.at(path) != newDeclarations.at(path) || !oldBodies.contains(path) || !newBodies.contains(path) ||
		oldBodies.at(path) != newBodies.at(path))
		return false;
	return current.at("declarations") == prior.at("declarations") ||
		   current.at("matches").at(path) == prior.at("matches").at(path);
}
