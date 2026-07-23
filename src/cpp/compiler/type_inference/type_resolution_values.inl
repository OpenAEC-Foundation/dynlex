static std::unordered_map<VariableReference *, CompileTimeValue>
snapshotKnownConstantsForClassInstantiation(InferenceContext *inferenceContext) {
	return inferenceContext ? inferenceContext->currentVariableValues
							: std::unordered_map<VariableReference *, CompileTimeValue>{};
}

static AddressInferenceState snapshotAddressStateForClassInstantiation(InferenceContext *inferenceContext) {
	return inferenceContext ? inferenceContext->currentAddressState : AddressInferenceState{};
}

static void restoreKnownConstantsForClassInstantiation(
	InferenceContext *inferenceContext, std::unordered_map<VariableReference *, CompileTimeValue> savedKnownConstants
) {
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

static Expression *resolveCompileTimeBindingForInference(
	Expression *expression, const BindingFrameStack &bindingFrameStack, BindingFrameStack *outBindingFrameStack,
	InferenceContext *inferenceContext
) {
	Expression *resolvedExpression = resolveCompileTimeBinding(expression, bindingFrameStack, outBindingFrameStack);
	if (!inferenceContext || !inferenceContext->trial || !expression || resolvedExpression != expression)
		return resolvedExpression;
	if (expression->kind != Expression::Kind::PatternCall)
		return resolvedExpression;
	Expression *trialFlexExpansion = inferenceContext->lookupFlexExpansion(expression);
	if (!trialFlexExpansion)
		return resolvedExpression;
	PatternDefinition *selectedPatternDefinition = expression->selectedPatternDefinition;
	if (!selectedPatternDefinition)
		crashCompilerBug("trial flex expansion has no selected definition");
	if (!selectedPatternDefinition || !selectedPatternDefinition->section || !selectedPatternDefinition->section->isFlex ||
		selectedPatternDefinition->section->type != SectionType::Function) {
		return resolvedExpression;
	}
	BindingFrame innerBindings;
	collectPatternCallBindings(expression, selectedPatternDefinition, innerBindings);
	materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
	if (outBindingFrameStack) {
		*outBindingFrameStack = bindingFrameStack;
		pushBindingScope(*outBindingFrameStack, std::move(innerBindings));
	}
	return trialFlexExpansion;
}

static CompileTimeValue resolveStoredCompileTimeValue(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext
) {
	if (!expression)
		crashCompilerBug("compile-time value resolution received null expression");
	Expression *currentExpression = expression;
	BindingFrameStack currentBindingFrameStack = bindingFrameStack;
	constexpr size_t maxResolutionDepth = 256;
	for (size_t depth = 0; currentExpression && depth < maxResolutionDepth; depth++) {
		CompileTimeValue storedValue = lookupCompileTimeExpressionValue(currentExpression, inferenceContext);
		if (isCompileTimeKnown(storedValue))
			return storedValue;
		if (inferenceContext && currentExpression->kind == Expression::Kind::Variable && currentExpression->variable) {
			CompileTimeValue knownValue = inferenceContext->lookupKnownConstant(currentExpression->variable);
			if (isCompileTimeKnown(knownValue))
				return knownValue;
		}
		BindingFrameStack resolvedBindingFrameStack;
		Expression *resolvedExpression = resolveCompileTimeBindingForInference(
			currentExpression, currentBindingFrameStack, &resolvedBindingFrameStack, inferenceContext
		);
		if (resolvedExpression && resolvedExpression != currentExpression) {
			currentExpression = resolvedExpression;
			currentBindingFrameStack = std::move(resolvedBindingFrameStack);
			continue;
		}
		CompileTimeValue immediateValue = resolveImmediateCompileTimeValue(currentExpression);
		if (isCompileTimeKnown(immediateValue))
			return immediateValue;
		break;
	}
	return {};
}

static bool isStructurallyCompileTimeConstant(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext &context,
	std::unordered_set<Expression *> &visiting
) {
	if (!expression)
		crashCompilerBug("compile-time constant classification received null expression");
	BindingFrameStack resolvedBindingFrameStack;
	Expression *resolvedExpression =
		resolveCompileTimeBindingForInference(expression, bindingFrameStack, &resolvedBindingFrameStack, &context);
	if (resolvedExpression && resolvedExpression != expression)
		return isStructurallyCompileTimeConstant(resolvedExpression, resolvedBindingFrameStack, context, visiting);
	if (isCompileTimeKnown(resolveImmediateCompileTimeValue(expression)))
		return true;
	if (expression->kind == Expression::Kind::Variable && expression->variable) {
		return context.currentInstantiation &&
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
	std::optional<std::int64_t> integerValue =
		getCompileTimeIntegerValue(resolveStoredCompileTimeValue(expression, bindingFrameStack, inferenceContext));
	if (!integerValue.has_value() || *integerValue < std::numeric_limits<int>::min() ||
		*integerValue > std::numeric_limits<int>::max()) {
		return false;
	}
	outValue = static_cast<int>(*integerValue);
	return true;
}

struct ScopedDiagnosticSuppression {
	InferenceContext &context;
	bool previous;

	explicit ScopedDiagnosticSuppression(InferenceContext &context) : context(context), previous(context.suppressDiagnostics) {
		context.suppressDiagnostics = true;
	}

	~ScopedDiagnosticSuppression() { context.suppressDiagnostics = previous; }
};
