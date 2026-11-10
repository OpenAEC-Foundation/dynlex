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

std::string callableSignatureKey(const std::vector<PatternElement> &path) {
	std::string key;
	for (const PatternElement &element : path) {
		if (element.type == PatternElement::Variable) {
			// Signature lookup accepts both ordinary and variadic argument slots.
			key += '$';
		} else {
			key += std::to_string(element.text.size());
			key += ':';
			key += element.text;
		}
	}
	return digest(key);
}

template <class Paths>
Json callableCandidateSets(const Paths &sections, const Json &lookups) {
	Json result = Json::object();
	if (lookups.empty())
		return result;
	for (const auto &[signature, _] : lookups.items())
		result[signature] = Json::array();
	for (const auto &[path, section] : sections) {
		if (section->type != SectionType::Function || section->isFlex)
			continue;
		for (size_t index = 0; index < section->patternDefinitions.size(); index++) {
			const PatternDefinition &definition = *section->patternDefinitions[index];
			if (definition.indexedTreeType != SectionType::Function)
				continue;
			for (size_t variant = 0; variant < definition.indexedPaths.size(); variant++) {
				auto found = result.find(callableSignatureKey(definition.indexedPaths[variant]));
				if (found != result.end())
					found->push_back(Json::array({path, index, variant, section->isLocal}));
			}
		}
	}
	return result;
}

class InferredSectionDependencies {
  public:
	explicit InferredSectionDependencies(
		std::vector<Section *> &dependencies, std::set<std::string> *callableLookups = nullptr
	)
		: dependencies(dependencies), callableLookups(callableLookups) {}

	void collectExpression(Expression *expression) {
		visitExpressionTree(expression, [&](Expression *node) {
			for (PatternDefinition *definition : {node->selectedPatternDefinition, node->selectedCallableDefinition})
				if (definition)
					dependencies.push_back(definition->section);
			if (callableLookups && node->selectedCallableDefinition && node->kind == Expression::Kind::IntrinsicCall &&
				intrinsicKind(node->intrinsicName) == IntrinsicKind::Function)
				callableLookups->insert(callableSignatureKey(
					node->selectedCallableDefinition->indexedPaths.at(node->selectedCallablePathIndex.value())
				));
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
	std::set<std::string> *callableLookups;
	std::unordered_set<Expression *> visitedExpressions;
	std::unordered_set<const InstantiatedSectionBody *> visitedBodies;
};

void collectInferredSectionDependencies(const InstantiatedSectionBody *body, std::vector<Section *> &dependencies) {
	InferredSectionDependencies(dependencies).collectBody(body);
}
