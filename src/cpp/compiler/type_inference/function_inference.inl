#pragma once

#include "compilerUtils.h"
#include "const_evaluation.inl"
static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack,
	bool requireVoidResult
);
#include "type_resolution.inl"
#include <bit>
#include <cstdlib>

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
	std::unordered_map<Expression *, CompileTimeValue> constantValuesByExpression;
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
		instantiation.constantValuesByExpression,
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
// Reinference is only for recursive / non-converged non-macro instantiations.
// It must rerun whole-section inference so earlier lines rebuild the callee's
// local variable state before later lines are revisited. Do NOT use this for
// isolated expression or operand-grouping trials.
static bool runInstantiationReinferenceLoop(
	InferenceContext &context, Instantiation &instantiation, PatternDefinition *definition, const Range &fallbackRange,
	std::string functionName, InferPassFn &&inferPass
) {
	size_t reinferPass = 0;
	constexpr size_t maxReinferPasses = 32;
	while (true) {
		InstantiationProgressSnapshot beforePass = snapshotInstantiationProgress(instantiation);
		bool inferenceSucceeded = inferPass();
		InstantiationProgressSnapshot afterPass = snapshotInstantiationProgress(instantiation);
		if (!inferenceSucceeded || !context.typesValid) {
			if (!instantiation.needsReinfer)
				return false;
		} else if (!instantiation.needsReinfer) {
			return true;
		}
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

static CompileTimeValue evaluateCompileTimeValueWithKnownState(
	Expression *expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	(void)bindingFrameStack;
	if (!expr)
		crashCompilerBug("compile-time evaluation requested for null expression");
	return context.lookupExpressionValue(expr);
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
		auto instantiationIt = it->section->instantiations.find(it->key);
		if (it->existed) {
			if (instantiationIt == it->section->instantiations.end()) {
				crashCompilerBug(
					"trial rollback lost a previously-existing section instantiation; instantiation map changed unexpectedly"
				);
			}
			instantiationIt->second = it->value;
			continue;
		}
		if (instantiationIt == it->section->instantiations.end())
			crashCompilerBug("trial rollback expected to erase a provisional section instantiation, but it was missing");
		it->section->instantiations.erase(instantiationIt);
	}
	for (auto it = journal.classInstantiationSizes.rbegin(); it != journal.classInstantiationSizes.rend(); ++it) {
		if (it->first->instantiations.size() < it->second)
			crashCompilerBug("trial rollback observed class instantiation list shrink below recorded snapshot");
		it->first->instantiations.resize(it->second);
	}
}

static std::string encodeTrialCompileTimeValue(const CompileTimeValue &value) {
	return encodeCompileTimeValueForCacheKey(value);
}

static bool instantiationKeyHasOnlyKnownCompileTimeParameters(const InstantiationKey &key) {
	for (const auto &[parameterName, value] : key.compileTimeParameters) {
		(void)parameterName;
		if (!isCompileTimeKnown(value))
			return false;
	}
	return true;
}

static void retargetTrialSectionInstantiationWriteOrCrash(
	InferenceContext &context, Section *section, const InstantiationKey &fromKey, const InstantiationKey &toKey,
	std::string_view operation
) {
	if (!context.trial || fromKey == toKey)
		return;
	if (!instantiationKeyHasOnlyKnownCompileTimeParameters(toKey))
		crashCompilerBug(
			std::string(operation) + " refined a section instantiation key with non-constant compile-time parameters"
		);
	if (!context.trialJournal)
		crashCompilerBug(std::string(operation) + " refined a section instantiation key without an active trial journal");
	InferenceContext::TrialJournal::SectionInstantiationRetargetResult retargetResult =
		context.trialJournal->retargetSectionInstantiationWrite(section, fromKey, toKey);
	switch (retargetResult) {
	case InferenceContext::TrialJournal::SectionInstantiationRetargetResult::Updated:
		return;
	case InferenceContext::TrialJournal::SectionInstantiationRetargetResult::MissingSourceRecord:
		crashCompilerBug(
			std::string(operation) + " refined a section instantiation key, but the provisional key was not journaled"
		);
	case InferenceContext::TrialJournal::SectionInstantiationRetargetResult::SourceWasPreexisting:
		crashCompilerBug(std::string(operation) + " attempted to retarget a preexisting section instantiation journal entry");
	case InferenceContext::TrialJournal::SectionInstantiationRetargetResult::TargetAlreadyRecorded:
		crashCompilerBug(
			std::string(operation) + " attempted to retarget a section instantiation journal entry to an already tracked key"
		);
	}
}

static std::vector<std::pair<std::string, CompileTimeValue>> collectTrialCompileTimeParameters(
	InferenceContext &context, const std::vector<std::pair<std::string, Expression *>> &paramBindings
) {
	std::vector<std::pair<std::string, CompileTimeValue>> values;
	values.reserve(paramBindings.size());
	for (const auto &[name, argExpr] : paramBindings) {
		if (!argExpr)
			crashCompilerBug("missing trial argument expression while collecting compile-time parameters");
		values.push_back({name, context.lookupExpressionValue(argExpr)});
	}
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
		key += encodeDataTypeForCacheKey(type);
	}
	for (const auto &[name, value] : compileTimeParameters) {
		key += "|param:";
		key += name;
		key += "=";
		key += encodeTrialCompileTimeValue(value);
	}
	return key;
}

template <typename EvaluateCompileTimeFn>
static InstantiationKey getOrCreateNonMacroInstantiationKey(
	Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, EvaluateCompileTimeFn &&evaluateCompileTime
) {
	return findMatchingInstantiationKey(section, paramBindings, argTypes, evaluateCompileTime)
		.value_or(buildInstantiationKey({}, paramBindings, argTypes, evaluateCompileTime));
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

static std::unique_ptr<Expression> makeNonMacroParameterBinding(
	ParseContext &parseContext, const std::string &parameterName, const DataType &argType, const Instantiation &instantiation
) {
	(void)parseContext;
	if (!instantiation.requiredCompileTimeParameters.contains(parameterName)) {
		auto typedBinding = std::make_unique<Expression>();
		typedBinding->kind = Expression::Kind::TypedPlaceholder;
		typedBinding->type = argType;
		return typedBinding;
	}

	auto constIt = instantiation.constantParameterValues.find(parameterName);
	if (constIt != instantiation.constantParameterValues.end() && isCompileTimeKnown(constIt->second)) {
		const CompileTimeValue &constantValue = constIt->second;
		if (const auto *typeRef = std::get_if<DataType>(&constantValue)) {
			auto typedBinding = std::make_unique<Expression>();
			typedBinding->kind = Expression::Kind::TypedPlaceholder;
			typedBinding->type = *typeRef;
			return typedBinding;
		}
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
	typedBinding->kind = Expression::Kind::TypedPlaceholder;
	typedBinding->type = argType;
	return typedBinding;
}

static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack,
	bool requireVoidResult = false
);
static void inferOrderedExpression(
	Expression *expr, InferenceContext &context, const BindingFrameStack &macroBindingFrameStack, bool preserveCurrentGrouping
);
static bool inferSection(Section *section, InferenceContext &context, const BindingFrameStack &bindingFrameStack);

static Expression *expandFunctionMacroBodyForInference(
	Expression *expr, PatternDefinition *definition, InferenceContext &context, const BindingFrameStack &bindingFrameStack,
	BindingFrameStack &expandedBindingFrameStack
) {
	if (!expr || !definition || !definition->section || !definition->section->isMacro ||
		definition->section->type != SectionType::Function)
		return nullptr;

	BindingMap innerBindings;
	Expression *bodyExpr = expandMacroPatternCall(context.parseContext, expr, definition, innerBindings);
	if (!bodyExpr)
		return nullptr;

	materializeMacroBindingsInCallerScope(&context.parseContext, innerBindings, bindingFrameStack);
	expandedBindingFrameStack = bindingFrameStack;
	pushBindingScope(expandedBindingFrameStack, std::move(innerBindings));
	return bodyExpr;
}

static DataType requestKnownOrInferExpressionType(
	Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack, bool preserveCurrentGrouping
);
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
	bool acceptsVoid = false;
	forEachPatternParameterName(nodesPassed, definition, [&](const std::string &parameterName, PatternTreeNode *) {
		if (acceptsVoid || currentArgumentIndex != argumentIndex) {
			currentArgumentIndex++;
			return;
		}
		for (const auto &element : definition->patternElements) {
			if (element.type != PatternElement::Type::Variable || element.text != parameterName)
				continue;
			acceptsVoid = element.resolvedTypeConstraint.isDeduced() &&
						  element.resolvedTypeConstraint.kind == DataType::Kind::Void &&
						  element.resolvedTypeConstraint.pointerDepth == 0;
			break;
		}
		currentArgumentIndex++;
	});
	return acceptsVoid;
}

static ArgumentTypeInferenceResult ensureArgumentTypeForPatternCall(
	Expression *argExpr, InferenceContext &context, const BindingFrameStack &callerBindingFrameStack
) {
	ArgumentTypeInferenceResult result;
	Expression *inferExpr = argExpr;
	result.type = requestKnownOrInferExpressionType(inferExpr, context, callerBindingFrameStack, false);
	result.deferred = !result.type.isDeduced() && context.observedInProgressUndeducedInstantiation;
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

static DataType requestKnownOrInferExpressionType(
	Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack, bool preserveCurrentGrouping
) {
	DataType type = resolveKnownExpressionType(expr, bindingFrameStack);
	if (type.isDeduced())
		return type;
	if (!expr)
		return {};
	bool inferred = preserveCurrentGrouping ? inferExpressionWithCurrentGrouping(expr, context, bindingFrameStack)
											: inferExpression(expr, context, false, bindingFrameStack);
	if (!inferred)
		return {};
	return resolveKnownExpressionType(expr, bindingFrameStack);
}

static DataType ensureExpressionType(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	return requestKnownOrInferExpressionType(expr, context, bindingFrameStack, false);
}

static DataType ensureExpressionTypeWithCurrentGrouping(
	Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	return requestKnownOrInferExpressionType(expr, context, bindingFrameStack, true);
}

static bool mergeSelectBranchTypes(const DataType &trueTypeInput, const DataType &falseTypeInput, DataType &outType) {
	DataType trueType = concretizeClassType(trueTypeInput);
	DataType falseType = concretizeClassType(falseTypeInput);
	if (!trueType.isDeduced() || !falseType.isDeduced())
		return false;
	if (trueType == falseType) {
		outType = trueType;
		return true;
	}
	if (trueType.kind == DataType::Kind::Type && falseType.kind == DataType::Kind::Type) {
		outType = {DataType::Kind::Type};
		outType.referencedKind = DataType::Kind::Type;
		return true;
	}
	return DataType::promoteArithmetic(trueType, falseType, outType);
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

static std::optional<double> parseCompileTimeNumericToken(std::string_view token) {
	if (token.empty())
		return std::nullopt;
	bool sawDigit = false;
	bool sawDot = false;
	for (char c : token) {
		if (c >= '0' && c <= '9') {
			sawDigit = true;
			continue;
		}
		if (c == '.') {
			if (sawDot)
				return std::nullopt;
			sawDot = true;
			continue;
		}
		return std::nullopt;
	}
	if (!sawDigit)
		return std::nullopt;
	try {
		return std::stod(std::string(token));
	} catch (...) {
		return std::nullopt;
	}
}

static std::optional<std::int64_t> extractCompileTimeInteger(const CompileTimeValue &value) {
	auto *number = std::get_if<double>(&value);
	if (!number || !std::isfinite(*number))
		return std::nullopt;
	double truncated = std::trunc(*number);
	if (*number != truncated)
		return std::nullopt;
	constexpr std::uint64_t maxExactMagnitude = std::uint64_t{1} << std::numeric_limits<double>::digits;
	if (truncated < -static_cast<double>(maxExactMagnitude) || truncated > static_cast<double>(maxExactMagnitude))
		return std::nullopt;
	if (truncated < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
		truncated > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
		return std::nullopt;
	}
	return static_cast<std::int64_t>(truncated);
}

static std::int64_t compileTimeBitwiseNot(std::int64_t value) {
	return static_cast<std::int64_t>(~static_cast<std::uint64_t>(value));
}

static std::int64_t compileTimeShiftLeft(std::int64_t value, unsigned amount) {
	return static_cast<std::int64_t>(static_cast<std::uint64_t>(value) << amount);
}

static std::int64_t compileTimeShiftRight(std::int64_t value, unsigned amount) {
	if (amount == 0)
		return value;
	std::uint64_t bits = static_cast<std::uint64_t>(value);
	bits >>= amount;
	if (value < 0)
		bits |= (~std::uint64_t{0}) << (64 - amount);
	return static_cast<std::int64_t>(bits);
}

static std::string currentBuildInfo(ParseContext &parseContext, std::string_view key) {
	if (key == "platform")
		return parseContext.options.emitSPIRV ? "gpu" : parseContext.options.emitWASM ? "wasm" : "cpu";
	if (key == "shader stage") {
		if (!parseContext.options.emitSPIRV)
			return "";
		return parseContext.options.shaderStage == ParseContext::ShaderStage::Vertex ? "vertex" : "fragment";
	}
	return {};
}

static std::optional<double> currentBuildInfoNumber(ParseContext &parseContext, std::string_view key) {
	if (key == "word size")
		return (parseContext.options.emitSPIRV || parseContext.options.emitWASM) ? 32.0
																				 : static_cast<double>(sizeof(void *) * 8);
	if (key == "optimization level")
		return static_cast<double>(parseContext.options.optimizationLevel);
	return std::nullopt;
}

static CompileTimeValue
evaluateInferredCompileTimeCast(const CompileTimeValue &value, Expression *typeExpr, InferenceContext &context) {
	if (!typeExpr)
		crashCompilerBug("compile-time cast evaluation encountered null type expression");
	CompileTimeValue typeExprValue = context.lookupExpressionValue(typeExpr);
	auto *typeRef = std::get_if<DataType>(&typeExprValue);
	if (!typeRef || typeRef->kind != DataType::Kind::Type)
		return {};
	DataType targetType = typeRef->toReferencedType();
	if (targetType.kind == DataType::Kind::Bool) {
		std::optional<bool> truthy = compileTimeTruthiness(value);
		return truthy.has_value() ? CompileTimeValue(*truthy) : CompileTimeValue{};
	}
	if (!targetType.isNumeric())
		return {};
	if (const auto *number = std::get_if<double>(&value))
		return *number;
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? 1.0 : 0.0;
	return {};
}

static CompileTimeValue evaluateInferredIntrinsicCompileTimeValue(
	Expression *expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	if (!expr)
		return {};
	auto requireArgument = [&](size_t index, std::string_view intrinsicName) -> Expression * {
		if (expr->arguments.size() <= index || !expr->arguments[index]) {
			crashCompilerBug(
				std::string("intrinsic '") + std::string(intrinsicName) +
				"' is missing an argument while reading compile-time value"
			);
		}
		return expr->arguments[index];
	};
	(void)bindingFrameStack;
	auto compileTimeValueOf = [&](Expression *argumentExpression) {
		return context.lookupExpressionValue(argumentExpression);
	};
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	if (kind == IntrinsicKind::BuildInfo) {
		CompileTimeValue keyValue = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		if (auto *key = std::get_if<std::string>(&keyValue)) {
			if (std::optional<double> number = currentBuildInfoNumber(context.parseContext, *key))
				return *number;
			std::string text = currentBuildInfo(context.parseContext, *key);
			if (!text.empty() || *key == "shader stage")
				return text;
		}
		return {};
	}
	if (kind == IntrinsicKind::SizeOf) {
		CompileTimeValue typeValue = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		auto *typeRef = std::get_if<DataType>(&typeValue);
		if (!typeRef || typeRef->kind != DataType::Kind::Type)
			return {};
		DataType valueType = typeRef->toReferencedType();
		if (valueType.kind == DataType::Kind::Class && valueType.classDefinition && valueType.classInstIndex < 0 &&
			!valueType.classDefinition->instantiations.empty()) {
			valueType.classInstIndex = 0;
		}
		return static_cast<double>(valueType.getByteSize());
	}
	if (kind == IntrinsicKind::Select) {
		CompileTimeValue conditionValue = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		auto *condition = std::get_if<bool>(&conditionValue);
		if (!condition)
			return {};
		return compileTimeValueOf(requireArgument(*condition ? 2 : 3, expr->intrinsicName));
	}
	if (kind == IntrinsicKind::Return && expr->arguments.size() > 1)
		return compileTimeValueOf(requireArgument(1, expr->intrinsicName));
	if (kind == IntrinsicKind::Cast && expr->arguments.size() > 2) {
		CompileTimeValue value = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		if (!isCompileTimeKnown(value))
			return {};
		return evaluateInferredCompileTimeCast(value, requireArgument(2, expr->intrinsicName), context);
	}
	if (kind == IntrinsicKind::Type || kind == IntrinsicKind::TypeOf || kind == IntrinsicKind::Array ||
		kind == IntrinsicKind::Vector || kind == IntrinsicKind::Matrix || kind == IntrinsicKind::AddPointerDepth) {
		return compileTimeValueOf(expr);
	}

	if (kind == IntrinsicKind::Not) {
		CompileTimeValue value = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		auto *boolean = std::get_if<bool>(&value);
		return boolean ? CompileTimeValue(!*boolean) : CompileTimeValue{};
	}
	if (kind == IntrinsicKind::BitwiseNot) {
		CompileTimeValue value = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		std::optional<std::int64_t> integerValue = extractCompileTimeInteger(value);
		return integerValue.has_value() ? CompileTimeValue(static_cast<double>(compileTimeBitwiseNot(*integerValue)))
										: CompileTimeValue{};
	}
	if (kind == IntrinsicKind::Negate) {
		CompileTimeValue value = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		auto *number = std::get_if<double>(&value);
		return number ? CompileTimeValue(-*number) : CompileTimeValue{};
	}

	if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or || kind == IntrinsicKind::Equal ||
		kind == IntrinsicKind::NotEqual || kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr ||
		kind == IntrinsicKind::BitwiseXor || kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight ||
		kind == IntrinsicKind::Add || kind == IntrinsicKind::Subtract || kind == IntrinsicKind::Multiply ||
		kind == IntrinsicKind::Divide || kind == IntrinsicKind::Modulo || kind == IntrinsicKind::LessThan ||
		kind == IntrinsicKind::GreaterThan || kind == IntrinsicKind::LessThanOrEqual ||
		kind == IntrinsicKind::GreaterThanOrEqual) {
		CompileTimeValue leftValue = compileTimeValueOf(requireArgument(1, expr->intrinsicName));
		CompileTimeValue rightValue = compileTimeValueOf(requireArgument(2, expr->intrinsicName));
		if (!isCompileTimeKnown(leftValue) || !isCompileTimeKnown(rightValue))
			return {};

		if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
			auto *leftBool = std::get_if<bool>(&leftValue);
			auto *rightBool = std::get_if<bool>(&rightValue);
			if (!leftBool || !rightBool)
				return {};
			return kind == IntrinsicKind::And ? CompileTimeValue(*leftBool && *rightBool)
											  : CompileTimeValue(*leftBool || *rightBool);
		}

		if (kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) {
			bool result = false;
			if (auto *leftText = std::get_if<std::string>(&leftValue)) {
				if (auto *rightText = std::get_if<std::string>(&rightValue))
					result = *leftText == *rightText;
				else
					return {};
			} else if (auto *leftBool = std::get_if<bool>(&leftValue)) {
				if (auto *rightBool = std::get_if<bool>(&rightValue))
					result = *leftBool == *rightBool;
				else
					return {};
			} else {
				auto *leftNumber = std::get_if<double>(&leftValue);
				auto *rightNumber = std::get_if<double>(&rightValue);
				if (!leftNumber || !rightNumber)
					return {};
				result = *leftNumber == *rightNumber;
			}
			return kind == IntrinsicKind::Equal ? CompileTimeValue(result) : CompileTimeValue(!result);
		}

		if (kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor ||
			kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight) {
			std::optional<std::int64_t> leftInteger = extractCompileTimeInteger(leftValue);
			std::optional<std::int64_t> rightInteger = extractCompileTimeInteger(rightValue);
			if (!leftInteger.has_value() || !rightInteger.has_value())
				return {};
			if (kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight) {
				if (*rightInteger < 0 || *rightInteger >= 64)
					return {};
				unsigned shiftAmount = static_cast<unsigned>(*rightInteger);
				std::int64_t result = kind == IntrinsicKind::ShiftLeft ? compileTimeShiftLeft(*leftInteger, shiftAmount)
																	   : compileTimeShiftRight(*leftInteger, shiftAmount);
				return static_cast<double>(result);
			}
			std::uint64_t leftBits = static_cast<std::uint64_t>(*leftInteger);
			std::uint64_t rightBits = static_cast<std::uint64_t>(*rightInteger);
			std::uint64_t result = kind == IntrinsicKind::BitwiseAnd  ? (leftBits & rightBits)
								   : kind == IntrinsicKind::BitwiseOr ? (leftBits | rightBits)
																	  : (leftBits ^ rightBits);
			return static_cast<double>(static_cast<std::int64_t>(result));
		}

		auto *leftNumber = std::get_if<double>(&leftValue);
		auto *rightNumber = std::get_if<double>(&rightValue);
		if (!leftNumber || !rightNumber)
			return {};
		if (kind == IntrinsicKind::Add)
			return *leftNumber + *rightNumber;
		if (kind == IntrinsicKind::Subtract)
			return *leftNumber - *rightNumber;
		if (kind == IntrinsicKind::Multiply)
			return *leftNumber * *rightNumber;
		if (kind == IntrinsicKind::Divide)
			return *rightNumber == 0.0 ? CompileTimeValue{} : CompileTimeValue(*leftNumber / *rightNumber);
		if (kind == IntrinsicKind::Modulo)
			return *rightNumber == 0.0 ? CompileTimeValue{} : CompileTimeValue(std::fmod(*leftNumber, *rightNumber));
		if (kind == IntrinsicKind::LessThan)
			return *leftNumber < *rightNumber;
		if (kind == IntrinsicKind::GreaterThan)
			return *leftNumber > *rightNumber;
		if (kind == IntrinsicKind::LessThanOrEqual)
			return *leftNumber <= *rightNumber;
		if (kind == IntrinsicKind::GreaterThanOrEqual)
			return *leftNumber >= *rightNumber;
	}

	return {};
}

static Expression *getSingleCompileTimeBody(Section *section) {
	if (!section)
		return nullptr;
	Expression *result = nullptr;
	for (Section *child : section->children) {
		for (CodeLine *line : child->codeLines) {
			if (!line->expression)
				continue;
			if (result)
				return nullptr;
			result = line->expression;
		}
	}
	return result;
}

static CompileTimeValue
inferVariableCompileTimeValue(Expression *expr, InferenceContext &context, const BindingFrameStack &macroBindingFrameStack) {
	if (!expr || !expr->variable)
		return {};
	CompileTimeValue computedValue{};
	Expression *boundExpression = macroBindingFrameStack.lookup(expr->variable->name);
	if (boundExpression && boundExpression != expr) {
		computedValue = context.lookupExpressionValue(boundExpression);
		if (!isCompileTimeKnown(computedValue) && boundExpression->type.kind == DataType::Kind::Type) {
			computedValue = boundExpression->type;
			context.setExpressionValue(boundExpression, computedValue);
		}
		if (!isCompileTimeKnown(computedValue) && boundExpression->kind == Expression::Kind::Variable &&
			boundExpression->variable) {
			Section *boundSection = boundExpression->range.line ? boundExpression->range.line->section : nullptr;
			Variable *boundVariable = boundSection ? boundSection->findVariable(boundExpression->variable->name) : nullptr;
			if (!boundVariable) {
				computedValue = boundExpression->variable->name;
				context.setExpressionValue(boundExpression, computedValue);
			}
		}
	}
	if (!isCompileTimeKnown(computedValue))
		computedValue = context.lookupKnownConstant(expr->variable);
	if (!isCompileTimeKnown(computedValue) && context.currentInstantiation) {
		if (context.currentInstantiation->requiredCompileTimeParameters.contains(expr->variable->name)) {
			auto it = context.currentInstantiation->constantParameterValues.find(expr->variable->name);
			if (it != context.currentInstantiation->constantParameterValues.end())
				computedValue = it->second;
		}
	}
	if (!isCompileTimeKnown(computedValue)) {
		if (std::optional<double> numericToken = parseCompileTimeNumericToken(expr->variable->name))
			computedValue = *numericToken;
	}
	if (!isCompileTimeKnown(computedValue) && expr->type.kind == DataType::Kind::Type)
		computedValue = expr->type;
	return computedValue;
}

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
	auto resolveThroughMacroBindings = [&](Expression *expression) {
		return resolveThroughBindings(expression, macroBindingFrameStack);
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
	auto failCompileTimeOnlyIntrinsicArgument = [&](size_t argumentIndex, std::string_view requirement) {
		if (argumentIndex >= expr->arguments.size() || !expr->arguments[argumentIndex])
			crashCompilerBug("intrinsic compile-time argument validation encountered a missing argument expression");
		std::string detail = "Intrinsic '" + expr->intrinsicName + "' argument " + std::to_string(argumentIndex) + " must be " +
							 std::string(requirement);
		failWithDetail(expr->arguments[argumentIndex]->range, detail, 0);
	};
	// Recurse into arguments first (bottom-up)
	for (size_t i = 0; i < expr->arguments.size(); i++) {
		Expression *arg = expr->arguments[i];
		bool inferred = inferExpression(arg, context, false, macroBindingFrameStack);
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
		DataType elementType = ensureExpressionType(expr->arguments[0], context, macroBindingFrameStack);
		if (!elementType.isDeduced())
			break;
		for (size_t i = 1; i < expr->arguments.size(); i++) {
			DataType nextType = ensureExpressionType(expr->arguments[i], context, macroBindingFrameStack);
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
				DataType boundType = ensureExpressionType(macroBinding, context, macroBindingFrameStack);
				if (boundType.isDeduced())
					expr->type = boundType;
				CompileTimeValue variableValue = inferVariableCompileTimeValue(expr, context, macroBindingFrameStack);
				if (!isCompileTimeKnown(variableValue) && expr->type.kind == DataType::Kind::Type)
					variableValue = expr->type;
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
			if (var->type.isDeduced())
				expr->type = var->type;
		}
		context.setExpressionValue(expr, inferVariableCompileTimeValue(expr, context, macroBindingFrameStack));
		break;
	}

	case Expression::Kind::TypedPlaceholder:
		if (expr->type.kind == DataType::Kind::Type)
			context.setExpressionValue(expr, expr->type);
		else
			context.setExpressionValue(expr, {});
		break;

	case Expression::Kind::IntrinsicCall: {
		const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		bool compileTimeRequirementsChanged = false;
		if (context.currentInstantiation) {
			for (size_t argumentIndex = 1; argumentIndex < expr->arguments.size(); argumentIndex++) {
				if (!intrinsicArgumentIsCompileTimeOnly(expr->intrinsicName, static_cast<int>(argumentIndex)))
					continue;
				if (markCompileTimeParameterRequirements(
						expr->arguments[argumentIndex], macroBindingFrameStack, context.currentInstantiation
					)) {
					compileTimeRequirementsChanged = true;
				}
			}
			if (compileTimeRequirementsChanged && !context.trial)
				context.currentInstantiation->needsReinfer = true;
		}
		if (info) {
			for (size_t argumentIndex = 1; argumentIndex < expr->arguments.size(); argumentIndex++) {
				if (!intrinsicArgumentIsCompileTimeOnly(expr->intrinsicName, static_cast<int>(argumentIndex)))
					continue;
				Expression *argumentExpression = expr->arguments[argumentIndex];
				if (!argumentExpression)
					crashCompilerBug("intrinsic compile-time argument validation encountered null argument expression");
				CompileTimeValue argumentValue = context.lookupExpressionValue(argumentExpression);
				if (!isCompileTimeKnown(argumentValue)) {
					failCompileTimeOnlyIntrinsicArgument(argumentIndex, "compile-time known");
					break;
				}
			}
			if (!context.typesValid)
				break;
		}
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
			case IntrinsicReturnKind::SameAsInts:
				if (expr->arguments.size() == 2) {
					DataType valueType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (!isBitwiseOperandType(valueType)) {
						setConfiguredTypeFailure(
							expr->range, "bitwise operator operand invalid", "message",
							{{"operator", expr->intrinsicName}, {"value_type", typeToUserName(valueType, context.parseContext)}}
						);
						break;
					}
					expr->type = valueType;
				} else {
					DataType leftType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
					DataType result;
					if (!DataType::promoteBitwise(leftType, rightType, result)) {
						setConfiguredTypeFailure(
							expr->range, "bitwise operator operands invalid", "message",
							{{"operator", expr->intrinsicName},
							 {"left_type", typeToUserName(leftType, context.parseContext)},
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
					DataType retType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (retType.isDeduced() && context.currentInstantiation)
						context.currentInstantiation->returnType = retType;
				}
				if ((kind == IntrinsicKind::If || kind == IntrinsicKind::ElseIf || kind == IntrinsicKind::LoopWhile) &&
					expr->arguments.size() > 1) {
					DataType conditionType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (!isLogicalOperandType(conditionType)) {
						std::string operatorLabel = kind == IntrinsicKind::If		? "if condition"
													: kind == IntrinsicKind::ElseIf ? "else if condition"
																					: "loop while condition";
						setConfiguredTypeFailure(
							expr->range, "logical operator operand invalid", "message",
							{{"operator", operatorLabel}, {"value_type", typeToUserName(conditionType, context.parseContext)}}
						);
						break;
					}
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
					DataType varType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (varType.isDeduced())
						expr->type = varType.pointed();
				} else if (kind == IntrinsicKind::Dereference) {
					DataType ptrType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (ptrType.isDeduced() && ptrType.isPointer())
						expr->type = concretizeClassType(ptrType.dereferenced());
				} else if (kind == IntrinsicKind::LoadAt) {
					DataType ptrType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
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
					DataType retTypeRef = ensureExpressionType(expr->arguments[3], context, macroBindingFrameStack);
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
						DataType argType = ensureExpressionType(expr->arguments[i], context, macroBindingFrameStack);
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
					DataType valueType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					DataType typeArgType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
					if (typeArgType.kind != DataType::Kind::Type) {
						failCompileTimeOnlyIntrinsicArgument(2, "a compile-time type reference");
						break;
					}
					if (!isValidCastRuntimeType(valueType)) {
						setConfiguredTypeFailure(
							expr->range, "invalid cast source type", "message",
							{{"source_type", typeToUserName(valueType, context.parseContext)}}
						);
						break;
					}
					DataType castResultType;
					if (typeArgType.kind == DataType::Kind::Type &&
						!tryResolveCastResultType(valueType, typeArgType, castResultType)) {
						DataType requestedType = concretizeClassType(typeArgType.toReferencedType());
						setConfiguredTypeFailure(
							expr->range, "unsupported cast", "message",
							{{"from_type", typeToUserName(valueType, context.parseContext)},
							 {"to_type", typeToUserName(requestedType, context.parseContext)}}
						);
						break;
					}
					expr->type = castResultType;
				} else if (kind == IntrinsicKind::Type) {
					// @intrinsic("type", kindString[, bits])
					// Resolve kind string through macro bindings
					Expression *kindExpr = resolveThroughMacroBindings(expr->arguments[1]);
					std::string kindStr;
					CompileTimeValue kindValue = context.lookupExpressionValue(kindExpr);
					if (auto *str = std::get_if<std::string>(&kindValue))
						kindStr = *str;
					if (kindStr.empty()) {
						failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type kind string");
						break;
					}
					DataType typeRef;
					typeRef.kind = DataType::Kind::Type;
					if (kindStr == "int") {
						typeRef.referencedKind = DataType::Kind::Int;
						typeRef.numericSize = 4; // default
					} else if (kindStr == "float") {
						typeRef.referencedKind = DataType::Kind::Float;
						typeRef.numericSize = defaultFloatByteSize(context.parseContext.options.emitSPIRV); // default
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
					} else {
						failWithDetail(
							expr->arguments[1]->range,
							"Unknown compile-time type kind '" + kindStr + "' in intrinsic '" + expr->intrinsicName + "'", 0
						);
						break;
					}
					// Override byte size if bits argument provided
					if (expr->arguments.size() > 2) {
						Expression *bitsExpr = resolveThroughMacroBindings(expr->arguments[2]);
						CompileTimeValue bitsValue = context.lookupExpressionValue(bitsExpr);
						auto *bits = std::get_if<double>(&bitsValue);
						if (!bits || !std::isfinite(*bits) || std::trunc(*bits) != *bits || *bits <= 0.0 ||
							std::fmod(*bits, 8.0) != 0.0) {
							failCompileTimeOnlyIntrinsicArgument(2, "a positive compile-time integer bit size divisible by 8");
							break;
						}
						typeRef.numericSize = static_cast<int>(*bits) / 8;
					}
					expr->type = typeRef;
					context.setExpressionValue(expr, expr->type);
				} else if (kind == IntrinsicKind::TypeOf) {
					DataType valueType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
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
						context.setExpressionValue(expr, expr->type);
					}
				} else if (kind == IntrinsicKind::Select) {
					DataType conditionType = expr->arguments[1]->type;
					if (!conditionType.isDeduced())
						conditionType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (!isLogicalOperandType(conditionType)) {
						setConfiguredTypeFailure(
							expr->range, "logical operator operand invalid", "message",
							{{"operator", "select condition"},
							 {"value_type", typeToUserName(conditionType, context.parseContext)}}
						);
						break;
					}
					DataType trueType = expr->arguments[2]->type;
					if (!trueType.isDeduced())
						trueType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
					DataType falseType = expr->arguments[3]->type;
					if (!falseType.isDeduced())
						falseType = ensureExpressionType(expr->arguments[3], context, macroBindingFrameStack);
					DataType mergedType;
					if (!mergeSelectBranchTypes(trueType, falseType, mergedType)) {
						setConfiguredTypeFailure(
							expr->range, "incompatible operand types", "message",
							{{"left_type", typeToUserName(trueType, context.parseContext)},
							 {"right_type", typeToUserName(falseType, context.parseContext)}}
						);
						break;
					}
					expr->type = mergedType;
				} else if (kind == IntrinsicKind::SizeOf) {
					markCompileTimeParameterRequirements(
						expr->arguments[1], macroBindingFrameStack, context.currentInstantiation
					);
					Expression *typeExpression = expr->arguments[1];
					if (!inferExpression(typeExpression, context, false, macroBindingFrameStack))
						break;
					expr->arguments[1] = typeExpression;
					DataType typeArgType;
					if (!resolveCompileTimeTypeReference(
							context.parseContext, expr->arguments[1], macroBindingFrameStack, typeArgType, &context
						) ||
						typeArgType.kind != DataType::Kind::Type) {
						failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type reference");
						break;
					}
					if (typeArgType.referencedKind == DataType::Kind::Type ||
						typeArgType.referencedKind == DataType::Kind::Unresolved) {
						setConfiguredTypeFailure(expr->range, "size of type invalid");
						break;
					}
					expr->type = {DataType::Kind::Int, 8};
				} else if (kind == IntrinsicKind::BuildInfo) {
					markCompileTimeParameterRequirements(
						expr->arguments[1], macroBindingFrameStack, context.currentInstantiation
					);
					Expression *keyExpr = expr->arguments[1];
					if (!inferExpression(keyExpr, context, false, macroBindingFrameStack))
						break;
					expr->arguments[1] = keyExpr;
					CompileTimeValue keyValue = context.lookupExpressionValue(keyExpr);
					auto *key = std::get_if<std::string>(&keyValue);
					if (!key) {
						context.setTypeFailure("build info key must be a compile-time string literal");
						break;
					}
					if (*key == "word size" || *key == "optimization level") {
						expr->type = {DataType::Kind::Int, 4};
					} else {
						expr->type = {DataType::Kind::Int, 1};
						expr->type.pointerDepth = 1;
					}
				} else if (kind == IntrinsicKind::Array) {
					Expression *sizeExpr = resolveThroughMacroBindings(expr->arguments[1]);
					CompileTimeValue sizeValue = context.lookupExpressionValue(sizeExpr);
					auto *size = std::get_if<double>(&sizeValue);
					if (!size || !std::isfinite(*size) || std::trunc(*size) != *size || *size < 0.0) {
						failCompileTimeOnlyIntrinsicArgument(1, "a non-negative compile-time integer array size");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Array;
					expr->type.arraySize = static_cast<int>(*size);
					if (expr->arguments.size() > 2) {
						DataType elemTypeRef = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
						if (elemTypeRef.kind != DataType::Kind::Type) {
							failCompileTimeOnlyIntrinsicArgument(2, "a compile-time element type reference");
							break;
						}
						expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
					context.setExpressionValue(expr, expr->type);
				} else if (kind == IntrinsicKind::Vector) {
					int vectorSize = 0;
					if (!evaluateCompileTimeInteger(
							context.parseContext, expr->arguments[1], macroBindingFrameStack, vectorSize,
							context.currentInstantiation
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
						DataType elemTypeRef = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
						if (elemTypeRef.kind != DataType::Kind::Type) {
							failCompileTimeOnlyIntrinsicArgument(2, "a compile-time vector element type reference");
							break;
						}
						expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
					if (!context.typesValid)
						break;
					context.setExpressionValue(expr, expr->type);
				} else if (kind == IntrinsicKind::Matrix) {
					int rows = 0;
					int columns = 0;
					if (!evaluateCompileTimeInteger(
							context.parseContext, expr->arguments[1], macroBindingFrameStack, rows, context.currentInstantiation
						) ||
						!evaluateCompileTimeInteger(
							context.parseContext, expr->arguments[2], macroBindingFrameStack, columns,
							context.currentInstantiation
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
						DataType elemTypeRef = ensureExpressionType(expr->arguments[3], context, macroBindingFrameStack);
						if (elemTypeRef.kind != DataType::Kind::Type) {
							failCompileTimeOnlyIntrinsicArgument(3, "a compile-time matrix element type reference");
							break;
						}
						expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
					if (!context.typesValid)
						break;
					context.setExpressionValue(expr, expr->type);
				} else if (kind == IntrinsicKind::AddPointerDepth) {
					DataType typeArgType = ensureExpressionType(expr->arguments[1], context, macroBindingFrameStack);
					if (typeArgType.kind != DataType::Kind::Type) {
						failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type reference");
						break;
					}
					if (typeArgType.referencedKind == DataType::Kind::Type ||
						typeArgType.referencedKind == DataType::Kind::Unresolved) {
						setConfiguredTypeFailure(expr->range, "pointer to type invalid");
						break;
					}
					expr->type = typeArgType;
					expr->type.pointerDepth++;
					context.setExpressionValue(expr, expr->type);
				} else if (kind == IntrinsicKind::Construct) {
					markCompileTimeParameterRequirements(
						expr->arguments[1], macroBindingFrameStack, context.currentInstantiation
					);
					Expression *typeExpression = expr->arguments[1];
					if (!inferExpression(typeExpression, context, false, macroBindingFrameStack))
						break;
					expr->arguments[1] = typeExpression;
					DataType typeRefType;
					if (!resolveCompileTimeTypeReference(
							context.parseContext, expr->arguments[1], macroBindingFrameStack, typeRefType, &context
						) ||
						typeRefType.kind != DataType::Kind::Type) {
						setConfiguredTypeFailure(expr->range, "construct requires compile-time type reference");
						break;
					}
					if (typeRefType.referencedKind == DataType::Kind::Array) {
						DataType arrayType = typeRefType.toReferencedType();
						if (arrayType.arraySize == static_cast<int>(expr->arguments.size()) - 2) {
							DataType elementType =
								arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
							bool allDeduced = true;
							for (size_t i = 2; i < expr->arguments.size(); i++) {
								DataType argType = ensureExpressionType(expr->arguments[i], context, macroBindingFrameStack);
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
								DataType argType = ensureExpressionType(expr->arguments[i], context, macroBindingFrameStack);
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
							DataType valueType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
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
							DataType argumentType = ensureExpressionType(expr->arguments[i], context, macroBindingFrameStack);
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
									if (!DataType::supportsRuntimeConversion(
											concretizeClassType(argumentTypes[i]), fieldTypes[i]
										))
										allCompatible = false;
								}
								if (allCompatible)
									expr->type = targetType;
							}
						}
					} else if (expr->arguments.size() == 3) {
						DataType targetType = typeRefType.toReferencedType();
						DataType valueType = ensureExpressionType(expr->arguments[2], context, macroBindingFrameStack);
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
					if (fieldName.empty()) {
						failCompileTimeOnlyIntrinsicArgument(2, "a compile-time property name");
						break;
					}
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
		context.setExpressionValue(expr, evaluateInferredIntrinsicCompileTimeValue(expr, context, macroBindingFrameStack));
		break;
	}

	case Expression::Kind::PatternCall: {
		if (!context.trial)
			expr->selectedPatternDefinition = nullptr;
		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;

		// Build argument types for overload selection.
		// Arguments are sorted by source position and include both Variable and Word captures.
		std::vector<DataType> argTypesForOverload;
		for (size_t ai = 0; ai < expr->arguments.size(); ai++) {
			Expression *inferArg = expr->arguments[ai];
			argTypesForOverload.push_back(
				requestKnownOrInferExpressionType(inferArg, context, macroBindingFrameStack, preserveCurrentGrouping)
			);
			expr->arguments[ai] = inferArg;
		}
		auto overloadFailurePriority = [&](Range diagnosticRange) {
			(void)diagnosticRange;
			for (const DataType &argType : argTypesForOverload) {
				if (argType.kind == DataType::Kind::Void)
					return 0;
			}
			return 1;
		};

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
			std::string detail = renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, expr->range), "no overload matches call",
				candidates.empty() ? "message" : "with overloads",
				{{"call", (std::string)expr->range.subString},
				 {"argument_types", formatTypeList(argTypesForOverload, context.parseContext)},
				 {"overloads", candidates}}
			);
			context.setTypeFailure(detail);
			context.fail(buildFailureDetailDiagnostic(expr->range, detail), overloadFailurePriority(expr->range));
			break;
		}
		for (size_t ai = 0; ai < argTypesForOverload.size(); ai++) {
			if (argTypesForOverload[ai].kind != DataType::Kind::Void)
				continue;
			if (definitionParameterAcceptsVoid(def, expr->patternMatch->nodesPassed, ai))
				continue;
			std::string detail = renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, expr->arguments[ai]->range), "no overload matches call", "message",
				{{"call", (std::string)expr->range.subString},
				 {"argument_types", formatTypeList(argTypesForOverload, context.parseContext)}}
			);
			context.setTypeFailure(detail);
			context.fail(buildFailureDetailDiagnostic(expr->range, detail), 0);
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
		if (matchedSection->isMacro) {
			materializeMacroBindingsInCallerScope(&context.parseContext, callBindings, macroBindingFrameStack);
		} else if (matchedSection->type == SectionType::Class) {
			// Class type references often forward dimension/template variables with
			// the same parameter names (for example n -> n). Resolve through caller
			// bindings first so we don't shadow the outer value with a self-binding.
			for (auto &[parameterName, argumentExpression] : callBindings) {
				(void)parameterName;
				if (Expression *resolvedArgument = resolveThroughBindings(argumentExpression, macroBindingFrameStack))
					argumentExpression = resolvedArgument;
			}
		}
		BindingFrameStack callBindingFrameStack = macroBindingFrameStack;
		pushBindingScope(callBindingFrameStack, callBindings);

		if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
			if (context.currentInstantiation)
				markCompileTimeParameterRequirements(expr, macroBindingFrameStack, context.currentInstantiation);
			auto *classSec = static_cast<ClassSection *>(matchedSection);
			expr->type =
				instantiateBoundClassType(context.parseContext, classSec->classDefinition, callBindingFrameStack, &context);
			if (expr->type.kind == DataType::Kind::Type)
				context.setExpressionValue(expr, expr->type);
		} else if (matchedSection->isMacro && matchedSection->type == SectionType::Function) {
			BindingFrameStack expandedBindingFrameStack;
			Expression *bodyExpr =
				expandFunctionMacroBodyForInference(expr, def, context, macroBindingFrameStack, expandedBindingFrameStack);
			if (!bodyExpr) {
				context.setTypeFailure("macro body expansion failed during type inference");
				break;
			}
			if (!inferExpression(bodyExpr, context, false, expandedBindingFrameStack))
				break;
			if (!context.trial)
				expr->inferredMacroExpansion = bodyExpr;
			DataType resolvedType = bodyExpr->type;
			if (resolvedType.isDeduced())
				expr->type = resolvedType;
			context.setExpressionValue(expr, context.lookupExpressionValue(bodyExpr));
		} else if (matchedSection->isMacro) {
			// Section macros still infer their shared section structure.
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
					DataType resolvedType = ensureExpressionType(line->expression, context, callBindingFrameStack);
					if (resolvedType.isDeduced()) {
						line->expression->type = resolvedType;
						expr->type = resolvedType;
						context.setExpressionValue(expr, context.lookupExpressionValue(line->expression));
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
				trialCompileTimeParameters = collectTrialCompileTimeParameters(context, paramBindings);
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

			auto evaluateParameterValue = [&](Expression *argumentExpression) {
				(void)macroBindingFrameStack;
				if (!argumentExpression)
					crashCompilerBug("missing non-macro argument while building inference instantiation key");
				return context.lookupExpressionValue(argumentExpression);
			};
			InstantiationKey instantiationKey =
				getOrCreateNonMacroInstantiationKey(matchedSection, paramBindings, argTypes, evaluateParameterValue);
			if (context.trial && context.trialJournal)
				context.trialJournal->recordSectionInstantiationWrite(matchedSection, instantiationKey);
			Instantiation &inst = matchedSection->instantiations[instantiationKey];
			if (inst.argumentTypes.empty()) {
				inst.argumentTypes = argTypes;
			} else {
				assert(inst.argumentTypes == argTypes && "Instantiation argumentTypes diverged from map key");
			}
			std::optional<InstantiationKey> refinedInstantiationKey;
			bool hasReusableInstantiation = inst.valid && inst.returnType.isDeduced() && !inst.needsReinfer;
			if (!inst.inferring && !hasReusableInstantiation) {
				Instantiation *savedInst = context.currentInstantiation;
				auto callerKnownConstants = context.currentKnownConstants;
				bool inferenceSucceeded = runInstantiationReinferenceLoop(
					context, inst, def, expr->range, (std::string)def->range.subString,
					[&]() -> bool {
					if (!context.trial)
						inst.selectedOverloadsByCall.clear();
					if (!context.trial)
						inst.ifChainSelections.clear();
					inst.constantValuesByExpression.clear();
					inst.writtenGlobalReferences.clear();
					inst.finalGlobalConstantValues.clear();
					for (size_t i = 0; i < paramBindings.size() && i < argTypes.size(); i++) {
						if (argTypes[i].kind == DataType::Kind::Type)
							inst.requiredCompileTimeParameters.insert(paramBindings[i].first);
					}
					seedInstantiationCompileTimeParameters(inst, paramBindings, argTypes, [&](Expression *argumentExpression) {
						if (context.trial) {
							auto trialIt = context.trialExpressionValues.find(argumentExpression);
							if (trialIt != context.trialExpressionValues.end())
								return trialIt->second;
							if (context.inheritedTrialExpressionValues) {
								auto inheritedIt = context.inheritedTrialExpressionValues->find(argumentExpression);
								if (inheritedIt != context.inheritedTrialExpressionValues->end())
									return inheritedIt->second;
							}
						}
						return getExpressionCompileTimeValue(context.parseContext, argumentExpression, callerInstantiation);
					});
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
						auto bindingValue =
							makeNonMacroParameterBinding(context.parseContext, paramBindings[i].first, argTypes[i], inst);
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
				refinedInstantiationKey =
					buildInstantiationKey(inst.requiredCompileTimeParameters, paramBindings, argTypes, evaluateParameterValue);
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
			if (Expression *bodyExpr = getSingleCompileTimeBody(matchedSection))
				context.setExpressionValue(expr, context.lookupExpressionValue(bodyExpr));
			if (refinedInstantiationKey && *refinedInstantiationKey != instantiationKey) {
				retargetTrialSectionInstantiationWriteOrCrash(
					context, matchedSection, instantiationKey, *refinedInstantiationKey, "trial inference"
				);
				auto instIt = matchedSection->instantiations.find(instantiationKey);
				assert(instIt != matchedSection->instantiations.end() && "Missing provisional instantiation to refine");
				auto node = matchedSection->instantiations.extract(instIt);
				node.key() = *refinedInstantiationKey;
				auto insertResult = matchedSection->instantiations.insert(std::move(node));
				assert(insertResult.inserted && "Refined instantiation key collided with existing entry");
			}
			context.setExpressionValue(expr, context.lookupExpressionValue(expr));
			break;
		}
	}

	case Expression::Kind::Pending:
		context.setExpressionValue(expr, {});
		break;
	}
	CompileTimeValue inferredValue = context.lookupExpressionValue(expr);
	if (!isCompileTimeKnown(inferredValue) && expr->type.kind == DataType::Kind::Type)
		context.setExpressionValue(expr, expr->type);
}

static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	inferOrderedExpression(expr, context, bindingFrameStack, true);
	return context.typesValid;
}
