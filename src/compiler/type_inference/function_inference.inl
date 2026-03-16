#pragma once

#include "compilerUtils.h"
#include "const_evaluation.inl"
#include "type_resolution.inl"

static void resetExpressionTypes(Expression *expr);
static void resetSectionExpressionTypes(Section *section);
static void recomputeRanges(Expression *expr);
static bool startsWithArgument(Expression *expression);
static bool endsWithArgument(Expression *expression);

struct InstantiationProgressSnapshot {
	DataType returnType;
	std::vector<DataType> parameterTypes;
	std::unordered_map<std::string, CompileTimeValue> constantParameterValues;
	std::unordered_map<VariableReference *, CompileTimeValue> constantValuesByReference;
	std::unordered_map<Expression *, PatternDefinition *> selectedOverloadsByCall;
	std::unordered_set<std::string> requiredCompileTimeParameters;

	bool operator==(const InstantiationProgressSnapshot &other) const = default;
};

static InstantiationProgressSnapshot snapshotInstantiationProgress(const Instantiation &instantiation) {
	return {
		instantiation.returnType,
		instantiation.parameterTypes,
		instantiation.constantParameterValues,
		instantiation.constantValuesByReference,
		instantiation.selectedOverloadsByCall,
		instantiation.requiredCompileTimeParameters,
	};
}

static void setRecursiveInferenceFailure(
	InferenceContext &context, PatternDefinition *definition, const Range &fallbackRange, std::string functionName
) {
	Range diagnosticRange = definition ? definition->range : fallbackRange;
	if (functionName.empty())
		functionName = definition ? (std::string)definition->range.subString : "<expression>";
	context.setTypeFailure(renderConfiguredMessage(
		syntaxConfigForRange(context.parseContext, diagnosticRange), "recursive type inference did not converge",
		"message", {{"function", functionName}}
	));
}

template <typename InferPassFn>
static bool runInstantiationReinferenceLoop(
	InferenceContext &context, Instantiation &instantiation, PatternDefinition *definition, const Range &fallbackRange,
	std::string functionName, InferPassFn &&inferPass
) {
	size_t reinferPass = 0;
	constexpr size_t maxReinferPasses = 32;
	while (true) {
		InstantiationProgressSnapshot beforePass = snapshotInstantiationProgress(instantiation);
		bool inferenceSucceeded = inferPass();
		if (!inferenceSucceeded || !context.typesValid)
			return false;
		if (!instantiation.needsReinfer)
			return true;
		InstantiationProgressSnapshot afterPass = snapshotInstantiationProgress(instantiation);
		if (afterPass == beforePass) {
			setRecursiveInferenceFailure(context, definition, fallbackRange, std::move(functionName));
			return false;
		}
		reinferPass++;
		if (reinferPass >= maxReinferPasses) {
			setRecursiveInferenceFailure(context, definition, fallbackRange, std::move(functionName));
			return false;
		}
		context.typesValid = true;
		context.typeFailureDetail.clear();
	}
}

static Expression *
literalExpressionFromCompileTimeValue(const CompileTimeValue &value, std::vector<Expression *> &ownedLiterals) {
	Expression *literal = new Expression();
	literal->kind = Expression::Kind::Literal;
	if (const auto *number = std::get_if<double>(&value)) {
		literal->literalValue = *number;
	} else if (const auto *text = std::get_if<std::string>(&value)) {
		literal->literalValue = *text;
	} else {
		delete literal;
		return nullptr;
	}
	ownedLiterals.push_back(literal);
	return literal;
}

static CompileTimeValue evaluateCompileTimeValueWithKnownState(
	Expression *expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	BindingFrameStack evalBindingFrameStack = bindingFrameStack;
	BindingMap knownConstantBindings;
	std::vector<Expression *> ownedLiterals;
	for (const auto &[reference, value] : context.currentKnownConstants) {
		if (!reference || !isCompileTimeKnown(value) || evalBindingFrameStack.lookup(reference->name))
			continue;
		Expression *literal = literalExpressionFromCompileTimeValue(value, ownedLiterals);
		if (literal)
			knownConstantBindings[reference->name] = literal;
	}
	if (!knownConstantBindings.empty())
		evalBindingFrameStack.pushFrame(std::move(knownConstantBindings));
	CompileTimeValue result =
		evaluateCompileTimeValue(expr, context.parseContext, evalBindingFrameStack, context.currentInstantiation);
	for (Expression *literal : ownedLiterals)
		delete literal;
	return result;
}

static void rollbackTrialJournal(InferenceContext::TrialJournal &journal) {
	for (Section *section : journal.touchedSections)
		resetSectionExpressionTypes(section);
	for (auto it = journal.variableTypeUndo.rbegin(); it != journal.variableTypeUndo.rend(); ++it) {
		it->variable->type = it->type;
		it->variable->typeOriginRange = it->typeOriginRange;
		it->variable->typeOriginFloatLiteralReplacement = it->typeOriginFloatLiteralReplacement;
	}
	for (auto it = journal.sectionInstantiationUndo.rbegin(); it != journal.sectionInstantiationUndo.rend(); ++it) {
		if (it->existed)
			it->section->instantiations[it->argTypes] = it->value;
		else
			it->section->instantiations.erase(it->argTypes);
	}
	for (auto it = journal.classInstantiationSizes.rbegin(); it != journal.classInstantiationSizes.rend(); ++it)
		it->first->instantiations.resize(it->second);
}

static Expression *cloneExpressionTree(Expression *expr) {
	if (!expr)
		return nullptr;
	Expression *clone = new Expression(*expr);
	clone->arguments.clear();
	for (Expression *arg : expr->arguments)
		clone->arguments.push_back(cloneExpressionTree(arg));
	return clone;
}

static void deleteExpressionTree(Expression *expr) {
	if (!expr)
		return;
	for (Expression *arg : expr->arguments)
		deleteExpressionTree(arg);
	delete expr;
}

static std::unique_ptr<Expression>
makeNonMacroParameterBinding(const std::string &parameterName, const DataType &argType, const Instantiation &instantiation) {
	if (!instantiation.requiredCompileTimeParameters.contains(parameterName)) {
		auto typedBinding = std::make_unique<Expression>();
		typedBinding->type = argType;
		return typedBinding;
	}

	auto constIt = instantiation.constantParameterValues.find(parameterName);
	if (constIt != instantiation.constantParameterValues.end() && isCompileTimeKnown(constIt->second)) {
		const CompileTimeValue &constantValue = constIt->second;
		auto literalBinding = std::make_unique<Expression>();
		literalBinding->kind = Expression::Kind::Literal;
		if (const auto *number = std::get_if<double>(&constantValue))
			literalBinding->literalValue = *number;
		else if (const auto *text = std::get_if<std::string>(&constantValue))
			literalBinding->literalValue = *text;
		else if (const auto *boolean = std::get_if<bool>(&constantValue))
			literalBinding->literalValue = *boolean ? 1.0 : 0.0;
		literalBinding->type = argType;
		return literalBinding;
	}

	auto typedBinding = std::make_unique<Expression>();
	typedBinding->type = argType;
	return typedBinding;
}

static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack
);
static bool inferSection(Section *section, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
static DataType derivePatternCallType(Expression *expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
static DataType
inferExpressionTypeWithoutSideEffects(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
static DataType ensureExpressionType(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);

static void snapshotExpressionVariableReferences(Expression *expr, InferenceContext &context) {
	if (!expr)
		return;
	for (Expression *arg : expr->arguments)
		snapshotExpressionVariableReferences(arg, context);
	if (expr->kind == Expression::Kind::Variable && expr->variable)
		context.snapshotReferenceConstant(expr->variable);
}

static void recordSelectedOverloadsForInstantiation(Expression *expr, Instantiation *instantiation) {
	if (!expr || !instantiation)
		return;
	if (expr->kind == Expression::Kind::PatternCall && expr->selectedPatternDefinition)
		instantiation->selectedOverloadsByCall[expr] = expr->selectedPatternDefinition;
	for (Expression *arg : expr->arguments)
		recordSelectedOverloadsForInstantiation(arg, instantiation);
}

struct ArgumentTypeInferenceResult {
	DataType type;
	bool deferred = false;
};

static ArgumentTypeInferenceResult ensureArgumentTypeForPatternCall(
	Expression *argExpr, InferenceContext &context, const BindingFrameStack &callerBindingFrameStack
) {
	bool savedObservedInProgress = context.observedInProgressUndeducedInstantiation;
	context.observedInProgressUndeducedInstantiation = false;
	Expression *inferExpr = argExpr;
	(void)inferExpression(inferExpr, context, false, callerBindingFrameStack);
	ArgumentTypeInferenceResult result;
	result.type = ensureExpressionType(argExpr, context, callerBindingFrameStack);
	if (!result.type.isDeduced())
		result.type = derivePatternCallType(argExpr, context, callerBindingFrameStack);
	if (!result.type.isDeduced())
		result.type = resolveTypeThroughBindings(argExpr, callerBindingFrameStack);
	bool observedInProgress = context.observedInProgressUndeducedInstantiation;
	context.observedInProgressUndeducedInstantiation = savedObservedInProgress || observedInProgress;
	result.deferred = !result.type.isDeduced() && observedInProgress;
	return result;
}

static void resetSectionExpressionTypes(Section *section) {
	if (!section)
		return;
	for (CodeLine *line : section->codeLines) {
		if (line->expression)
			resetExpressionTypes(line->expression);
	}
}

static void resetSectionLocalVariableTypes(Section *section) {
	if (!section)
		return;
	for (auto &[name, variable] : section->variables) {
		if (!variable || variable->isGlobal)
			continue;
		variable->type = {};
		variable->typeOriginRange = {};
		variable->typeOriginFloatLiteralReplacement.clear();
	}
}

static DataType derivePatternCallType(Expression *expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return {};

	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return {};

	std::vector<DataType> argTypesForOverload;
	for (Expression *&arg : expr->arguments)
		argTypesForOverload.push_back(inferExpressionTypeWithoutSideEffects(arg, context, bindingFrameStack));

	PatternDefinition *def = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload);
	if (!def || !def->section)
		return {};

	Section *matchedSection = def->section;
	BindingMap callBindings;
	appendPatternCallBindings(expr, def, callBindings);
	for (auto &[name, boundExpr] : callBindings) {
		Expression *resolvedExpr = resolveThroughBindings(boundExpr, bindingFrameStack);
		if (resolvedExpr)
			boundExpr = resolvedExpr;
	}
	BindingFrameStack callBindingFrameStack = bindingFrameStack;
	pushBindingScope(callBindingFrameStack, callBindings);

	if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
		auto *classSec = static_cast<ClassSection *>(matchedSection);
		return instantiateBoundClassType(context.parseContext, classSec->classDefinition, callBindingFrameStack);
	}

	if (matchedSection->isMacro) {
		if (!matchedSection->inferring) {
			matchedSection->inferring = true;
			ScopedDiagnosticSuppression suppressDiagnostics(context);
			inferSection(matchedSection, context, callBindingFrameStack);
			matchedSection->inferring = false;
		}
		for (Section *child : matchedSection->children) {
			for (CodeLine *line : child->codeLines) {
				if (!line->expression)
					continue;
				DataType resolvedType = resolveTypeThroughBindings(line->expression, callBindingFrameStack);
				if (resolvedType.isDeduced())
					return resolvedType;
			}
		}
		return {};
	}

	std::vector<DataType> argTypes;
	std::vector<std::pair<std::string, Expression *>> orderedBindings;
	collectPatternCallBindingPairs(expr, def, orderedBindings);
	for (const auto &[parameterName, ignoredArgumentExpression] : orderedBindings) {
		Expression *&argExpr = callBindings[parameterName];
		(void)ignoredArgumentExpression;
		DataType argType = inferExpressionTypeWithoutSideEffects(argExpr, context, callBindingFrameStack);
		if (!argType.isDeduced())
			return {};
		argTypes.push_back(argType);
	}

	if (!ensureSectionInstantiationInferred(
			context.parseContext, matchedSection, callBindingFrameStack, argTypes, context.currentInstantiation
		))
		return {};

	auto instIt = matchedSection->instantiations.find(argTypes);
	if (instIt != matchedSection->instantiations.end() && instIt->second.returnType.isDeduced())
		return instIt->second.returnType;

	return {};
}

// Probe an expression's type without committing inference side effects or surfacing
// nested diagnostics. This is used by overload resolution and logical/operator
// checks where we only need the resulting type.
static DataType inferExpressionTypeWithoutSideEffects(
	Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	static thread_local std::unordered_set<const Expression *> activeTypeProbes;
	Expression *targetExpr = expr;
	BindingFrameStack targetBindingFrameStack = bindingFrameStack;
	BindingFrameStack effectiveBindingFrameStack;
	Expression *resolvedExpr = resolveThroughBindingsDeep(expr, targetBindingFrameStack, effectiveBindingFrameStack);
	if (resolvedExpr) {
		targetExpr = resolvedExpr;
		targetBindingFrameStack = std::move(effectiveBindingFrameStack);
	}
	bool bindingDependentProbe = (targetExpr != expr) || targetBindingFrameStack.hasBindings();
	if (targetExpr && targetExpr->kind == Expression::Kind::IntrinsicCall &&
		intrinsicKind(targetExpr->intrinsicName) == IntrinsicKind::Select) {
		Expression *activeBranch =
			selectCompileTimeBranch(targetExpr, context.parseContext, targetBindingFrameStack, context.currentInstantiation);
		if (activeBranch) {
			DataType activeType = inferExpressionTypeWithoutSideEffects(activeBranch, context, targetBindingFrameStack);
			if (activeType.isDeduced()) {
				if (!bindingDependentProbe)
					targetExpr->type = activeType;
				return activeType;
			}
		}
	}

	DataType type = resolveTypeThroughBindings(targetExpr, targetBindingFrameStack);
	if (type.isDeduced())
		return type;
	if (!targetExpr)
		return {};
	if (activeTypeProbes.contains(targetExpr))
		return type;

	struct ActiveTypeProbeGuard {
		std::unordered_set<const Expression *> &active;
		const Expression *expr;

		ActiveTypeProbeGuard(std::unordered_set<const Expression *> &active, const Expression *expr)
			: active(active), expr(expr) {
			active.insert(expr);
		}

		~ActiveTypeProbeGuard() { active.erase(expr); }
	} activeProbe(activeTypeProbes, targetExpr);

	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.trialJournal = &journal;

	(void)inferExpression(targetExpr, trialContext, false, targetBindingFrameStack);
	type = resolveTypeThroughBindings(targetExpr, targetBindingFrameStack);
	if (!type.isDeduced())
		type = derivePatternCallType(targetExpr, trialContext, targetBindingFrameStack);
	if (!type.isDeduced() && context.typeFailureDetail.empty() && !trialContext.typeFailureDetail.empty())
		context.typeFailureDetail = trialContext.typeFailureDetail;

	rollbackTrialJournal(journal);
	if (type.isDeduced() && !bindingDependentProbe)
		expr->type = type;
	return type;
}

static DataType ensureExpressionType(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	return inferExpressionTypeWithoutSideEffects(expr, context, bindingFrameStack);
}

static void commitVariableTypeFromValue(Variable *var, Expression *valueExpr, const DataType &valueType) {
	if (!var)
		return;
	var->type = concretizeClassType(valueType);
	var->typeOriginRange = valueExpr ? valueExpr->range : Range();
	var->typeOriginFloatLiteralReplacement = makeFloatLiteralReplacement(valueExpr);
}

static bool isVariableAssignmentCompatible(const DataType &targetType, const DataType &valueType) {
	if (!targetType.isDeduced() || !valueType.isDeduced())
		return false;
	if (targetType == valueType)
		return true;
	return targetType.kind == DataType::Kind::Int && valueType.kind == DataType::Kind::Int && targetType.pointerDepth == 0 &&
		   valueType.pointerDepth == 0;
}

#include "variable_flow.inl"

// Infer types for a section's code lines with operand reordering. Returns false on failure.
static bool
inferSection(Section *section, InferenceContext &context, const BindingFrameStack &bindingFrameStack = BindingFrameStack{});

// Infer the type of an expression bottom-up.
// Sets context.typesValid = false if types are invalid for this grouping.
static void inferOrderedExpression(
	Expression *expr, InferenceContext &context, const BindingFrameStack &macroBindingFrameStack = BindingFrameStack{}
) {
	context.typesValid = true;
	auto resolveThroughMacroBindings = [&](Expression *expression) {
		return resolveThroughBindings(expression, macroBindingFrameStack);
	};
	auto setConfiguredTypeFailure = [&](Range range, std::string_view key, std::string_view variant = "message",
										std::vector<std::pair<std::string, std::string>> replacements = {}) {
		context.setTypeFailure(
			renderConfiguredMessage(syntaxConfigForRange(context.parseContext, range), key, variant, replacements)
		);
	};
	if (expr->kind == Expression::Kind::IntrinsicCall && intrinsicKind(expr->intrinsicName) == IntrinsicKind::Select) {
		markCompileTimeParameterRequirements(expr->arguments[1], macroBindingFrameStack, context.currentInstantiation);
		Expression *chosenBranch =
			selectCompileTimeBranch(expr, context.parseContext, macroBindingFrameStack, context.currentInstantiation);
		if (!chosenBranch) {
			setConfiguredTypeFailure(expr->range, "select condition must be compile time known");
			return;
		}
		size_t chosenIndex = chosenBranch == expr->arguments[2] ? 2 : 3;
		Expression *activeBranch = chosenBranch;
		if (!inferExpression(activeBranch, context, false, macroBindingFrameStack))
			return;
		expr->arguments[chosenIndex] = activeBranch;
		expr->type = resolveTypeThroughBindings(activeBranch, macroBindingFrameStack);
		return;
	}
	bool deferArgumentInference = false;
	std::unordered_set<size_t> skippedArgumentIndices;
	if (expr->kind == Expression::Kind::PatternCall && !deferArgumentInference)
		skippedArgumentIndices = compileTimeOnlyArgumentIndices(expr);
	// Recurse into arguments first (bottom-up)
	if (!deferArgumentInference) {
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			if (skippedArgumentIndices.contains(i))
				continue;
			Expression *arg = expr->arguments[i];
			if (!inferExpression(arg, context, false, macroBindingFrameStack))
				return;
			expr->arguments[i] = arg;
		}
	}

	switch (expr->kind) {
	case Expression::Kind::Literal: {
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
				expr->type = {DataType::Kind::Float, context.parseContext.options.emitSPIRV ? 4 : 8};
			}
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {DataType::Kind::Int, 1};
			expr->type.pointerDepth = 1;
		}
		break;
	}

	case Expression::Kind::ArrayLiteral: {
		if (expr->arguments.empty())
			break;
		DataType elementType = resolveTypeThroughBindings(expr->arguments[0], macroBindingFrameStack);
		if (!elementType.isDeduced())
			break;
		for (size_t i = 1; i < expr->arguments.size(); i++) {
			DataType nextType = resolveTypeThroughBindings(expr->arguments[i], macroBindingFrameStack);
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
			context.snapshotReferenceConstant(expr->variable);
			// Check macro bindings first
			Expression *macroBinding = macroBindingFrameStack.lookup(varName);
			if (macroBinding) {
				DataType boundType = resolveTypeThroughBindings(macroBinding, macroBindingFrameStack);
				if (boundType.isDeduced()) {
					expr->type = boundType;
				}
				break;
			}
			// Look up variable in scope
			Section *sec = expr->range.line ? expr->range.line->section : nullptr;
			Variable *var = sec ? sec->findVariable(varName) : nullptr;
			if (var && var->type.isDeduced()) {
				expr->type = var->type;
			}
		}
		break;
	}

	case Expression::Kind::IntrinsicCall: {
		const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		if (info) {
			switch (info->returnKind) {
			case IntrinsicReturnKind::SameAsArgs:
				if (expr->arguments.size() == 2) {
					expr->type = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
				} else {
					DataType leftType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
					DataType result;
					if (!DataType::promoteArithmetic(leftType, rightType, result)) {
						setConfiguredTypeFailure(
							expr->range, "incompatible operand types", "message",
							{{"left_type", typeToUserName(leftType, context.parseContext)},
							 {"right_type", typeToUserName(rightType, context.parseContext)}}
						);
						break;
					}
					expr->type = result;
				}
				break;
			case IntrinsicReturnKind::Bool: {
				if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
					DataType leftType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
					if (!isLogicalOperandType(leftType) || !isLogicalOperandType(rightType)) {
						setConfiguredTypeFailure(
							expr->range, "logical operator operands invalid", "message",
							{{"operator", expr->intrinsicName},
							 {"left_type", typeToUserName(leftType, context.parseContext)},
							 {"right_type", typeToUserName(rightType, context.parseContext)}}
						);
						break;
					}
				} else if (kind == IntrinsicKind::Not) {
					DataType valueType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (!isLogicalOperandType(valueType)) {
						setConfiguredTypeFailure(
							expr->range, "logical operator operand invalid", "message",
							{{"operator", "not"}, {"value_type", typeToUserName(valueType, context.parseContext)}}
						);
						break;
					}
				} else {
					DataType leftType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
					DataType promoted;
					bool pointerEquality = (kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) &&
										   leftType.isPointer() && rightType.isPointer() && leftType == rightType;
					if (!pointerEquality && !DataType::promoteArithmetic(leftType, rightType, promoted)) {
						setConfiguredTypeFailure(
							expr->range, "incompatible operand types", "message",
							{{"left_type", typeToUserName(leftType, context.parseContext)},
							 {"right_type", typeToUserName(rightType, context.parseContext)}}
						);
						break;
					}
				}
				expr->type = {DataType::Kind::Bool};
				break;
			}
			case IntrinsicReturnKind::Void:
				// "store" has side effects on variable types beyond just being Void
				if (kind == IntrinsicKind::Store) {
					BindingFrameStack destinationBindingFrameStack;
					Expression *destExpr =
						resolveThroughBindingsDeep(expr->arguments[1], macroBindingFrameStack, destinationBindingFrameStack);
					BindingFrameStack valueBindingFrameStack;
					Expression *valueExpr =
						resolveThroughBindingsDeep(expr->arguments[2], macroBindingFrameStack, valueBindingFrameStack);
					DataType valType = ensureExpressionType(valueExpr, context, valueBindingFrameStack);
					auto applyCastTargetType = [&](Expression *castExpr, const BindingFrameStack &castBindingFrameStack) {
						if (!castExpr || castExpr->kind != Expression::Kind::IntrinsicCall ||
							intrinsicKind(castExpr->intrinsicName) != IntrinsicKind::Cast || castExpr->arguments.size() < 3)
							return;
						BindingFrameStack resolvedTypeBindingFrameStack;
						Expression *resolvedTypeExpr = resolveThroughBindingsDeep(
							castExpr->arguments[2], castBindingFrameStack, resolvedTypeBindingFrameStack
						);
						if (!resolvedTypeExpr)
							resolvedTypeExpr = castExpr->arguments[2];
						const BindingFrameStack &typeBindingsForResolution =
							resolvedTypeBindingFrameStack.hasBindings() ? resolvedTypeBindingFrameStack : castBindingFrameStack;
						DataType castTypeArg = resolveTypeThroughBindings(resolvedTypeExpr, typeBindingsForResolution);
						if (!(castTypeArg.kind == DataType::Kind::Type &&
							  castTypeArg.referencedKind != DataType::Kind::Unresolved)) {
							Expression *valBinding = castBindingFrameStack.lookup("val");
							if (valBinding && valBinding->kind == Expression::Kind::PatternCall &&
								valBinding->arguments.size() >= 2) {
								Expression *rawTypeExpr = valBinding->arguments[1];
								DataType recoveredTypeArg;
								if (rawTypeExpr && rawTypeExpr->type.kind == DataType::Kind::Type) {
									recoveredTypeArg = rawTypeExpr->type;
								} else if (resolveCompileTimeTypeReference(
											   context.parseContext, rawTypeExpr, castBindingFrameStack, recoveredTypeArg
										   ) &&
										   recoveredTypeArg.kind == DataType::Kind::Type) {
									castTypeArg = recoveredTypeArg;
								}
								if (recoveredTypeArg.kind == DataType::Kind::Type)
									castTypeArg = recoveredTypeArg;
							}
						}
						if (castTypeArg.kind == DataType::Kind::Type)
							valType = concretizeClassType(castTypeArg.toReferencedType());
					};
					applyCastTargetType(valueExpr, valueBindingFrameStack);
					if (valueExpr && valueExpr->kind == Expression::Kind::PatternCall) {
						BindingFrameStack castBindingFrameStack = valueBindingFrameStack;
						BindingMap innerBindings;
						Expression *bodyExpr = expandMacroPatternCall(valueExpr, innerBindings);
						if (bodyExpr) {
							BindingMap scopedMacroBindings;
							scopedMacroBindings.reserve(innerBindings.size());
							for (const auto &[name, argExpr] : innerBindings) {
								Expression *resolvedArg = resolveThroughBindings(argExpr, castBindingFrameStack);
								scopedMacroBindings[name] = resolvedArg ? resolvedArg : argExpr;
							}
							pushBindingScope(castBindingFrameStack, std::move(scopedMacroBindings));
							applyCastTargetType(bodyExpr, castBindingFrameStack);
						}
					}
					// If the original syntax is "value as type", recover the cast target type directly
					// from the raw PatternCall argument to avoid stale template types.
					Expression *rawValueExpr = expr->arguments[2];
					if (rawValueExpr && rawValueExpr->kind == Expression::Kind::PatternCall &&
						rawValueExpr->arguments.size() >= 2 &&
						rawValueExpr->range.subString.find(" as ") != std::string_view::npos) {
						DataType rawTypeArg = resolveTypeThroughBindings(rawValueExpr->arguments[1], macroBindingFrameStack);
						if (rawTypeArg.kind == DataType::Kind::Type)
							valType = concretizeClassType(rawTypeArg.toReferencedType());
					}
					if (valType.kind == DataType::Kind::Type) {
						setConfiguredTypeFailure(expr->range, "compile time type value used at runtime");
						break;
					}
					if (destExpr->kind == Expression::Kind::Variable && destExpr->variable && valType.isDeduced()) {
						Section *sec = destExpr->range.line ? destExpr->range.line->section : nullptr;
						Variable *var = sec ? sec->findVariable(destExpr->variable->name) : nullptr;
						if (var) {
							if (!var->type.isDeduced() || isVariableAssignmentCompatible(var->type, valType)) {
								if (context.trial && context.trialJournal)
									context.trialJournal->recordVariableWrite(var);
								if (!var->type.isDeduced() || var->type == valType)
									commitVariableTypeFromValue(var, valueExpr, valType);
								CompileTimeValue assignedValue =
									evaluateCompileTimeValueWithKnownState(valueExpr, context, valueBindingFrameStack);
								context.setKnownConstant(var->definition, assignedValue);
								if (context.inLoopMutationScope()) {
									context.noteLoopMutation(var->definition);
									context.setKnownConstant(var->definition, {});
								}
								context.snapshotReferenceConstant(destExpr->variable);
							} else if (context.trial) {
								setConfiguredTypeFailure(
									valueExpr ? valueExpr->range : expr->range, "variable type change", "message",
									{{"name", var->name},
									 {"from_type", typeToUserName(var->type, context.parseContext)},
									 {"to_type", typeToUserName(valType, context.parseContext)}}
								);
								break;
							} else {
								setConfiguredTypeFailure(
									valueExpr ? valueExpr->range : expr->range, "variable type change", "message",
									{{"name", var->name},
									 {"from_type", typeToUserName(var->type, context.parseContext)},
									 {"to_type", typeToUserName(valType, context.parseContext)}}
								);
								context.addDiagnostic(
									buildVariableTypeChangeDiagnostic(var, valueExpr, valType, context.parseContext)
								);
								break;
							}
						}
					} else if (destExpr->kind == Expression::Kind::IntrinsicCall &&
							   intrinsicKind(destExpr->intrinsicName) == IntrinsicKind::Property && valType.isDeduced()) {
						BindingFrameStack resolvedBindingFrameStack = macroBindingFrameStack;
						destinationBindingFrameStack.forEachFrame([&](const BindingFrame &frame) {
							pushBindingScope(resolvedBindingFrameStack, frame.bindings);
						});
						BindingFrameStack ignoredBindingFrameStack;
						Expression *ownerExpr = resolveThroughBindingsDeep(
							destExpr->arguments[1], resolvedBindingFrameStack, ignoredBindingFrameStack
						);
						DataType instType = ownerExpr ? concretizeClassType(ownerExpr->type) : DataType{};
						if (instType.kind == DataType::Kind::Class && instType.classDefinition &&
							instType.classInstIndex >= 0) {
							Expression *propExpr = resolveThroughBindings(destExpr->arguments[2], resolvedBindingFrameStack);
							std::string fieldName;
							if (auto *str = std::get_if<std::string>(&propExpr->literalValue))
								fieldName = *str;
							if (!fieldName.empty()) {
								ClassDefinition *classDef = instType.classDefinition;
								for (size_t i = 0; i < classDef->fields.size(); i++) {
									if (classDef->fields[i].name == fieldName) {
										int refinedInstIndex = getRefinedClassInstantiationIndex(
											context, classDef, instType.classInstIndex, i, valType
										);
										if (refinedInstIndex < 0)
											break;
										if (ownerExpr && ownerExpr->kind == Expression::Kind::Variable && ownerExpr->variable) {
											Section *ownerSection =
												ownerExpr->range.line ? ownerExpr->range.line->section : nullptr;
											Variable *ownerVar =
												ownerSection ? ownerSection->findVariable(ownerExpr->variable->name) : nullptr;
											if (ownerVar && ownerVar->type.kind == DataType::Kind::Class &&
												ownerVar->type.classDefinition == classDef) {
												if (context.trial && context.trialJournal)
													context.trialJournal->recordVariableWrite(ownerVar);
												ownerVar->type.classInstIndex = refinedInstIndex;
											}
										}
										break;
									}
								}
							}
						}
					}
				}
				expr->type = {DataType::Kind::Void};
				break;
			case IntrinsicReturnKind::Float:
				expr->type = {DataType::Kind::Float, 4};
				break;
			case IntrinsicReturnKind::Custom:
				if (kind == IntrinsicKind::AddressOf) {
					DataType varType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					if (varType.isDeduced())
						expr->type = varType.pointed();
				} else if (kind == IntrinsicKind::Dereference) {
					DataType ptrType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					if (ptrType.isDeduced() && ptrType.isPointer())
						expr->type = concretizeClassType(ptrType.dereferenced());
				} else if (kind == IntrinsicKind::LoadAt) {
					DataType ptrType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					if (ptrType.isDeduced() && ptrType.isPointer()) {
						DataType pointedType = ptrType.dereferenced();
						if (pointedType.kind == DataType::Kind::Array && pointedType.arrayElementType)
							expr->type = *pointedType.arrayElementType;
						else
							expr->type = pointedType;
					} else {
						setConfiguredTypeFailure(expr->range, "load at requires pointer");
						break;
					}
				} else if (kind == IntrinsicKind::Return) {
					if (expr->arguments.size() > 1) {
						DataType retType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
						if (retType.isDeduced()) {
							expr->type = retType;
							if (context.currentInstantiation)
								context.currentInstantiation->returnType = retType;
						}
					}
				} else if (kind == IntrinsicKind::Call) {
					DataType retTypeRef = resolveTypeThroughBindings(expr->arguments[3], macroBindingFrameStack);
					if (retTypeRef.kind != DataType::Kind::Type) {
						setConfiguredTypeFailure(expr->range, "call return type must be type reference");
						break;
					}
					if (retTypeRef.kind == DataType::Kind::Type && (retTypeRef.referencedKind == DataType::Kind::Type ||
																	retTypeRef.referencedKind == DataType::Kind::Unresolved)) {
						setConfiguredTypeFailure(expr->range, "call return type must be concrete runtime type");
						break;
					}
					for (size_t i = 4; i < expr->arguments.size(); i++) {
						DataType argType = resolveTypeThroughBindings(expr->arguments[i], macroBindingFrameStack);
						if (argType.kind == DataType::Kind::Type) {
							setConfiguredTypeFailure(expr->range, "call arguments cannot be type values");
							break;
						}
					}
					if (!context.typesValid)
						break;
					if (retTypeRef.kind == DataType::Kind::Type)
						expr->type = retTypeRef.toReferencedType();
				} else if (kind == IntrinsicKind::Cast) {
					DataType valueType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[2], macroBindingFrameStack);
					if (!valueType.isDeduced() || valueType.kind == DataType::Kind::Void) {
						setConfiguredTypeFailure(
							expr->range, "invalid cast source type", "message",
							{{"source_type", typeToUserName(valueType, context.parseContext)}}
						);
						break;
					}
					if (typeArgType.kind == DataType::Kind::Type) {
						expr->type = concretizeClassType(typeArgType.toReferencedType());
						if (!isSupportedCastConversion(valueType, expr->type)) {
							setConfiguredTypeFailure(
								expr->range, "unsupported cast", "message",
								{{"from_type", typeToUserName(valueType, context.parseContext)},
								 {"to_type", typeToUserName(expr->type, context.parseContext)}}
							);
							break;
						}
					}
				} else if (kind == IntrinsicKind::Type) {
					// @intrinsic("type", kindString[, bits])
					// Resolve kind string through macro bindings
					Expression *kindExpr = resolveThroughMacroBindings(expr->arguments[1]);
					std::string kindStr;
					if (auto *str = std::get_if<std::string>(&kindExpr->literalValue))
						kindStr = *str;
					if (!kindStr.empty()) {
						DataType typeRef;
						typeRef.kind = DataType::Kind::Type;
						if (kindStr == "int") {
							typeRef.referencedKind = DataType::Kind::Int;
							typeRef.numericSize = 4; // default
						} else if (kindStr == "float") {
							typeRef.referencedKind = DataType::Kind::Float;
							typeRef.numericSize = 8; // default
						} else if (kindStr == "bool") {
							typeRef.referencedKind = DataType::Kind::Bool;
						} else if (kindStr == "void") {
							typeRef.referencedKind = DataType::Kind::Void;
						} else if (kindStr == "string") {
							// string = pointer to byte (i8*)
							typeRef.referencedKind = DataType::Kind::Int;
							typeRef.numericSize = 1;
							typeRef.pointerDepth = 1;
						} else if (kindStr == "type") {
							typeRef.referencedKind = DataType::Kind::Type;
						}
						// Override byte size if bits argument provided
						if (expr->arguments.size() > 2) {
							Expression *bitsExpr = resolveThroughMacroBindings(expr->arguments[2]);
							if (auto *bits = std::get_if<double>(&bitsExpr->literalValue))
								typeRef.numericSize = (int)*bits / 8;
						}
						expr->type = typeRef;
					}
				} else if (kind == IntrinsicKind::TypeOf) {
					DataType valueType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					if (valueType.isDeduced()) {
						expr->type.kind = DataType::Kind::Type;
						expr->type.referencedKind = valueType.kind;
						expr->type.numericSize = valueType.numericSize;
						expr->type.pointerDepth = valueType.pointerDepth;
						expr->type.classDefinition = valueType.classDefinition;
						expr->type.classInstIndex = valueType.classInstIndex;
						expr->type.arraySize = valueType.arraySize;
						expr->type.arrayElementType =
							valueType.arrayElementType ? std::make_shared<DataType>(*valueType.arrayElementType) : nullptr;
					}
				} else if (kind == IntrinsicKind::SizeOf) {
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					if (typeArgType.kind == DataType::Kind::Type) {
						if (typeArgType.referencedKind == DataType::Kind::Type ||
							typeArgType.referencedKind == DataType::Kind::Unresolved) {
							setConfiguredTypeFailure(expr->range, "size of type invalid");
							break;
						}
						expr->type = {DataType::Kind::Int, 8};
					}
				} else if (kind == IntrinsicKind::BuildInfo) {
					Expression *keyExpr = resolveThroughMacroBindings(expr->arguments[1]);
					if (auto *key = std::get_if<std::string>(&keyExpr->literalValue)) {
						if (*key == "word size" || *key == "optimization level") {
							expr->type = {DataType::Kind::Int, 4};
						} else {
							expr->type = {DataType::Kind::Int, 1};
							expr->type.pointerDepth = 1;
						}
					}
				} else if (kind == IntrinsicKind::Select) {
					markCompileTimeParameterRequirements(
						expr->arguments[1], macroBindingFrameStack, context.currentInstantiation
					);
					Expression *chosenBranch = selectCompileTimeBranch(
						expr, context.parseContext, macroBindingFrameStack, context.currentInstantiation
					);
					if (!chosenBranch) {
						setConfiguredTypeFailure(expr->range, "select condition must be compile time known");
						break;
					}
					DataType chosenType = resolveTypeThroughBindings(chosenBranch, macroBindingFrameStack);
					if (chosenType.isDeduced())
						expr->type = chosenType;
				} else if (kind == IntrinsicKind::Array) {
					Expression *sizeExpr = resolveThroughMacroBindings(expr->arguments[1]);
					if (auto *size = std::get_if<double>(&sizeExpr->literalValue)) {
						expr->type.kind = DataType::Kind::Type;
						expr->type.referencedKind = DataType::Kind::Array;
						expr->type.arraySize = static_cast<int>(*size);
						if (expr->arguments.size() > 2) {
							DataType elemTypeRef = resolveTypeThroughBindings(expr->arguments[2], macroBindingFrameStack);
							if (elemTypeRef.kind == DataType::Kind::Type)
								expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
						}
					}
				} else if (kind == IntrinsicKind::Vector) {
					int vectorSize = 0;
					if (!evaluateCompileTimeInteger(
							context.parseContext, expr->arguments[1], macroBindingFrameStack, vectorSize
						) ||
						vectorSize < 1) {
						setConfiguredTypeFailure(expr->range, "vector size invalid");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Vector;
					expr->type.arraySize = vectorSize;
					expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
					if (expr->arguments.size() > 2) {
						DataType elemTypeRef = resolveTypeThroughBindings(expr->arguments[2], macroBindingFrameStack);
						if (elemTypeRef.kind == DataType::Kind::Type)
							expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
				} else if (kind == IntrinsicKind::Matrix) {
					int rows = 0;
					int columns = 0;
					if (!evaluateCompileTimeInteger(context.parseContext, expr->arguments[1], macroBindingFrameStack, rows) ||
						!evaluateCompileTimeInteger(
							context.parseContext, expr->arguments[2], macroBindingFrameStack, columns
						) ||
						rows < 1 || columns < 1) {
						setConfiguredTypeFailure(expr->range, "matrix dimensions invalid");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Matrix;
					expr->type.matrixRowCount = rows;
					expr->type.arraySize = columns;
					expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
					if (expr->arguments.size() > 3) {
						DataType elemTypeRef = resolveTypeThroughBindings(expr->arguments[3], macroBindingFrameStack);
						if (elemTypeRef.kind == DataType::Kind::Type)
							expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
				} else if (kind == IntrinsicKind::AddPointerDepth) {
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					if (typeArgType.kind == DataType::Kind::Type) {
						if (typeArgType.referencedKind == DataType::Kind::Type ||
							typeArgType.referencedKind == DataType::Kind::Unresolved) {
							setConfiguredTypeFailure(expr->range, "pointer to type invalid");
							break;
						}
						expr->type = typeArgType;
						expr->type.pointerDepth++;
					}
				} else if (kind == IntrinsicKind::Construct) {
					markCompileTimeParameterRequirements(
						expr->arguments[1], macroBindingFrameStack, context.currentInstantiation
					);
					DataType typeRefType;
					if (!resolveCompileTimeTypeReference(
							context.parseContext, expr->arguments[1], macroBindingFrameStack, typeRefType
						) ||
						typeRefType.kind != DataType::Kind::Type) {
						break;
					}
					if (typeRefType.referencedKind == DataType::Kind::Array) {
						DataType arrayType = typeRefType.toReferencedType();
						if (arrayType.arraySize == static_cast<int>(expr->arguments.size()) - 2) {
							DataType elementType =
								arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
							bool allDeduced = true;
							for (size_t i = 2; i < expr->arguments.size(); i++) {
								DataType argType = resolveTypeThroughBindings(expr->arguments[i], macroBindingFrameStack);
								if (!argType.isDeduced())
									allDeduced = false;
								if (!arrayType.arrayElementType) {
									if (!elementType.isDeduced())
										elementType = argType;
									else if (elementType != argType)
										allDeduced = false;
								}
							}
							if (allDeduced && elementType.isDeduced()) {
								arrayType.arrayElementType = std::make_shared<DataType>(elementType);
								expr->type = arrayType;
							}
						}
					} else if (typeRefType.referencedKind == DataType::Kind::Vector) {
						DataType vectorType = typeRefType.toReferencedType();
						if (vectorType.arraySize == static_cast<int>(expr->arguments.size()) - 2) {
							bool allCompatible = true;
							for (size_t i = 2; i < expr->arguments.size(); i++) {
								DataType argType = resolveTypeThroughBindings(expr->arguments[i], macroBindingFrameStack);
								if (!argType.isDeduced()) {
									allCompatible = false;
									break;
								}
								DataType promoted;
								if (!DataType::promoteArithmetic(argType, *vectorType.arrayElementType, promoted) ||
									promoted != *vectorType.arrayElementType) {
									allCompatible = false;
									break;
								}
							}
							if (allCompatible)
								expr->type = vectorType;
						}
					} else if (typeRefType.referencedKind == DataType::Kind::Matrix) {
						DataType matrixType = typeRefType.toReferencedType();
						if (expr->arguments.size() == 3) {
							DataType valueType = resolveTypeThroughBindings(expr->arguments[2], macroBindingFrameStack);
							if (valueType.kind == DataType::Kind::Array && valueType.arrayElementType &&
								valueType.arraySize == matrixType.matrixRows() * matrixType.matrixColumns()) {
								DataType promoted;
								if (DataType::promoteArithmetic(
										*valueType.arrayElementType, matrixType.matrixElementType(), promoted
									) &&
									promoted == matrixType.matrixElementType()) {
									expr->type = matrixType;
								}
							}
						}
					} else if (typeRefType.classDefinition) {
						std::vector<DataType> argumentTypes;
						argumentTypes.reserve(expr->arguments.size() - 2);
						bool allArgumentsDeduced = true;
						for (size_t i = 2; i < expr->arguments.size(); i++) {
							DataType argumentType = resolveTypeThroughBindings(expr->arguments[i], macroBindingFrameStack);
							if (!argumentType.isDeduced()) {
								allArgumentsDeduced = false;
								break;
							}
							argumentTypes.push_back(argumentType);
						}

						DataType instantiatedTypeRef;
						if (allArgumentsDeduced &&
							instantiateClassFromArgumentTypes(
								typeRefType.classDefinition, argumentTypes, instantiatedTypeRef, typeRefType.classInstIndex
							)) {
							expr->type = instantiatedTypeRef.toReferencedType();
						} else {
							DataType targetType = concretizeClassType(typeRefType.toReferencedType());
							if (expr->arguments.size() == targetType.classDefinition->fields.size() + 2 &&
								targetType.classInstIndex >= 0) {
								const auto &fieldTypes =
									targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
								bool allCompatible = argumentTypes.size() == fieldTypes.size();
								for (size_t i = 0; allCompatible && i < fieldTypes.size(); i++) {
									if (argumentTypes[i] != fieldTypes[i])
										allCompatible = false;
								}
								if (allCompatible)
									expr->type = targetType;
							}
						}
					} else if (expr->arguments.size() == 3) {
						DataType targetType = typeRefType.toReferencedType();
						DataType valueType = resolveTypeThroughBindings(expr->arguments[2], macroBindingFrameStack);
						if (valueType.isDeduced())
							expr->type = targetType;
					}
				} else if (kind == IntrinsicKind::Property) {
					DataType instType =
						concretizeClassType(resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack));
					if (instType.isPointer() && instType.kind == DataType::Kind::Class)
						instType = concretizeClassType(instType.dereferenced());
					Expression *propExpr = resolveThroughMacroBindings(expr->arguments[2]);
					std::string fieldName = extractFieldName(propExpr);
					DataType builtInPropertyType = resolveBuiltInPropertyType(instType, fieldName);
					if (builtInPropertyType.isDeduced()) {
						expr->type = builtInPropertyType;
						break;
					}
					if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
						if (!fieldName.empty()) {
							ClassDefinition *classDef = instType.classDefinition;
							for (size_t i = 0; i < classDef->fields.size(); i++) {
								if (classDef->fields[i].name == fieldName) {
									expr->type = classDef->instantiations[instType.classInstIndex].fieldTypes[i];
									break;
								}
							}
						}
					}
				} else {
					std::string uri =
						(expr && expr->range.line && expr->range.line->sourceFile) ? expr->range.line->sourceFile->uri : "";
					int line = (expr && expr->range.line) ? expr->range.line->sourceFileLineIndex + 1 : -1;
					crashUnimplementedIntrinsic("type inference", expr->intrinsicName, uri, line);
				}
				break;
			}
		}
		break;
	}

	case Expression::Kind::PatternCall: {
		if (!context.trial)
			expr->selectedPatternDefinition = nullptr;
		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;

		// Build argument types for overload selection.
		// Arguments are sorted by source position and include both Variable and Word captures.
		std::vector<DataType> argTypesForOverload;
		for (size_t ai = 0; ai < expr->arguments.size(); ai++)
			argTypesForOverload.push_back(resolveTypeThroughBindings(expr->arguments[ai], macroBindingFrameStack));

		// Select the best overload based on argument types
		PatternDefinition *def = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload);
		if (!def) {
			std::string candidates;
			std::unordered_set<std::string> uniqueCandidates;
			for (PatternDefinition *candidate : defs) {
				std::string pattern = (std::string)candidate->range.subString;
				if (!pattern.empty() && !uniqueCandidates.contains(pattern)) {
					if (!candidates.empty())
						candidates += ", ";
					candidates += "'" + pattern + "'";
					uniqueCandidates.insert(pattern);
				}
			}
			context.setTypeFailure(renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, expr->range), "no overload matches call",
				candidates.empty() ? "message" : "with overloads",
				{{"call", (std::string)expr->range.subString},
				 {"argument_types", formatTypeList(argTypesForOverload, context.parseContext)},
				 {"overloads", candidates}}
			));
			break;
		}
		if (!context.trial) {
			expr->selectedPatternDefinition = def;
			if (context.currentInstantiation)
				context.currentInstantiation->selectedOverloadsByCall[expr] = def;
		}

		Section *matchedSection = def->section;

		// Build parameter bindings from call-site arguments
		BindingMap callBindings;
		appendPatternCallBindings(expr, def, callBindings);
		for (auto &[name, boundExpr] : callBindings) {
			Expression *resolvedExpr = resolveThroughMacroBindings(boundExpr);
			if (resolvedExpr)
				boundExpr = resolvedExpr;
		}
		BindingFrameStack callBindingFrameStack = macroBindingFrameStack;
		pushBindingScope(callBindingFrameStack, callBindings);

		if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
			auto *classSec = static_cast<ClassSection *>(matchedSection);
			expr->type = instantiateBoundClassType(context.parseContext, classSec->classDefinition, callBindingFrameStack);
		} else if (matchedSection->isMacro) {
			// Code replacement: infer body, type = replacement expression type
			if (!matchedSection->inferring) {
				matchedSection->inferring = true;
				ScopedDiagnosticSuppression suppressDiagnostics(context);
				inferSection(matchedSection, context, callBindingFrameStack);
				matchedSection->inferring = false;
			}
			if (!context.typesValid)
				break;
			for (Section *child : matchedSection->children) {
				for (CodeLine *line : child->codeLines) {
					if (!line->expression)
						continue;
					DataType resolvedType = resolveTypeThroughBindings(line->expression, callBindingFrameStack);
					if (resolvedType.isDeduced()) {
						line->expression->type = resolvedType;
						expr->type = resolvedType;
					}
				}
			}
		} else {
			// Non-macro function: infer body per-instantiation
			// Build parameter bindings and argTypes in nodesPassed order (must match codegen's paramBindings order)
			std::vector<std::pair<std::string, Expression *>> paramBindings;
			collectPatternCallBindingPairs(expr, def, paramBindings);
			std::vector<DataType> argTypes;
			for (auto &[parameterName, argumentExpression] : paramBindings) {
				argumentExpression = callBindings[parameterName];
				ArgumentTypeInferenceResult argTypeResult =
					ensureArgumentTypeForPatternCall(argumentExpression, context, macroBindingFrameStack);
				DataType argType = argTypeResult.type;
				if (!argType.isDeduced()) {
					if (argTypeResult.deferred && context.currentInstantiation) {
						context.currentInstantiation->needsReinfer = true;
						return;
					}
					if (context.trial) {
						setConfiguredTypeFailure(expr->range, "undeduced argument type in trial inference");
						return;
					}
					assert(
						argType.isDeduced() && "Undeduced argument type encountered during non-macro pattern-call inference"
					);
				}
				argTypes.push_back(argType);
			}

			if (context.trial && context.trialJournal)
				context.trialJournal->recordSectionInstantiationWrite(matchedSection, argTypes);
			Instantiation &inst = matchedSection->instantiations[argTypes];
			bool hasReusableInstantiation = inst.valid && inst.returnType.isDeduced() && !inst.needsReinfer;
			if (!inst.inferring && !hasReusableInstantiation) {
				Instantiation *savedInst = context.currentInstantiation;
				auto callerKnownConstants = context.currentKnownConstants;
				const Instantiation *callerInstantiation = savedInst;
				bool inferenceSucceeded = runInstantiationReinferenceLoop(
					context, inst, def, expr->range, (std::string)def->range.subString, [&]() -> bool {
					if (!context.trial)
						inst.selectedOverloadsByCall.clear();
					if (!context.trial)
						inst.ifChainSelections.clear();
					seedInstantiationCompileTimeParameters(
						context.parseContext, inst, paramBindings, macroBindingFrameStack, callerInstantiation
					);
					size_t requiredBeforePass = inst.requiredCompileTimeParameters.size();
					inst.needsReinfer = false;
					inst.inferring = true;
					context.currentInstantiation = &inst;
					context.currentKnownConstants = callerKnownConstants;
					bool savedReinferSuppression = context.suppressReinferPassDiagnostics;
					context.suppressReinferPassDiagnostics = true;
					BindingMap nonMacroTypeBindings;
					std::vector<std::unique_ptr<Expression>> ownedNonMacroTypeBindings;
					for (size_t i = 0; i < paramBindings.size() && i < argTypes.size(); i++) {
						auto bindingValue = makeNonMacroParameterBinding(paramBindings[i].first, argTypes[i], inst);
						nonMacroTypeBindings[paramBindings[i].first] = bindingValue.get();
						ownedNonMacroTypeBindings.push_back(std::move(bindingValue));
					}
					bool passSucceeded = inferSection(matchedSection, context, makeBindingFrameStack(nonMacroTypeBindings));
					context.suppressReinferPassDiagnostics = savedReinferSuppression;
					inst.inferring = false;
					if (inst.requiredCompileTimeParameters.size() != requiredBeforePass)
						inst.needsReinfer = true;
					return passSucceeded;
				});
				context.currentKnownConstants = std::move(callerKnownConstants);
				context.currentInstantiation = savedInst;
				inst.valid = inferenceSucceeded;
			} else if (inst.returnType.isDeduced()) {
				expr->type = inst.returnType;
			} else {
				context.observedInProgressUndeducedInstantiation = true;
			}
			if (!inst.valid) {
				context.typesValid = false;
				break;
			}
			if (!context.typesValid)
				break;

			if (inst.parameterTypes.size() != paramBindings.size()) {
				inst.parameterTypes.clear();
				for (size_t i = 0; i < paramBindings.size(); i++) {
					Variable *paramVar = findVariableInSectionTree(matchedSection, paramBindings[i].first);
					if (paramVar && paramVar->type.isDeduced())
						inst.parameterTypes.push_back(paramVar->type);
					else
						inst.parameterTypes.push_back(argTypes[i]);
				}
			}
			for (size_t i = 0; i < paramBindings.size() && i < inst.parameterTypes.size(); i++) {
				const DataType &parameterType = inst.parameterTypes[i];
				if (!parameterType.isDeduced() || parameterType == argTypes[i])
					continue;
				Expression *argExpr = resolveThroughMacroBindings(paramBindings[i].second);
				if (!argExpr || argExpr->kind != Expression::Kind::Variable || !argExpr->variable)
					continue;
				Section *argSection = argExpr->range.line ? argExpr->range.line->section : nullptr;
				Variable *argVar = argSection ? argSection->findVariable(argExpr->variable->name) : nullptr;
				if (!argVar)
					continue;
				if (!argVar->type.isDeduced() || argVar->type == parameterType) {
					if (context.trial && context.trialJournal)
						context.trialJournal->recordVariableWrite(argVar);
					commitVariableTypeFromValue(argVar, argExpr, parameterType);
				} else if (argVar->type.kind == DataType::Kind::Class && parameterType.kind == DataType::Kind::Class &&
						   argVar->type.classDefinition == parameterType.classDefinition) {
					if (context.trial && context.trialJournal)
						context.trialJournal->recordVariableWrite(argVar);
					argVar->type.classInstIndex = parameterType.classInstIndex;
				}
			}

			// If no return intrinsic was found, default to Void
			if (!inst.inferring && inst.returnType.kind == DataType::Kind::Any) {
				inst.returnType = {DataType::Kind::Void};
			}
			if (inst.returnType.isDeduced())
				expr->type = inst.returnType;
		}

		break;
	}

	case Expression::Kind::Pending:
		break;
	}
}
