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
		arguments.push_back(Json::array(
			{argument.argumentIndex, static_cast<int>(argument.kind), argument.itemIndex, argument.isPackElement,
			 expression}
		));
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

std::string resolvedFunctionMatches(const Section *section, const std::map<const Section *, std::string> &paths) {
	Json references = Json::array();
	std::function<void(const Section *)> visit = [&](const Section *body) {
		for (const PatternReference *reference : body->patternReferences) {
			references.push_back(Json{
				{"text", reference->pattern.text},
				{"type", static_cast<int>(reference->patternType)},
				{"purpose", static_cast<int>(reference->purpose)},
				{"match", reference->match ? resolvedMatchIdentity(*reference->match, paths) : Json(nullptr)}
			});
		}
		for (const Section *child : body->children)
			if (child->patternDefinitions.empty())
				visit(child);
	};
	visit(section);
	return digest(references.dump());
}

bool compatibleFunctionProjection(const Json &prior, const Json &current) {
	if (current.at("declarations") == prior.at("declarations") && current.at("sources") == prior.at("sources") &&
		current.at("inactive") == prior.at("inactive"))
		return true;
	return current.at("ambient_declarations") == prior.at("ambient_declarations") &&
		   current.at("ambient_sources") == prior.at("ambient_sources");
}

bool compatibleGlobalFreeProjection(const Json &prior, const Json &current) {
	return current.at("ambient_declarations") == prior.at("ambient_declarations") &&
		   current.at("ambient_nonexecution_sources") == prior.at("ambient_nonexecution_sources");
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
