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

class InferredSectionDependencies {
  public:
	explicit InferredSectionDependencies(std::vector<Section *> &dependencies) : dependencies(dependencies) {}

	void collectExpression(Expression *expression) {
		visitExpressionTree(expression, [&](Expression *node) {
			for (PatternDefinition *definition : {node->selectedPatternDefinition, node->selectedCallableDefinition})
				if (definition)
					dependencies.push_back(definition->section);
			collectBody(node->inferredFlexBody.get());
			collectBody(node->inferredCallerBody.get());
			if (node->staticLoopBodies)
				for (const auto &iteration : *node->staticLoopBodies)
					collectBody(iteration.get());
			if (const auto &values = node->dataSectionValues) {
				collectExpression(values->record);
				for (const auto &[_, value] : values->fields)
					collectExpression(value);
			}
			return false;
		}, visitedExpressions);
	}

	void collectBody(const InstantiatedSectionBody *body) {
		if (!body || !visitedBodies.insert(body).second)
			return;
		for (Expression *expression : body->lineExpressions)
			collectExpression(expression);
		for (const auto &child : body->childBodies)
			collectBody(child.get());
	}

	void collectExecution(const Section &section) {
		for (CodeLine *line : section.codeLines)
			collectExpression(line->expression);
		for (const Section *child : section.children)
			if (child->runsInEnclosingBody())
				collectExecution(*child);
	}

  private:
	std::vector<Section *> &dependencies;
	std::unordered_set<Expression *> visitedExpressions;
	std::unordered_set<const InstantiatedSectionBody *> visitedBodies;
};

void collectInferredSectionDependencies(const InstantiatedSectionBody *body, std::vector<Section *> &dependencies) {
	InferredSectionDependencies(dependencies).collectBody(body);
}
