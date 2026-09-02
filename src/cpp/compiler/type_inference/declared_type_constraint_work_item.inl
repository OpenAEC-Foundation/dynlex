struct DeclaredTypeConstraintWorkItem {
	PatternDefinition *definition{};
	DefinitionPatternElement *element{};
	Variable *variable{};
	VariableReference *variableReference{};
	Range diagnosticRange;
	std::string typeConstraintName;
	Expression *expression{};
	std::optional<Diagnostic> failureDiagnostic;
};

enum class PatternTypeConstraintProbe { Ready, Deferred, Invalid, Impure };

static bool sectionVariableIsPatternParameter(Section *section, const std::string &name) {
	if (!section)
		return false;
	for (PatternDefinition *definition : section->patternDefinitions) {
		for (const auto &path : definition->indexedPaths) {
			for (const PatternElement &element : path) {
				if (element.type == PatternElement::Type::Variable && element.text == name)
					return true;
			}
		}
	}
	return false;
}

static bool expressionReferencesSectionPatternParameter(Section *section, Expression *expression) {
	return visitExpressionTree(expression, [&](Expression *current) {
		if (current->kind != Expression::Kind::Variable || !current->variable ||
			!sectionVariableIsPatternParameter(section, current->variable->name))
			return false;
		auto definition = section->variableDefinitions.find(current->variable->name);
		return definition != section->variableDefinitions.end() &&
			   normalizeBindingReference(current->variable) == normalizeBindingReference(definition->second);
	});
}

static bool readPatternTypeConstraintValue(
	Expression *expression, InferenceContext &context, TypeConstraint &outConstraint, DataType &outParameterType
) {
	CompileTimeValue value = resolveStoredCompileTimeValue(expression, {}, &context);
	return expression && readTypeConstraintValue(value, expression->type, outConstraint, outParameterType);
}
