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

static bool readPatternTypeConstraintValue(
	Expression *expression, InferenceContext &context, TypeConstraint &outConstraint, DataType &outParameterType
) {
	CompileTimeValue value = resolveStoredCompileTimeValue(expression, {}, &context);
	return expression && readTypeConstraintValue(value, expression->type, outConstraint, outParameterType);
}
