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
	if (executeBodyCallSite && !context.staticLoopFrames.empty() && context.staticLoopFrames.back().required) {
		executeBodyCallSite->inferredCallerBody = context.cloneSectionBody(
			bodySection, instantiatedBody ? instantiatedBody->parentBody : nullptr,
			context.parseContext.storageScopeForInvocation(executeBodyCallSite)
		);
		instantiatedBody = executeBodyCallSite->inferredCallerBody.get();
	}
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
		executeBodyCallSite->sectionOutcome.write().kind =
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

#include "expression_inference_support.inl"

#include "expression_argument_inference.inl"
#include "expression_value_inference.inl"
#include "function_inference_intrinsics.inl"

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
	expr->inferredArgumentReference.reset();
	expr->memoryFacts = {};
	struct ExpressionTraceGuard {
		InferenceContext &context;
		explicit ExpressionTraceGuard(InferenceContext &context, Expression *expression) : context(context) {
			context.pushExpression(expression);
		}
		~ExpressionTraceGuard() { context.popExpression(); }
	} expressionTraceGuard(context, expr);
	ExpressionInferenceSupport support{expr, context, flexBindingFrameStack};
	bool typeQuery =
		expr->kind == Expression::Kind::IntrinsicCall && isTypeQueryIntrinsicKind(intrinsicKind(expr->intrinsicName));
	std::optional<InferenceExecutionSnapshot> argumentEffects;
	if (expr->kind == Expression::Kind::PatternCall || typeQuery)
		argumentEffects.emplace(context);
	InferredArgumentScope argumentScope(context);
	// The condition executes before its branches. Infer every branch for typing,
	// but retain execution effects only from reachable paths, as for sections.
	for (size_t i = 0; expr->kind != Expression::Kind::ArgumentPack && i < expr->arguments.size(); i++) {
		auto conditional = expr->kind == Expression::Kind::IntrinsicCall
							   ? conditionalOperands(intrinsicKind(expr->intrinsicName))
							   : std::nullopt;
		if (i == 2 && conditional && expr->arguments[1]->type.kind == DataType::Kind::Bool) {
			if (!support.inferConditionalArguments(i, *conditional, preserveCurrentGrouping, argumentScope))
				return;
			break;
		}
		if (!support.inferArgument(i, preserveCurrentGrouping, argumentScope))
			return;
	}
	if (typeQuery)
		argumentEffects->restore(context);

	switch (expr->kind) {
	case Expression::Kind::Literal:
		support.inferLiteral();
		break;

	case Expression::Kind::ArgumentPack:
		support.inferArgumentPack();
		break;

	case Expression::Kind::ArrayLiteral:
		if (!support.inferArrayLiteral())
			return;
		break;

	case Expression::Kind::Variable:
		if (!support.inferVariable())
			return;
		break;

	case Expression::Kind::TypedPlaceholder:
		if (expr->type.kind == DataType::Kind::Type)
			context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
		else
			context.setExpressionValue(expr, {});
		break;

	case Expression::Kind::IntrinsicCall:
		support.inferIntrinsic();
		break;

	case Expression::Kind::PatternCall:
		if (!inferPatternCall(
				expr, context, flexBindingFrameStack, preserveCurrentGrouping, argumentEffects ? &*argumentEffects : nullptr
			))
			return;
		break;

	case Expression::Kind::Pending:
		context.setExpressionValue(expr, {});
		break;
	}
	CompileTimeValue inferredValue = context.lookupExpressionValue(expr);
	if (!isCompileTimeKnown(inferredValue) && expr->type.kind == DataType::Kind::Type && !expr->inferredFlexExpansion)
		context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
	if (context.typesValid)
		recordExpressionMemoryFacts(expr, context, flexBindingFrameStack);
}

static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	inferOrderedExpression(expr, context, bindingFrameStack, true);
	return context.typesValid;
}
