#pragma once

#include <bit>
#include <cstdlib>
#include <iostream>

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
	std::vector<DataType> argumentTypes;
	std::unordered_map<std::string, CompileTimeValue> constantParameterValues;
	std::unordered_map<VariableReference *, CompileTimeValue> constantValuesByReference;
	std::unordered_set<VariableReference *> writtenGlobalReferences;
	std::unordered_map<VariableReference *, CompileTimeValue> finalGlobalConstantValues;
	std::unordered_map<Expression *, PatternDefinition *> selectedOverloadsByCall;
	std::unordered_set<std::string> requiredCompileTimeParameters;

	bool operator==(const InstantiationProgressSnapshot &other) const = default;
};

static InstantiationProgressSnapshot snapshotInstantiationProgress(const Instantiation &instantiation) {
	return {
		instantiation.returnType,
		instantiation.argumentTypes,
		instantiation.constantParameterValues,
		instantiation.constantValuesByReference,
		instantiation.writtenGlobalReferences,
		instantiation.finalGlobalConstantValues,
		instantiation.selectedOverloadsByCall,
		instantiation.requiredCompileTimeParameters,
	};
}

static void mergeWrittenGlobalConstantsIntoCaller(
	const Instantiation &inst, std::unordered_map<VariableReference *, CompileTimeValue> &callerKnownConstants
) {
	for (VariableReference *reference : inst.writtenGlobalReferences) {
		auto it = inst.finalGlobalConstantValues.find(reference);
		if (it != inst.finalGlobalConstantValues.end() && isCompileTimeKnown(it->second))
			callerKnownConstants[reference] = it->second;
		else
			callerKnownConstants.erase(reference);
	}
}

static void setRecursiveInferenceFailure(
	InferenceContext &context, PatternDefinition *definition, const Range &fallbackRange, std::string functionName
) {
	Range diagnosticRange = definition ? definition->range : fallbackRange;
	if (functionName.empty())
		functionName = definition ? (std::string)definition->range.subString : "<expression>";
	context.setTypeFailure(renderConfiguredMessage(
		syntaxConfigForRange(context.parseContext, diagnosticRange), "recursive type inference did not converge", "message",
		{{"function", functionName}}
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
		context.clearTypeFailure();
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

static std::string encodeTrialCompileTimeValue(const CompileTimeValue &value) {
	if (std::holds_alternative<std::monostate>(value))
		return "?";
	if (const auto *number = std::get_if<double>(&value))
		return "d" + std::to_string(std::bit_cast<uint64_t>(*number));
	if (const auto *text = std::get_if<std::string>(&value))
		return "s" + std::to_string(text->size()) + ":" + *text;
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "b1" : "b0";
	return "?";
}

static std::vector<std::pair<std::string, CompileTimeValue>> collectTrialCompileTimeParameters(
	ParseContext &parseContext, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const BindingFrameStack &callerBindingFrameStack, const Instantiation *callerInstantiation
) {
	std::vector<std::pair<std::string, CompileTimeValue>> values;
	values.reserve(paramBindings.size());
	for (const auto &[name, argExpr] : paramBindings)
		values.push_back({name, evaluateCompileTimeValue(argExpr, parseContext, callerBindingFrameStack, callerInstantiation)});
	return values;
}

static std::string buildTrialInstantiationCacheKey(
	Section *section, bool sectionHasCommittedOrdering, const std::vector<DataType> &argTypes,
	const std::vector<std::pair<std::string, CompileTimeValue>> &compileTimeParameters
) {
	std::string key = std::to_string(reinterpret_cast<uintptr_t>(section));
	key += sectionHasCommittedOrdering ? "|ordered" : "|unordered";
	for (const DataType &type : argTypes) {
		key += "|arg:";
		key += type.toString();
	}
	for (const auto &[name, value] : compileTimeParameters) {
		key += "|param:";
		key += name;
		key += "=";
		key += encodeTrialCompileTimeValue(value);
	}
	return key;
}

static bool traceTrialInstantiationCacheEnabled() {
	static bool enabled = std::getenv("DYNLEX_TRACE_TRIAL_INSTANTIATION_CACHE") != nullptr;
	return enabled;
}

static void traceTrialInstantiationCacheEvent(std::string_view event, PatternDefinition *definition, const std::string &key) {
	if (!traceTrialInstantiationCacheEnabled())
		return;
	std::cerr << "[trial-inst-cache] " << event;
	if (definition)
		std::cerr << " function='" << std::string(definition->range.subString) << "'";
	std::cerr << " key='" << key << "'\n";
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
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack,
	bool requireVoidResult = false
);
static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
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

static bool definitionParameterAcceptsVoid(
	PatternDefinition *definition, const std::vector<PatternTreeNode *> &nodesPassed, size_t argumentIndex
) {
	if (!definition)
		return false;
	size_t currentArgumentIndex = 0;
	for (PatternTreeNode *node : nodesPassed) {
		if (!node)
			continue;
		auto paramIt = node->parameterNames.find(definition);
		if (paramIt == node->parameterNames.end())
			continue;
		if (currentArgumentIndex != argumentIndex) {
			currentArgumentIndex++;
			continue;
		}
		for (const auto &element : definition->patternElements) {
			if (element.type != PatternElement::Type::Variable || element.text != paramIt->second)
				continue;
			return element.resolvedTypeConstraint.isDeduced() && element.resolvedTypeConstraint.kind == DataType::Kind::Void &&
				   element.resolvedTypeConstraint.pointerDepth == 0;
		}
		return false;
	}
	return false;
}

static ArgumentTypeInferenceResult ensureArgumentTypeForPatternCall(
	Expression *argExpr, InferenceContext &context, const BindingFrameStack &callerBindingFrameStack
) {
	ArgumentTypeInferenceResult result;
	DataType resolvedType = resolveTypeThroughBindings(argExpr, callerBindingFrameStack);
	if (argExpr && argExpr->kind != Expression::Kind::PatternCall && resolvedType.isDeduced()) {
		result.type = resolvedType;
		return result;
	}
	bool savedObservedInProgress = context.observedInProgressUndeducedInstantiation;
	context.observedInProgressUndeducedInstantiation = false;
	Expression *inferExpr = argExpr;
	(void)inferExpressionWithCurrentGrouping(inferExpr, context, callerBindingFrameStack);
	result.type = ensureExpressionType(inferExpr, context, callerBindingFrameStack);
	if (!result.type.isDeduced())
		result.type = derivePatternCallType(inferExpr, context, callerBindingFrameStack);
	if (!result.type.isDeduced())
		result.type = resolveTypeThroughBindings(inferExpr, callerBindingFrameStack);
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

static DefinitionPatternElement *
findParameterElement(std::vector<DefinitionPatternElement> &elements, const std::string &parameterName) {
	for (auto &element : elements) {
		if (element.type == PatternElement::Type::Choice) {
			for (auto &alternative : element.alternatives) {
				if (DefinitionPatternElement *found = findParameterElement(alternative, parameterName))
					return found;
			}
			continue;
		}
		if (element.type == PatternElement::Type::Variable && element.text == parameterName)
			return &element;
	}
	return nullptr;
}

static bool expressionContainsTargetExpression(Expression *expr, Expression *target) {
	if (!expr || !target)
		return false;
	if (expr == target)
		return true;
	for (Expression *arg : expr->arguments) {
		if (expressionContainsTargetExpression(arg, target))
			return true;
	}
	return false;
}

static bool rangeStartsEarlier(const Range &candidate, const Range &currentBest) {
	if (!candidate.line)
		return false;
	if (!currentBest.line)
		return true;
	if (candidate.line->mergedLineIndex != currentBest.line->mergedLineIndex)
		return candidate.line->mergedLineIndex < currentBest.line->mergedLineIndex;
	return candidate.start() < currentBest.start();
}

static void findFirstNamedReferenceInExpression(Expression *expr, const std::string &name, Expression *&bestReference) {
	if (!expr)
		return;
	if (expr->kind == Expression::Kind::Variable && expr->variable && expr->variable->name == name &&
		rangeStartsEarlier(expr->range, bestReference ? bestReference->range : Range()))
		bestReference = expr;
	for (Expression *arg : expr->arguments) {
		findFirstNamedReferenceInExpression(arg, name, bestReference);
	}
}

static void findFirstNamedReferenceInSection(Section *section, const std::string &name, Expression *&bestReference) {
	if (!section)
		return;
	for (CodeLine *line : section->codeLines) {
		if (line && line->expression)
			findFirstNamedReferenceInExpression(line->expression, name, bestReference);
	}
	for (Section *child : section->children)
		findFirstNamedReferenceInSection(child, name, bestReference);
}

static Expression *findFirstNamedReferenceInSection(Section *section, const std::string &name) {
	Expression *bestReference = nullptr;
	findFirstNamedReferenceInSection(section, name, bestReference);
	return bestReference;
}

static bool findExpressionPath(Expression *root, Expression *target, std::vector<Expression *> &path) {
	if (!root || !target)
		return false;
	path.push_back(root);
	if (root == target)
		return true;
	for (Expression *arg : root->arguments) {
		if (findExpressionPath(arg, target, path))
			return true;
	}
	path.pop_back();
	return false;
}

struct PatternCallTraceTarget {
	PatternDefinition *definition{};
	std::string parameterName;
};

static void collectPatternCallTraceTargets(
	Expression *patternCallExpr, Expression *targetExpression, std::vector<PatternCallTraceTarget> &outTargets
) {
	if (!patternCallExpr || patternCallExpr->kind != Expression::Kind::PatternCall || !patternCallExpr->patternMatch ||
		!patternCallExpr->patternMatch->matchedEndNode || !targetExpression)
		return;

	std::vector<PatternDefinition *> candidateDefinitions;
	if (patternCallExpr->selectedPatternDefinition)
		candidateDefinitions.push_back(patternCallExpr->selectedPatternDefinition);
	else
		candidateDefinitions = patternCallExpr->patternMatch->matchedEndNode->matchingDefinitions;

	for (PatternDefinition *definition : candidateDefinitions) {
		if (!definition)
			continue;
		std::vector<std::pair<std::string, Expression *>> paramBindings;
		collectPatternCallBindingPairs(patternCallExpr, definition, paramBindings);
		for (const auto &[parameterName, argumentExpression] : paramBindings) {
			if (!expressionContainsTargetExpression(argumentExpression, targetExpression))
				continue;
			bool duplicate = false;
			for (const auto &existing : outTargets) {
				if (existing.definition == definition && existing.parameterName == parameterName) {
					duplicate = true;
					break;
				}
			}
			if (!duplicate)
				outTargets.push_back({definition, parameterName});
		}
	}
}

struct ImplicitPromotionTraceResult {
	bool reachesStore = false;
	Range reportRange;
	Range firstNameMismatchReferenceRange;
};

static ImplicitPromotionTraceResult traceImplicitPromotionUse(
	PatternDefinition *definition, const std::string &parameterName, const Range &firstNameMismatchReferenceRange,
	const Range &deepestNonMacroReferenceRange, std::unordered_set<std::string> &visited
) {
	if (!definition || !definition->section)
		return {};

	std::string visitKey = std::to_string(reinterpret_cast<uintptr_t>(definition)) + "|" + parameterName;
	if (!visited.insert(visitKey).second)
		return {};

	Expression *firstReference = findFirstNamedReferenceInSection(definition->section, parameterName);
	if (!firstReference || !firstReference->range.line || !firstReference->range.line->expression)
		return {};

	Range currentDeepestNonMacroReferenceRange = deepestNonMacroReferenceRange;
	if (!definition->section->isMacro && definition->section->type == SectionType::Function)
		currentDeepestNonMacroReferenceRange = firstReference->range;

	std::vector<Expression *> path;
	if (!findExpressionPath(firstReference->range.line->expression, firstReference, path))
		return {};

	for (auto it = path.rbegin(); it != path.rend(); ++it) {
		Expression *consumer = *it;
		if (consumer == firstReference)
			continue;
		if (consumer->kind == Expression::Kind::IntrinsicCall) {
			if (intrinsicKind(consumer->intrinsicName) == IntrinsicKind::Store && consumer->arguments.size() > 1 &&
				expressionContainsTargetExpression(consumer->arguments[1], firstReference)) {
				ImplicitPromotionTraceResult result;
				result.reachesStore = true;
				result.reportRange =
					currentDeepestNonMacroReferenceRange.line ? currentDeepestNonMacroReferenceRange : firstReference->range;
				result.firstNameMismatchReferenceRange = firstNameMismatchReferenceRange;
				return result;
			}
			return {};
		}
		if (consumer->kind == Expression::Kind::PatternCall) {
			std::vector<PatternCallTraceTarget> targets;
			collectPatternCallTraceTargets(consumer, firstReference, targets);
			if (targets.size() != 1)
				return {};

			Range nextFirstNameMismatchReferenceRange = firstNameMismatchReferenceRange;
			if (!nextFirstNameMismatchReferenceRange.line && targets.front().parameterName != parameterName)
				nextFirstNameMismatchReferenceRange = firstReference->range;

			return traceImplicitPromotionUse(
				targets.front().definition, targets.front().parameterName, nextFirstNameMismatchReferenceRange,
				currentDeepestNonMacroReferenceRange, visited
			);
		}
	}

	return {};
}

static ImplicitPromotionTraceResult traceImplicitPromotionUse(PatternDefinition *definition, const std::string &parameterName) {
	std::unordered_set<std::string> visited;
	return traceImplicitPromotionUse(definition, parameterName, {}, {}, visited);
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
	trialContext.trialInstantiationCache =
		context.trialInstantiationCache ? context.trialInstantiationCache : context.ensureTrialInstantiationCache();

	(void)inferExpression(targetExpr, trialContext, false, targetBindingFrameStack);
	if (trialContext.typesValid) {
		type = resolveTypeThroughBindings(targetExpr, targetBindingFrameStack);
		if (!type.isDeduced())
			type = derivePatternCallType(targetExpr, trialContext, targetBindingFrameStack);
	}
	context.inheritTypeFailureFrom(trialContext);

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

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Expression *valueExpr, const DataType &valueType, ParseContext &parseContext);
static int getRefinedClassInstantiationIndex(
	InferenceContext &context, ClassDefinition *classDef, int instIndex, size_t fieldIndex, const DataType &fieldType
);

#include "intrinsics/store_inference.inl"
#include "variable_flow.inl"

// Infer types for a section's code lines with operand reordering. Returns false on failure.
static bool
inferSection(Section *section, InferenceContext &context, const BindingFrameStack &bindingFrameStack = BindingFrameStack{});

// Infer the type of an expression bottom-up.
// Sets context.typesValid = false if types are invalid for this grouping.
static void inferOrderedExpression(
	Expression *expr, InferenceContext &context, const BindingFrameStack &macroBindingFrameStack = BindingFrameStack{},
	bool preserveCurrentGrouping = false
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
		if (!(preserveCurrentGrouping ? inferExpressionWithCurrentGrouping(activeBranch, context, macroBindingFrameStack)
									  : inferExpression(activeBranch, context, false, macroBindingFrameStack)))
			return;
		expr->arguments[chosenIndex] = activeBranch;
		expr->type = resolveTypeThroughBindings(activeBranch, macroBindingFrameStack);
		return;
	}
	bool deferArgumentInference = false;
	std::unordered_set<size_t> skippedArgumentIndices;
	if (expr->kind == Expression::Kind::PatternCall && !deferArgumentInference)
		skippedArgumentIndices = compileTimeOnlyArgumentIndices(expr);
	if (expr->kind == Expression::Kind::IntrinsicCall && intrinsicKind(expr->intrinsicName) == IntrinsicKind::Construct)
		skippedArgumentIndices.insert(1);
	// Recurse into arguments first (bottom-up)
	if (!deferArgumentInference) {
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			if (skippedArgumentIndices.contains(i))
				continue;
			Expression *arg = expr->arguments[i];
			if (!(preserveCurrentGrouping ? inferExpressionWithCurrentGrouping(arg, context, macroBindingFrameStack)
										  : inferExpression(arg, context, false, macroBindingFrameStack)))
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
				if (kind == IntrinsicKind::Return && expr->arguments.size() > 1) {
					DataType retType = resolveTypeThroughBindings(expr->arguments[1], macroBindingFrameStack);
					if (retType.isDeduced() && context.currentInstantiation)
						context.currentInstantiation->returnType = retType;
				}
				if (kind == IntrinsicKind::Store)
					inferStoreEffects(expr, context, macroBindingFrameStack);
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
				} else if (kind == IntrinsicKind::Function) {
					Expression *functionExpr = resolveThroughMacroBindings(expr->arguments[1]);
					if (!std::holds_alternative<std::string>(functionExpr->literalValue)) {
						setConfiguredTypeFailure(expr->range, "function intrinsic requires string literal");
						break;
					}
					expr->type = {DataType::Kind::Int, 1};
					expr->type.pointerDepth = 1;
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
					markCompileTimeParameterRequirements(
						expr->arguments[1], macroBindingFrameStack, context.currentInstantiation
					);
					DataType typeArgType;
					if (!resolveCompileTimeTypeReference(
							context.parseContext, expr->arguments[1], macroBindingFrameStack, typeArgType
						) ||
						typeArgType.kind != DataType::Kind::Type) {
						break;
					}
					if (typeArgType.referencedKind == DataType::Kind::Type ||
						typeArgType.referencedKind == DataType::Kind::Unresolved) {
						setConfiguredTypeFailure(expr->range, "size of type invalid");
						break;
					}
					expr->type = {DataType::Kind::Int, 8};
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
						concretizeClassType(ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack));
					if (!instType.isDeduced()) {
						context.typesValid = false;
						break;
					}
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
					if (!expr->type.isDeduced() && !fieldName.empty()) {
						setConfiguredTypeFailure(
							expr->range, "class missing property", "message",
							{{"type", typeToUserName(instType, context.parseContext)}, {"property", fieldName}}
						);
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
		for (size_t ai = 0; ai < argTypesForOverload.size(); ai++) {
			if (argTypesForOverload[ai].kind != DataType::Kind::Void)
				continue;
			if (definitionParameterAcceptsVoid(def, expr->patternMatch->nodesPassed, ai))
				continue;
			context.setTypeFailure(renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, expr->arguments[ai]->range), "no overload matches call", "message",
				{{"call", (std::string)expr->range.subString},
				 {"argument_types", formatTypeList(argTypesForOverload, context.parseContext)}}
			));
			break;
		}
		if (!context.typesValid)
			break;
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
						DefinitionPatternElement *parameterElement = findParameterElement(def->patternElements, parameterName);
						if (parameterElement && parameterElement->promotedFromVariableLike && argumentExpression &&
							argumentExpression->kind == Expression::Kind::Variable && argumentExpression->variable &&
							argumentExpression->variable->name == parameterName) {
							ImplicitPromotionTraceResult traceResult = traceImplicitPromotionUse(def, parameterName);
							if (traceResult.reachesStore && traceResult.reportRange.line) {
								context.typeFailureRelatedInfo.push_back(
									{"'" + parameterName + "' is a parameter because it was used here:",
									 traceResult.reportRange}
								);
								if (traceResult.firstNameMismatchReferenceRange.line) {
									context.typeFailureRelatedInfo.push_back(
										{"The first nested parameter name mismatch was here:",
										 traceResult.firstNameMismatchReferenceRange}
									);
								}
							}
						}
						return;
					}
					assert(
						argType.isDeduced() && "Undeduced argument type encountered during non-macro pattern-call inference"
					);
				}
				argTypes.push_back(argType);
			}

			const Instantiation *callerInstantiation = context.currentInstantiation;
			std::vector<std::pair<std::string, CompileTimeValue>> trialCompileTimeParameters;
			std::string trialCacheKey;
			if (context.trial) {
				trialCompileTimeParameters = collectTrialCompileTimeParameters(
					context.parseContext, paramBindings, macroBindingFrameStack, callerInstantiation
				);
				trialCacheKey = buildTrialInstantiationCacheKey(
					matchedSection, !matchedSection->instantiations.empty(), argTypes, trialCompileTimeParameters
				);
				auto trialCache = context.ensureTrialInstantiationCache();
				auto cachedTrial = trialCache->find(trialCacheKey);
				if (cachedTrial != trialCache->end()) {
					traceTrialInstantiationCacheEvent("hit", def, trialCacheKey);
					context.typesValid = true;
					if (cachedTrial->second.returnType.isDeduced())
						expr->type = cachedTrial->second.returnType;
					break;
				}
				traceTrialInstantiationCacheEvent("miss", def, trialCacheKey);
			}

			if (context.trial && context.trialJournal)
				context.trialJournal->recordSectionInstantiationWrite(matchedSection, argTypes);
			Instantiation &inst = matchedSection->instantiations[argTypes];
			if (inst.argumentTypes.empty()) {
				inst.argumentTypes = argTypes;
			} else {
				assert(inst.argumentTypes == argTypes && "Instantiation argumentTypes diverged from map key");
			}
			bool hasReusableInstantiation = inst.valid && inst.returnType.isDeduced() && !inst.needsReinfer;
			if (!inst.inferring && !hasReusableInstantiation) {
				Instantiation *savedInst = context.currentInstantiation;
				auto callerKnownConstants = context.currentKnownConstants;
				callerInstantiation = savedInst;
				bool inferenceSucceeded = runInstantiationReinferenceLoop(
					context, inst, def, expr->range, (std::string)def->range.subString,
					[&]() -> bool {
					if (!context.trial)
						inst.selectedOverloadsByCall.clear();
					if (!context.trial)
						inst.ifChainSelections.clear();
					inst.writtenGlobalReferences.clear();
					inst.finalGlobalConstantValues.clear();
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
						if (std::find(
								matchedSection->globalVariables.begin(), matchedSection->globalVariables.end(),
								paramBindings[i].first
							) != matchedSection->globalVariables.end())
							continue;
						auto bindingValue = makeNonMacroParameterBinding(paramBindings[i].first, argTypes[i], inst);
						nonMacroTypeBindings[paramBindings[i].first] = bindingValue.get();
						ownedNonMacroTypeBindings.push_back(std::move(bindingValue));
					}
					bool passSucceeded = inferSection(matchedSection, context, makeBindingFrameStack(nonMacroTypeBindings));
					inst.finalGlobalConstantValues.clear();
					for (VariableReference *reference : inst.writtenGlobalReferences) {
						auto knownIt = context.currentKnownConstants.find(reference);
						if (knownIt != context.currentKnownConstants.end() && isCompileTimeKnown(knownIt->second))
							inst.finalGlobalConstantValues[reference] = knownIt->second;
					}
					context.suppressReinferPassDiagnostics = savedReinferSuppression;
					inst.inferring = false;
					if (inst.requiredCompileTimeParameters.size() != requiredBeforePass)
						inst.needsReinfer = true;
					return passSucceeded;
				}
				);
				if (!context.trial && inferenceSucceeded && context.typesValid)
					mergeWrittenGlobalConstantsIntoCaller(inst, callerKnownConstants);
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
			if (!context.trial)
				mergeWrittenGlobalConstantsIntoCaller(inst, context.currentKnownConstants);

			// If no return intrinsic was found, default to Void
			if (!inst.inferring && inst.returnType.kind == DataType::Kind::Any) {
				inst.returnType = {DataType::Kind::Void};
			}
			bool canCacheStableTrialInstantiation =
				context.trial && !trialCacheKey.empty() && context.typesValid && inst.valid && !inst.inferring &&
				!inst.needsReinfer && inst.returnType.isDeduced() && !context.observedInProgressUndeducedInstantiation;
			if (canCacheStableTrialInstantiation) {
				TrialInstantiationSummary summary;
				summary.returnType = inst.returnType;
				(*context.ensureTrialInstantiationCache())[trialCacheKey] = std::move(summary);
				traceTrialInstantiationCacheEvent("store", def, trialCacheKey);
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

static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	if (!context.fixedGroupingRoots || !context.fixedGroupingRoots->contains(expr))
		return inferExpression(expr, context, false, bindingFrameStack);
	inferOrderedExpression(expr, context, bindingFrameStack, true);
	return context.typesValid;
}
