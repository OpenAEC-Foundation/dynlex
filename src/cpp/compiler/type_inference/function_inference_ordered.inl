// Infer types for a section's code lines with operand reordering. Returns false on failure.
static bool inferSection(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack = BindingFrameStack{}, bool *fallsThrough = nullptr
);

static bool inferSectionFlexCallerBodyFrame(
	size_t targetFrameIndex, Section *executionSection, Expression *executeBodyCallSite, InferenceContext &context
) {
	requireCompilerInvariant(
		targetFrameIndex < context.sectionFlexBodyFrames.size(), "section flex body inference frame is not active"
	);
	InferenceContext::SectionFlexBodyInferenceFrame &targetFrame = context.sectionFlexBodyFrames[targetFrameIndex];
	requireCompilerInvariant(
		targetFrame.definitionSection && targetFrame.definitionBody && targetFrame.bodySection,
		"section flex body inference frame is incomplete"
	);
	std::vector<Expression *> invocationPath = context.activeFlexCallStack;
	invocationPath.push_back(executeBodyCallSite);
	ExpressionInvocationIdentity transferIdentity = expressionInvocationIdentity(invocationPath);
	if (targetFrame.bodyInferred) {
		requireCompilerInvariant(
			targetFrame.bodyTransferIdentity.has_value(), "inferred section flex body has no transfer identity"
		);
	}
	if (targetFrame.bodyInferred && *targetFrame.bodyTransferIdentity != transferIdentity) {
		size_t differingComponent = 0;
		const std::vector<const Expression *> &previousPath = targetFrame.bodyTransferIdentity->path;
		while (differingComponent < previousPath.size() && differingComponent < transferIdentity.path.size() &&
			   previousPath[differingComponent] == transferIdentity.path[differingComponent]) {
			differingComponent++;
		}
		Expression *diagnosticExpression =
			differingComponent < invocationPath.size() ? invocationPath[differingComponent] : executeBodyCallSite;
		context.fail(Diagnostic(
			context.parseContext, Diagnostic::Level::Error, "execute body can only run once per section flex call",
			diagnosticExpression ? diagnosticExpression->range : Range()
		));
		return false;
	}

	targetFrame.bodyInferred = true;
	targetFrame.executeBodyCallSite = executeBodyCallSite;
	targetFrame.bodyTransferIdentity = std::move(transferIdentity);
	Section *definitionSection = targetFrame.definitionSection;
	Section *bodySection = targetFrame.bodySection;
	InstantiatedSectionBody *instantiatedBody = targetFrame.instantiatedBody;
	BindingFrameStack callerBindings = targetFrame.callerBindings;
	pushSectionFlexCallerVariableBindings(
		callerBindings, targetFrame.definitionSection, targetFrame.definitionBody, executionSection, bodySection
	);
	requireCompilerInvariant(
		context.activeFlexDefinitionStack.size() == context.activeFlexExpansionKeys.size(),
		"active flex definitions and expansion keys diverged before caller body inference"
	);
	auto targetDefinition =
		std::find(context.activeFlexDefinitionStack.rbegin(), context.activeFlexDefinitionStack.rend(), definitionSection);
	requireCompilerInvariant(
		targetDefinition != context.activeFlexDefinitionStack.rend(), "section flex caller body has no active definition"
	);
	size_t targetExpansionKeyIndex =
		context.activeFlexDefinitionStack.size() - 1 -
		static_cast<size_t>(std::distance(context.activeFlexDefinitionStack.rbegin(), targetDefinition));
	size_t activeExpansionKeyCount = context.activeFlexExpansionKeys.size();
	std::vector<std::optional<FlexExpansionKey>> suspendedExpansionKeys;
	suspendedExpansionKeys.reserve(activeExpansionKeyCount - targetExpansionKeyIndex);
	for (size_t keyIndex = targetExpansionKeyIndex; keyIndex < activeExpansionKeyCount; keyIndex++) {
		suspendedExpansionKeys.push_back(std::move(context.activeFlexExpansionKeys[keyIndex]));
		context.activeFlexExpansionKeys[keyIndex].reset();
	}
	bool callerBodyFallsThrough = true;
	bool inferred = inferSection(bodySection, instantiatedBody, nullptr, context, callerBindings, &callerBodyFallsThrough);
	requireCompilerInvariant(
		targetFrameIndex < context.sectionFlexBodyFrames.size() &&
			context.sectionFlexBodyFrames[targetFrameIndex].definitionSection == definitionSection,
		"section flex body inference frame stack changed while inferring its body"
	);
	context.sectionFlexBodyFrames[targetFrameIndex].bodyFallsThrough = callerBodyFallsThrough;
	if (inferred && executeBodyCallSite)
		executeBodyCallSite->sectionOutcome.kind =
			callerBodyFallsThrough ? Expression::SectionOutcome::Kind::None : Expression::SectionOutcome::Kind::FunctionReturn;
	requireCompilerInvariant(
		context.activeFlexDefinitionStack.size() == activeExpansionKeyCount &&
			context.activeFlexExpansionKeys.size() == activeExpansionKeyCount,
		"caller body inference left an unbalanced active flex expansion"
	);
	for (size_t offset = 0; offset < suspendedExpansionKeys.size(); offset++)
		context.activeFlexExpansionKeys[targetExpansionKeyIndex + offset] = std::move(suspendedExpansionKeys[offset]);
	return inferred;
}

static bool inferSectionFlexCallerBody(Expression *executeBodyExpression, InferenceContext &context) {
	Section *callSection =
		executeBodyExpression && executeBodyExpression->range.line ? executeBodyExpression->range.line->section : nullptr;
	if (!callSection)
		crashCompilerBug("execute body inference is missing source section context");
	Section *executionSection = nullptr;
	InferenceContext::SectionFlexBodyInferenceFrame *targetFrame = resolveSectionFlexBodyFrame(
		context.sectionFlexBodyFrames, callSection, context.flexCallSiteSectionStack, context.activeFlexDefinitionStack,
		&executionSection
	);
	if (!targetFrame || !targetFrame->bodySection) {
		context.fail(Diagnostic(
			context.parseContext, Diagnostic::Level::Error, "execute body has no matching section flex body",
			executeBodyExpression->range
		));
		return false;
	}
	size_t targetFrameIndex = static_cast<size_t>(targetFrame - context.sectionFlexBodyFrames.data());
	return inferSectionFlexCallerBodyFrame(targetFrameIndex, executionSection, executeBodyExpression, context);
}

#include "function_inference_pattern_calls.inl"
#include "intrinsics/shader_runtime_inference.inl"

// Infer the type of an expression bottom-up.
// Sets context.typesValid = false if types are invalid for this grouping.
static void inferOrderedExpression(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack = BindingFrameStack{},
	bool preserveCurrentGrouping = false
) {
	context.typesValid = true;
	if (!expr)
		return;
	context.setExpressionValue(expr, {});
	struct ExpressionTraceGuard {
		InferenceContext &context;
		explicit ExpressionTraceGuard(InferenceContext &context, Expression *expression) : context(context) {
			context.pushExpression(expression);
		}
		~ExpressionTraceGuard() { context.popExpression(); }
	} expressionTraceGuard(context, expr);
	auto resolveThroughFlexBindings = [&](Expression *expression) {
		return resolveInferenceBindingLayers(expression, flexBindingFrameStack, &context).expression;
	};
	auto setConfiguredTypeFailure = [&](Range range, std::string_view key, std::string_view variant = "message",
										std::vector<std::pair<std::string, std::string>> replacements = {}) {
		context.setTypeFailure(
			renderConfiguredMessage(syntaxConfigForRange(context.parseContext, range), key, variant, replacements)
		);
	};
	auto failWithDetail = [&](Range range, const std::string &detail, int priority = 1) {
		context.setTypeFailure(detail);
		context.fail(buildFailureDetailDiagnostic(range, detail), priority);
	};
	auto failConfigured = [&](Range range, std::string_view key, std::string_view variant = "message",
							  std::vector<std::pair<std::string, std::string>> replacements = {}) {
		failWithDetail(
			range, renderConfiguredMessage(syntaxConfigForRange(context.parseContext, range), key, variant, replacements)
		);
	};
	auto failIntrinsicArgumentRequirement = [&](size_t argumentIndex, std::string_view requirement) {
		if (argumentIndex >= expr->arguments.size() || !expr->arguments[argumentIndex])
			crashCompilerBug("intrinsic argument validation encountered a missing argument expression");
		std::string detail = "Intrinsic '" + expr->intrinsicName + "' argument " + std::to_string(argumentIndex) + " must be " +
							 std::string(requirement);
		failWithDetail(expr->arguments[argumentIndex]->range, detail, 0);
	};
	auto failCompileTimeOnlyIntrinsicArgument = [&](size_t argumentIndex, std::string_view requirement) {
		failIntrinsicArgumentRequirement(argumentIndex, requirement);
	};
	// Recurse into arguments first (bottom-up)
	for (size_t i = 0; i < expr->arguments.size(); i++) {
		Expression *arg = expr->arguments[i];
		bool preserveArgumentGrouping =
			preserveCurrentGrouping && (!context.fixedGroupingRoots || context.fixedGroupingRoots->contains(arg));
		bool inferred = preserveArgumentGrouping ? inferExpressionWithCurrentGrouping(arg, context, flexBindingFrameStack)
												 : inferExpression(arg, context, false, flexBindingFrameStack);
		if (!inferred)
			return;
		expr->arguments[i] = arg;
	}

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		CompileTimeValue literalValue{};
		if (const auto *integer = std::get_if<std::int64_t>(&expr->literalValue)) {
			NumericLiteralValue numericValue{*integer};
			expr->type = numericLiteralType(numericValue, context.parseContext.options.emitSPIRV);
			literalValue = numericLiteralCompileTimeValue(numericValue);
		} else if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&expr->literalValue)) {
			NumericLiteralValue numericValue{*minimumMagnitude};
			expr->type = numericLiteralType(numericValue, context.parseContext.options.emitSPIRV);
			literalValue = numericLiteralCompileTimeValue(numericValue);
		} else if (const auto *floatingPoint = std::get_if<double>(&expr->literalValue)) {
			NumericLiteralValue numericValue{*floatingPoint};
			// Shader pipelines default explicit float literals to f32 to avoid
			// introducing Float64 arithmetic from constants like 0.5 or 800.0.
			expr->type = numericLiteralType(numericValue, context.parseContext.options.emitSPIRV);
			literalValue = numericLiteralCompileTimeValue(numericValue);
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {DataType::Kind::Int, 1};
			expr->type.pointerDepth = 1;
			literalValue = std::get<std::string>(expr->literalValue);
		}
		context.setExpressionValue(expr, literalValue);
		break;
	}

	case Expression::Kind::ArrayLiteral: {
		context.setExpressionValue(expr, {});
		if (expr->arguments.empty())
			break;
		DataType elementType = ensureExpressionType(expr->arguments[0], context, flexBindingFrameStack);
		if (!elementType.isDeduced())
			break;
		for (size_t i = 1; i < expr->arguments.size(); i++) {
			DataType nextType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
			DataType merged;
			if (!mergeArrayElementType(elementType, nextType, merged)) {
				context.typesValid = false;
				setConfiguredTypeFailure(expr->range, "array literal element type mismatch");
				return;
			}
			elementType = merged;
		}
		expr->type = DataType(DataType::Kind::Array);
		expr->type.arraySize = static_cast<int>(expr->arguments.size());
		expr->type.arrayElementType = std::make_shared<DataType>(elementType);
		break;
	}

	case Expression::Kind::Variable: {
		if (expr->variable) {
			std::string varName = expr->variable->name;
			// Check flex bindings first
			ResolvedBindingLayers resolvedBinding = resolveVariableBindingWithCallerScope(expr, flexBindingFrameStack);
			Expression *flexBinding = resolvedBinding.expression;
			if (flexBinding && flexBinding != expr) {
				CompileTimeValue boundValue =
					resolveStoredCompileTimeValue(flexBinding, resolvedBinding.bindingFrameStack, &context);
				bool requiresInference =
					!flexBinding->type.isDeduced() || (flexBinding->type.isMetaType() && !isCompileTimeKnown(boundValue));
				if (requiresInference && flexBinding != expr) {
					bool preserveBindingGrouping =
						context.fixedGroupingRoots && context.fixedGroupingRoots->contains(flexBinding);
					bool inferred =
						preserveBindingGrouping
							? inferExpressionWithCurrentGrouping(flexBinding, context, resolvedBinding.bindingFrameStack)
							: inferExpression(flexBinding, context, false, resolvedBinding.bindingFrameStack);
					if (!inferred)
						return;
				}
				DataType boundType = ensureExpressionType(flexBinding, context, resolvedBinding.bindingFrameStack);
				if (boundType.isDeduced())
					expr->type = boundType;
				CompileTimeValue variableValue = inferVariableCompileTimeValue(expr, context, flexBindingFrameStack);
				if (!isCompileTimeKnown(variableValue) && expr->type.kind == DataType::Kind::Type)
					variableValue = TypeReferenceValue::exact(expr->type);
				context.setExpressionValue(expr, variableValue);
				break;
			}
			// Look up variable in scope
			Section *sec = expr->range.line ? expr->range.line->section : nullptr;
			Variable *var = sec ? sec->findVariable(expr->variable) : nullptr;
			if (!var) {
				context.typesValid = false;
				context.setTypeFailure(renderConfiguredMessage(
					syntaxConfigForRange(context.parseContext, expr->range), "unknown variable", "message", {{"name", varName}}
				));
				if (!context.trial)
					context.addDiagnosticWithCurrentTrace(Diagnostic(
						context.parseContext, Diagnostic::Level::Error, "unknown variable", expr->range, "name", varName
					));
				return;
			}
			if (context.currentInstantiation && var->isGlobal && !expressionIsLValueOnlyUse(context, expr))
				markCurrentInstantiationImpure(context);
			if (var->type.isDeduced())
				expr->type = var->type;
			else if (var->declaredType.isDeduced() && expressionIsLValueOnlyUse(context, expr))
				expr->type = var->declaredType;
		}
		context.setExpressionValue(expr, inferVariableCompileTimeValue(expr, context, flexBindingFrameStack));
		break;
	}

	case Expression::Kind::TypedPlaceholder:
		if (expr->type.kind == DataType::Kind::Type)
			context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
		else
			context.setExpressionValue(expr, {});
		break;

#include "function_inference_intrinsics.inl"
#include "function_inference_patterns.inl"
