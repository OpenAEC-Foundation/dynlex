// Infer types for a section's code lines with operand reordering. Returns false on failure.
static bool inferSection(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack = BindingFrameStack{}, bool *fallsThrough = nullptr
);

static bool inferSectionFlexCallerBodyFrame(
	InferenceContext::SectionFlexBodyInferenceFrame &targetFrame, Section *executionSection, Expression *executeBodyCallSite,
	InferenceContext &context
) {
	requireCompilerInvariant(
		targetFrame.definitionSection && targetFrame.definitionBody && targetFrame.bodySection,
		"section flex body inference frame is incomplete"
	);
	if (targetFrame.bodyInferred && targetFrame.executeBodyCallSite != executeBodyCallSite) {
		context.fail(Diagnostic(
			context.parseContext, Diagnostic::Level::Error, "execute body can only run once per section flex call",
			executeBodyCallSite ? executeBodyCallSite->range : Range()
		));
		return false;
	}

	targetFrame.bodyInferred = true;
	targetFrame.executeBodyCallSite = executeBodyCallSite;
	Section *bodySection = targetFrame.bodySection;
	InstantiatedSectionBody *instantiatedBody = targetFrame.instantiatedBody;
	BindingFrameStack callerBindings = targetFrame.callerBindings;
	pushSectionFlexCallerVariableBindings(
		callerBindings, targetFrame.definitionSection, targetFrame.definitionBody, executionSection, bodySection
	);
	std::vector<std::pair<Section *, bool>> activeDefinitionStates;
	activeDefinitionStates.reserve(context.activeFlexDefinitionStack.size());
	for (Section *activeDefinition : context.activeFlexDefinitionStack) {
		if (!activeDefinition)
			continue;
		activeDefinitionStates.push_back({activeDefinition, activeDefinition->inferring});
		activeDefinition->inferring = false;
	}
	bool callerBodyFallsThrough = true;
	bool inferred = inferSection(bodySection, instantiatedBody, nullptr, context, callerBindings, &callerBodyFallsThrough);
	targetFrame.bodyFallsThrough = callerBodyFallsThrough;
	if (inferred && executeBodyCallSite)
		executeBodyCallSite->sectionOutcome.kind =
			callerBodyFallsThrough ? Expression::SectionOutcome::Kind::None : Expression::SectionOutcome::Kind::FunctionReturn;
	for (const auto &[activeDefinition, wasInferring] : activeDefinitionStates)
		activeDefinition->inferring = wasInferring;
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
	Expression *executeBodyCallSite =
		context.activeFlexCallStack.empty() ? executeBodyExpression : context.activeFlexCallStack.back();
	return inferSectionFlexCallerBodyFrame(*targetFrame, executionSection, executeBodyCallSite, context);
}

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
		return resolveThroughBindings(expression, flexBindingFrameStack);
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
		if (std::holds_alternative<double>(expr->literalValue)) {
			double value = std::get<double>(expr->literalValue);
			std::string_view literalText = expr->range.subString;
			bool explicitlyFloat = literalText.find('.') != std::string_view::npos ||
								   literalText.find('e') != std::string_view::npos ||
								   literalText.find('E') != std::string_view::npos;
			if (!explicitlyFloat && std::trunc(value) == value) {
				expr->type = {DataType::Kind::Int, 4};
			} else {
				// Shader pipelines default explicit float literals to f32 to avoid
				// introducing Float64 arithmetic from constants like 0.5 or 800.0.
				expr->type = defaultFloatType(context.parseContext.options.emitSPIRV);
			}
			if (expr->type.kind == DataType::Kind::Bool)
				literalValue = value != 0.0;
			else
				literalValue = value;
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
			Expression *flexBinding = flexBindingFrameStack.lookup(expr->variable);
			if (flexBinding) {
				CompileTimeValue boundValue = resolveStoredCompileTimeValue(flexBinding, flexBindingFrameStack, &context);
				bool requiresInference =
					!flexBinding->type.isDeduced() || (flexBinding->type.isMetaType() && !isCompileTimeKnown(boundValue));
				if (requiresInference && flexBinding != expr) {
					bool preserveBindingGrouping =
						context.fixedGroupingRoots && context.fixedGroupingRoots->contains(flexBinding);
					bool inferred = preserveBindingGrouping
										? inferExpressionWithCurrentGrouping(flexBinding, context, flexBindingFrameStack)
										: inferExpression(flexBinding, context, false, flexBindingFrameStack);
					if (!inferred)
						return;
				}
				DataType boundType = ensureExpressionType(flexBinding, context, flexBindingFrameStack);
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
			Variable *var = sec ? sec->findVariable(varName) : nullptr;
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
