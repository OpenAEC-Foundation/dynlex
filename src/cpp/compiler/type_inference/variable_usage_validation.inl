#include "variable_usage_flow.inl"

static void
collectUsageReferences(Section *section, VariableReference *definition, std::vector<VariableReference *> &references) {
	auto found = section->variableReferences.find(definition->name);
	if (found != section->variableReferences.end())
		for (VariableReference *reference : found->second)
			if (normalizeBindingReference(reference) == definition)
				references.push_back(reference);
	for (Section *child : section->children)
		if (child->runsInEnclosingBody())
			collectUsageReferences(child, definition, references);
}

static void validateVariableUsage(
	InferenceContext &context, Section *section, InstantiatedSectionBody *body, const BindingFrameStack &bindings,
	const std::shared_ptr<const VariableStorageScope> &owner
) {
	for (const auto &[name, definition] :
		 std::map<std::string, VariableReference *>(section->variableDefinitions.begin(), section->variableDefinitions.end())) {
		if (definition->range.sourceStart().sourceFile->isInternal)
			continue;
		bool parameter = !section->isFlex && context.currentInstantiation && context.currentInstantiation->body &&
						 context.currentInstantiation->body->sourceSection == section &&
						 variableReferenceIsCurrentInstantiationParameter(context, definition);
		std::vector<VariableReference *> references;
		collectUsageReferences(section, definition, references);
		VariableReference *first = nullptr;
		for (VariableReference *reference : references) {
			if (reference == definition || (parameter && reference->range.section() == section))
				continue;
			if (!parameter && reference->range.line != definition->range.line)
				continue;
			if (!first || std::pair(reference->range.line->mergedLineIndex, reference->range.start()) <
							  std::pair(first->range.line->mergedLineIndex, first->range.start()))
				first = reference;
		}
		if (!first)
			continue;
		Expression *expression = body ? body->expressionForLine(first->range.line) : first->range.line->expression;
		// Unreachable source lines have no typed usage to diagnose.
		if (!expression || !expression->type.isDeduced())
			continue;
		std::unordered_set<Expression *> active;
		VariableUsage usage = analyzeVariableUsage(expression, definition, parameter, bindings, active);
		if (!(parameter ? ((usage.first & VariableUsage::Write) && !(usage.first & VariableUsage::Read))
						: (usage.first & VariableUsage::Read)))
			continue;
		Diagnostic diagnostic(
			context.parseContext, Diagnostic::Level::Warning, "variable usage warning",
			parameter ? "pattern argument assigned before read" : "variable read before assigned", first->range, "name", name
		);
		diagnostic.relatedInfo.push_back(configuredRelatedInfo(
			context.parseContext, first->range, "variable usage warning",
			parameter ? "related argument definition" : "related assignment", definition->range
		));
		auto &emission = diagnostic.editEmission();
		emission.owner = owner;
		emission.discriminator = parameter ? "parameter assigned before read" : "variable read before assigned";
		context.queueDiagnostic(std::move(diagnostic));
	}
	for (Section *child : section->children)
		if (child->runsInEnclosingBody())
			validateVariableUsage(context, child, body ? body->bodyForChild(child) : nullptr, bindings, owner);
}
