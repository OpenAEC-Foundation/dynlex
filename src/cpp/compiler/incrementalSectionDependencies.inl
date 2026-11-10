void collectMatchedSectionDependencies(const Section *body, std::vector<Section *> &dependencies) {
	std::function<void(const PatternMatch &)> visitMatch = [&](const PatternMatch &match) {
		for (PatternDefinition *definition : match.matchingDefinitions)
			dependencies.push_back(definition->section);
		for (const PatternMatch &child : match.subMatches)
			visitMatch(child);
	};
	for (PatternReference *reference : body->patternReferences)
		if (reference->match)
			visitMatch(*reference->match);
	for (const Section *child : body->children)
		if (child->patternDefinitions.empty())
			collectMatchedSectionDependencies(child, dependencies);
}

void collectInferredSectionDependencies(const InstantiatedSectionBody *body, std::vector<Section *> &dependencies) {
	if (!body)
		return;
	for (Expression *expression : body->lineExpressions)
		visitExpressionTree(expression, [&](Expression *node) {
			for (PatternDefinition *definition : {node->selectedPatternDefinition, node->selectedCallableDefinition})
				if (definition)
					dependencies.push_back(definition->section);
			return false;
		});
	for (const auto &child : body->childBodies)
		collectInferredSectionDependencies(child.get(), dependencies);
}
