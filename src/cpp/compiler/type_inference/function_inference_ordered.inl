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
	bool typeQuery = expr->kind == Expression::Kind::IntrinsicCall && isTypeQueryIntrinsicKind(intrinsicKind(expr->intrinsicName));
	std::optional<InferenceExecutionSnapshot> argumentEffects;
	if (expr->kind == Expression::Kind::PatternCall || typeQuery)
		argumentEffects.emplace(context);
	InferredArgumentScope argumentScope(context);
	auto inferArgument = [&](size_t index) {
		struct ReferenceTargetScope {
			InferenceContext &context;
			bool previous;
			~ReferenceTargetScope() { context.inferringReferenceAssignmentTarget = previous; }
		} targetScope{context, context.inferringReferenceAssignmentTarget};
		context.inferringReferenceAssignmentTarget = expr->kind == Expression::Kind::IntrinsicCall &&
													 intrinsicKind(expr->intrinsicName) == IntrinsicKind::Store && index == 1;
		Expression *arg = expr->arguments[index];
		std::optional<InferenceExecutionSnapshot> beforeArgument;
		if (expr->kind == Expression::Kind::PatternCall)
			beforeArgument.emplace(context);
		bool preserveArgumentGrouping =
			preserveCurrentGrouping && (!context.fixedGroupingRoots || context.fixedGroupingRoots->contains(arg));
		bool inferred = preserveArgumentGrouping ? inferExpressionWithCurrentGrouping(arg, context, flexBindingFrameStack)
												 : inferExpression(arg, context, false, flexBindingFrameStack);
		expr->arguments[index] = arg;
		if (inferred && beforeArgument && beforeArgument->matches(context))
			argumentScope.unchanged.emplace(arg, std::move(*beforeArgument));
		return inferred;
	};
	// The condition executes before its branches. Infer every branch for typing,
	// but retain execution effects only from reachable paths, as for sections.
	for (size_t i = 0; expr->kind != Expression::Kind::ArgumentPack && i < expr->arguments.size(); i++) {
		auto conditional = expr->kind == Expression::Kind::IntrinsicCall
							   ? conditionalOperands(intrinsicKind(expr->intrinsicName))
							   : std::nullopt;
		if (i == 2 && conditional && expr->arguments[1]->type.kind == DataType::Kind::Bool) {
			InferenceExecutionSnapshot branchInput(context);
			CompileTimeValue conditionValue =
				resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
			const bool *condition = std::get_if<bool>(&conditionValue);
			std::vector<KnownConstantState> values;
			std::vector<AddressInferenceState> addresses;
			std::vector<InferenceContext::SubjectState> subjects;
			auto rememberPath = [&] {
				values.push_back(context.currentVariableValues);
				addresses.push_back(context.currentAddressState);
				subjects.push_back(context.currentSubject);
			};
			if ((!condition && (!conditional->whenFalse || !conditional->whenTrue)) ||
				(condition && !conditional->selected(*condition)))
				rememberPath();
			for (size_t branch = i; branch < expr->arguments.size(); branch++) {
				branchInput.restoreState(context);
				InferenceExecutionSnapshot beforeBranch(context);
				if (!inferArgument(branch))
					return;
				if (!condition || conditional->selected(*condition) == branch)
					rememberPath();
				else
					beforeBranch.restore(context);
			}
			mergeExecutionStates(context, values, addresses, subjects);
			break;
		}
		if (!inferArgument(i))
			return;
	}
	if (typeQuery)
		argumentEffects->restore(context);

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		CompileTimeValue literalValue{};
		if (std::optional<NumericLiteralValue> numericValue = numericLiteralValueOf(*expr)) {
			// Shader pipelines default explicit float literals to f32 to avoid
			// introducing Float64 arithmetic from constants like 0.5 or 800.0.
			NumericLiteralTyping typing = typeNumericLiteral(context.parseContext, expr->range.line, *numericValue);
			if (typing.unrepresentableBy) {
				setConfiguredTypeFailure(
					expr->range, "number rule literal unrepresentable", "message",
					{{"rule", std::string(typing.unrepresentableBy->range.subString)}}
				);
				context.typeFailureRelatedInfo.push_back(
					{renderConfiguredMessage(
						 syntaxConfigForRange(context.parseContext, expr->range), "number rule literal unrepresentable",
						 "related rule"
					 ),
					 typing.unrepresentableBy->range}
				);
				break;
			}
			// A literal's typing does not depend on how its line is grouped, so trials record it too.
			if (typing.widenedBy) {
				context.parseContext.scopeRuleWarnings.push_back(
					{"number rule literal widened",
					 expr->range,
					 typing.widenedBy->range,
					 {{"rule", std::string(typing.widenedBy->range.subString)}, {"type", typeToUserName(typing.type)}}}
				);
			}
			if (typing.contradicted) {
				context.parseContext.scopeRuleWarnings.push_back(
					{"number rule contradicted",
					 expr->range,
					 typing.contradicted->range,
					 {{"explicit", std::string(expr->range.subString)},
					  {"rule", std::string(typing.contradicted->range.subString)}}}
				);
			}
			expr->type = typing.type;
			literalValue = typing.value;
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {DataType::Kind::Int, 1};
			expr->type.pointerDepth = 1;
			literalValue = std::get<std::string>(expr->literalValue);
		}
		context.setExpressionValue(expr, literalValue);
		break;
	}

	case Expression::Kind::ArgumentPack: {
		ArgumentPackValue pack;
		for (size_t index = 0; index < expr->arguments.size(); index++) {
			Expression *argument = expr->arguments[index];
			CompileTimeValue forwarded = resolveStoredCompileTimeValue(argument, flexBindingFrameStack, &context);
			if (expr->arguments.size() == 1 && std::holds_alternative<ArgumentPackValue>(forwarded))
				pack = std::get<ArgumentPackValue>(std::move(forwarded));
			else
				pack.elements.push_back(captureExpression(context.parseContext, expr, index, flexBindingFrameStack));
		}
		expr->type = {DataType::Kind::ArgumentPack};
		expr->type.arraySize = static_cast<int>(pack.elements.size());
		context.setExpressionValue(expr, std::move(pack));
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
				bool reusable = flexBinding->type.isDeduced() && context.inferredArguments->reusable(flexBinding);
				if (!reusable) {
					// A binding belongs to the caller, outside this expression's
					// grouping snapshot. Rejected trials must restore its selected
					// expansion together with the expansion's compile-time values.
					if (context.trial)
						context.trialJournal->recordBorrowedExpressionWrite(flexBinding);
					bool preserveBindingGrouping =
						flexBinding->type.isDeduced() ||
						(context.fixedGroupingRoots && context.fixedGroupingRoots->contains(flexBinding));
					bool inferred =
						inferExpression(flexBinding, context, preserveBindingGrouping, resolvedBinding.bindingFrameStack);
					if (!inferred)
						return;
				}
				DataType boundType = ensureExpressionType(flexBinding, context, resolvedBinding.bindingFrameStack);
				if (boundType.isDeduced())
					expr->type = boundType;
				expr->inferredArgumentReference = flexBinding->inferredArgumentReference;
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
			if (var->dataInitializer && !var->type.isDeduced()) {
				Expression *initializer = var->dataInitializer;
				Section *definition = initializer->patternMatch->matchingDefinitions.front()->section;
				if (definition->dataSection && definition->dataSection->resultType) {
					Expression *resultType = definition->dataSection->resultType;
					ensureExpressionType(resultType, context, flexBindingFrameStack);
					auto type = getCompileTimeTypeReferenceValue(context.lookupExpressionValue(resultType));
					if (!type) {
						context.fail(
							buildFailureDetailDiagnostic(resultType->range, "A data section result must be a compile-time type")
						);
						return;
					}
					if (context.trial && context.trialJournal)
						context.trialJournal->recordVariableWrite(var);
					var->type = type->type.toReferencedType();
				}
			}
			if (context.currentInstantiation && var->isGlobal && !var->dataInitializer &&
				!expressionIsLValueOnlyUse(context, expr))
				markCurrentInstantiationImpure(context);
			restoreRefinedVariableType(expr, var, context);
			if (var->type.isDeduced())
				expr->type = var->type;
			else if (var->declaredType.isDeduced() && expressionIsLValueOnlyUse(context, expr))
				expr->type = var->declaredType;
		}
		context.setExpressionValue(expr, inferVariableCompileTimeValue(expr, context, flexBindingFrameStack));
		if (expr->type.kind == DataType::Kind::ArgumentReference && !context.inferringReferenceAssignmentTarget) {
			CompileTimeValue stored = context.lookupExpressionValue(expr);
			const auto *capture = std::get_if<ExpressionCapture>(&stored);
			requireCompilerInvariant(capture && capture->data, "argument reference variable lost its capture");
			expr->inferredArgumentReference = *capture;
			Expression *&referenced = capture->data->expression();
			if (!inferExpression(referenced, context, referenced->type.isDeduced(), capture->data->bindings))
				return;
			expr->type = referenced->type;
			context.setExpressionValue(expr, context.lookupExpressionValue(referenced));
		}
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
