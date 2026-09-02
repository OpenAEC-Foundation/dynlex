static KnownConstantState snapshotKnownConstantsForClassInstantiation(InferenceContext *inferenceContext) {
	return inferenceContext ? inferenceContext->currentVariableValues : KnownConstantState{};
}

static AddressInferenceState snapshotAddressStateForClassInstantiation(InferenceContext *inferenceContext) {
	return inferenceContext ? inferenceContext->currentAddressState : AddressInferenceState{};
}

static void
restoreKnownConstantsForClassInstantiation(InferenceContext *inferenceContext, KnownConstantState savedKnownConstants) {
	if (inferenceContext)
		inferenceContext->currentVariableValues = std::move(savedKnownConstants);
}

static void
restoreAddressStateForClassInstantiation(InferenceContext *inferenceContext, AddressInferenceState savedAddressState) {
	if (inferenceContext)
		inferenceContext->currentAddressState = std::move(savedAddressState);
}

static void
seedKnownConstantsForClassInstantiation(const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext) {
	if (!inferenceContext || bindingFrameStack.empty())
		return;
	const BindingFrame &classBindings = bindingFrameStack.topFrame();
	for (const auto &[parameterDefinition, argumentExpression] : classBindings.parameterBindings) {
		if (!parameterDefinition)
			continue;
		CompileTimeValue argumentValue = resolveStoredCompileTimeValue(argumentExpression, bindingFrameStack, inferenceContext);
		inferenceContext->setKnownConstant(parameterDefinition, argumentValue);
	}
}

static Expression *lookupInferenceFlexExpansion(InferenceContext *inferenceContext, Expression *expression) {
	return inferenceContext ? inferenceContext->lookupFlexExpansion(expression)
							: (expression ? expression->inferredFlexExpansion : nullptr);
}

static CompileTimeValue lookupCompileTimeExpressionValue(Expression *expression, InferenceContext *inferenceContext) {
	if (inferenceContext)
		return inferenceContext->lookupExpressionValue(expression);
	return getExpressionCompileTimeValue(expression);
}

static CompileTimeValue resolveStoredCompileTimeValue(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext
) {
	return resolveStoredCompileTimeValueWith(expression, [&](auto &&stop) {
		return resolveInferenceBindingLayers(
			expression, bindingFrameStack, inferenceContext, std::forward<decltype(stop)>(stop)
		);
	}, [&](Expression *currentExpression) {
		CompileTimeValue knownValue = lookupCompileTimeExpressionValue(currentExpression, inferenceContext);
		if (!isCompileTimeKnown(knownValue) && inferenceContext && currentExpression->kind == Expression::Kind::Variable &&
			currentExpression->variable) {
			knownValue = inferenceContext->lookupKnownConstant(currentExpression->variable);
		}
		return knownValue;
	});
}

static bool isStructurallyCompileTimeConstant(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext &context,
	std::unordered_set<Expression *> &visiting
) {
	if (!expression)
		crashCompilerBug("compile-time constant classification received null expression");
	ResolvedBindingLayers resolved = resolveInferenceBindingLayers(expression, bindingFrameStack, &context);
	Expression *resolvedExpression = resolved.expression;
	if (resolvedExpression && resolvedExpression != expression)
		return isStructurallyCompileTimeConstant(resolvedExpression, resolved.bindingFrameStack, context, visiting);
	if (isCompileTimeKnown(resolveImmediateCompileTimeValue(expression)))
		return true;
	if (expression->kind == Expression::Kind::Variable && expression->variable) {
		return context.currentInstantiation &&
			   variableReferenceIsCurrentInstantiationParameter(context, expression->variable) &&
			   context.currentInstantiation->requiredCompileTimeParameters.contains(expression->variable->name) &&
			   context.currentInstantiation->constantParameterValues.contains(expression->variable->name);
	}
	if (!isCompileTimeKnown(context.lookupExpressionValue(expression)))
		return false;
	if (!visiting.insert(expression).second)
		crashCompilerBug("compile-time constant expression contains a cycle");
	for (Expression *argument : expression->arguments) {
		if (!isStructurallyCompileTimeConstant(argument, bindingFrameStack, context, visiting)) {
			visiting.erase(expression);
			return false;
		}
	}
	visiting.erase(expression);
	return true;
}

static bool isStructurallyCompileTimeConstant(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext &context
) {
	std::unordered_set<Expression *> visiting;
	return isStructurallyCompileTimeConstant(expression, bindingFrameStack, context, visiting);
}

static bool resolveStoredCompileTimeInteger(
	Expression *expression, const BindingFrameStack &bindingFrameStack, int &outValue, InferenceContext *inferenceContext
) {
	return narrowCompileTimeInteger(resolveStoredCompileTimeValue(expression, bindingFrameStack, inferenceContext), outValue);
}

struct ScopedDiagnosticSuppression {
	InferenceContext &context;
	bool previous;

	explicit ScopedDiagnosticSuppression(InferenceContext &context) : context(context), previous(context.suppressDiagnostics) {
		context.suppressDiagnostics = true;
	}

	~ScopedDiagnosticSuppression() { context.suppressDiagnostics = previous; }
};
