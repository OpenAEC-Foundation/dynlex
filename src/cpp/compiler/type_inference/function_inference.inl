#pragma once

#include "compilerUtils.h"
#include "const_evaluation.inl"
static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &flexBindingFrameStack,
	bool requireVoidResult
);
static DataType ensureExpressionType(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
static void resetExpressionTypes(Expression *expr);
#include "type_resolution.inl"
#include <bit>
#include <cstdlib>
#include <memory>

static void resetSectionExpressionTypes(Section *section, InstantiatedSectionBody *body = nullptr);
static void recomputeRanges(Expression *expr);
static bool startsWithArgument(Expression *expression);
static bool endsWithArgument(Expression *expression);

static bool patternElementsHaveUnresolvedTypeConstraint(const std::vector<DefinitionPatternElement> &elements) {
	for (const DefinitionPatternElement &element : elements) {
		if (element.type == PatternElement::Type::Choice) {
			for (const auto &alternative : element.alternatives) {
				if (patternElementsHaveUnresolvedTypeConstraint(alternative))
					return true;
			}
			continue;
		}
		if (!element.typeConstraintName.empty() && !element.resolvedTypeConstraint.isResolved())
			return true;
	}
	return false;
}

static bool definitionsHaveUnresolvedTypeConstraints(const std::vector<PatternDefinition *> &definitions) {
	return std::any_of(definitions.begin(), definitions.end(), [](PatternDefinition *definition) {
		return definition && patternElementsHaveUnresolvedTypeConstraint(definition->patternElements);
	});
}

struct InstantiationProgressSnapshot {
	DataType returnType;
	std::vector<DataType> argumentTypes;
	std::unordered_map<std::string, CompileTimeValue> constantParameterValues;
	std::unordered_set<VariableReference *> writtenGlobalReferences;
	std::unordered_map<VariableReference *, CompileTimeValue> finalGlobalConstantValues;
	std::unordered_set<std::string> requiredCompileTimeParameters;
	InstantiationPurity purity;

	bool operator==(const InstantiationProgressSnapshot &other) const = default;
};

static InstantiationProgressSnapshot snapshotInstantiationProgress(const Instantiation &instantiation) {
	return {
		instantiation.returnType,
		instantiation.argumentTypes,
		instantiation.constantParameterValues,
		instantiation.writtenGlobalReferences,
		instantiation.finalGlobalConstantValues,
		instantiation.requiredCompileTimeParameters,
		instantiation.purity,
	};
}

// Applies a callee's global write effects to the caller context, mirroring a
// direct store to each written global: the caller's own effect summary, its
// tracked constant value, and the surrounding loop mutation scope. Skipping
// any of these would let later code fold branches against values the callee
// already overwrote, or treat a loop-carried global as loop-invariant.
static void mergeCalleeGlobalWritesIntoCaller(InferenceContext &context, const Instantiation &inst) {
	for (VariableReference *reference : inst.writtenGlobalReferences) {
		context.noteWrittenGlobalReference(reference);
		auto it = inst.finalGlobalConstantValues.find(reference);
		context.setKnownConstant(reference, it != inst.finalGlobalConstantValues.end() ? it->second : CompileTimeValue{});
		if (context.inLoopMutationScope()) {
			context.noteLoopMutation(reference);
			context.setKnownConstant(reference, {});
		}
	}
}

static void markCurrentInstantiationImpure(InferenceContext &context) {
	if (!context.currentInstantiation || context.currentInstantiation->purity == InstantiationPurity::Impure)
		return;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "Trial purity mutation requires a rollback journal");
		context.trialJournal->recordInstantiationPurityWrite(context.currentInstantiation);
	}
	context.currentInstantiation->purity = InstantiationPurity::Impure;
}

static void mergeInstantiationPurityIntoCaller(InferenceContext &context, const Instantiation &inst) {
	if (inst.purity == InstantiationPurity::Impure)
		markCurrentInstantiationImpure(context);
}

static Variable *findVariableForExpression(Expression *expression) {
	if (!expression || expression->kind != Expression::Kind::Variable || !expression->variable || !expression->range.line)
		return nullptr;
	return expression->range.line->section ? expression->range.line->section->findVariable(expression->variable->name)
										   : nullptr;
}

static bool variableIsCurrentInstantiationParameter(const InferenceContext &context, Variable *variable) {
	return context.currentInstantiation && variable &&
		   context.currentInstantiation->parameterTypesByName.contains(variable->name);
}

static InstantiationPurity classifyPropertyIntrinsicPurity(
	Expression *propertyExpr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack,
	bool writingThroughProperty = false
) {
	if (!propertyExpr || propertyExpr->arguments.size() <= 1)
		crashCompilerBug("property purity classification encountered malformed intrinsic arguments");
	BindingFrameStack ownerBindingFrameStack;
	Expression *ownerExpr =
		resolveThroughBindingsDeep(propertyExpr->arguments[1], flexBindingFrameStack, ownerBindingFrameStack);
	if (!ownerExpr)
		return InstantiationPurity::Impure;
	DataType ownerType = ownerExpr->type;
	if (!ownerType.isDeduced())
		ownerType = ensureExpressionType(ownerExpr, context, ownerBindingFrameStack);
	if (!ownerType.isDeduced())
		return InstantiationPurity::Impure;
	if (ownerType.isBytePointer()) {
		Variable *ownerVariable = findVariableForExpression(ownerExpr);
		return ownerVariable && ownerVariable->isGlobal ? InstantiationPurity::Impure : InstantiationPurity::Pure;
	}
	if (ownerType.isPointer())
		return InstantiationPurity::Impure;
	Variable *ownerVariable = findVariableForExpression(ownerExpr);
	if (ownerVariable && ownerVariable->isGlobal)
		return InstantiationPurity::Impure;
	if (writingThroughProperty && variableIsCurrentInstantiationParameter(context, ownerVariable))
		return InstantiationPurity::Impure;
	return InstantiationPurity::Pure;
}

static InstantiationPurity
classifyStoreIntrinsicPurity(Expression *storeExpr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack) {
	if (!storeExpr || storeExpr->arguments.size() <= 1)
		crashCompilerBug("store purity classification encountered malformed intrinsic arguments");
	BindingFrameStack destinationBindingFrameStack;
	Expression *destinationExpr =
		resolveThroughBindingsDeep(storeExpr->arguments[1], flexBindingFrameStack, destinationBindingFrameStack);
	if (!destinationExpr)
		return InstantiationPurity::Impure;
	if (destinationExpr->kind == Expression::Kind::Variable) {
		Variable *variable = findVariableForExpression(destinationExpr);
		if (!variable)
			return InstantiationPurity::Impure;
		if (variableIsCurrentInstantiationParameter(context, variable))
			return InstantiationPurity::Impure;
		return variable->isGlobal ? InstantiationPurity::Impure : InstantiationPurity::Pure;
	}
	if (destinationExpr->kind == Expression::Kind::IntrinsicCall &&
		intrinsicKind(destinationExpr->intrinsicName) == IntrinsicKind::Property) {
		BindingFrameStack resolvedBindingFrameStack = flexBindingFrameStack;
		destinationBindingFrameStack.forEachFrame([&](const BindingFrame &frame) {
			pushBindingScope(resolvedBindingFrameStack, frame);
		});
		return classifyPropertyIntrinsicPurity(destinationExpr, context, resolvedBindingFrameStack, true);
	}
	return InstantiationPurity::Impure;
}

static InstantiationPurity
classifyCustomIntrinsicPurity(Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack) {
	if (!expr)
		return InstantiationPurity::Impure;
	switch (intrinsicKind(expr->intrinsicName)) {
	case IntrinsicKind::Store:
		return classifyStoreIntrinsicPurity(expr, context, flexBindingFrameStack);
	case IntrinsicKind::Property:
		return classifyPropertyIntrinsicPurity(expr, context, flexBindingFrameStack);
	default:
		return InstantiationPurity::Impure;
	}
}

static void
markIntrinsicImpurityIfNeeded(Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack) {
	if (!expr || !context.currentInstantiation)
		return;
	switch (intrinsicPurityKind(intrinsicKind(expr->intrinsicName))) {
	case IntrinsicPurityKind::Pure:
		return;
	case IntrinsicPurityKind::Impure:
		markCurrentInstantiationImpure(context);
		return;
	case IntrinsicPurityKind::Custom:
		if (classifyCustomIntrinsicPurity(expr, context, flexBindingFrameStack) == InstantiationPurity::Impure)
			markCurrentInstantiationImpure(context);
		return;
	}
	crashCompilerBug("unhandled intrinsic purity kind");
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
// Reinference is only for recursive / non-converged non-flex instantiations.
// It must rerun whole-section inference so earlier lines rebuild the callee's
// local variable state before later lines are revisited. Do NOT use this for
// isolated expression or operand-grouping trials.
static bool runInstantiationReinferenceLoop(
	InferenceContext &context, Instantiation &instantiation, PatternDefinition *definition, const Range &fallbackRange,
	std::string functionName, bool canDeferToCaller, InferPassFn &&inferPass
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
			if (canDeferToCaller && context.observedInProgressUndeducedInstantiation && instantiation.returnType.isDeduced())
				return true;
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

static void rollbackTrialJournal(InferenceContext::TrialJournal &journal) {
	for (Section *section : journal.touchedSections)
		resetSectionExpressionTypes(section);
	for (auto it = journal.variableTypeUndo.rbegin(); it != journal.variableTypeUndo.rend(); ++it) {
		it->variable->type = it->type;
		it->variable->typeOriginRange = it->typeOriginRange;
		it->variable->typeOriginFloatLiteralReplacement = it->typeOriginFloatLiteralReplacement;
	}
	for (auto it = journal.instantiationPurityUndo.rbegin(); it != journal.instantiationPurityUndo.rend(); ++it)
		it->instantiation->purity = it->purity;
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

template <typename ReadValueFn>
static std::vector<std::pair<std::string, CompileTimeValue>> collectRequiredCompileTimeParameters(
	const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::unordered_set<std::string> &requiredCompileTimeParameters, ReadValueFn &&readValue
) {
	std::vector<std::pair<std::string, CompileTimeValue>> values;
	values.reserve(requiredCompileTimeParameters.size());
	for (const auto &[name, argExpr] : paramBindings) {
		if (!requiredCompileTimeParameters.contains(name))
			continue;
		if (!argExpr)
			crashCompilerBug("missing trial argument expression while collecting compile-time parameters");
		values.push_back({name, readValue(argExpr)});
	}
	return values;
}

static std::vector<std::pair<std::string, CompileTimeValue>> collectTrialCompileTimeParameters(
	InferenceContext &context, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::unordered_set<std::string> &requiredCompileTimeParameters
) {
	return collectRequiredCompileTimeParameters(
		paramBindings, requiredCompileTimeParameters,
		[&](Expression *argumentExpression) {
		return context.lookupExpressionValue(argumentExpression);
	}
	);
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

template <typename ReadCompileTimeFn>
static InstantiationKey getOrCreateNonFlexInstantiationKey(
	Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, const std::unordered_set<std::string> &requiredCompileTimeParameters,
	ReadCompileTimeFn &&readCompileTime
) {
	return findMatchingInstantiationKey(section, paramBindings, argTypes, readCompileTime)
		.value_or(buildInstantiationKey(requiredCompileTimeParameters, paramBindings, argTypes, readCompileTime));
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

static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &flexBindingFrameStack,
	bool requireVoidResult = false
);
static void inferOrderedExpression(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack, bool preserveCurrentGrouping
);
static bool inferSection(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack
);

static DataType requestKnownOrInferExpressionType(
	Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack, bool preserveCurrentGrouping
);
static DataType ensureExpressionType(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack);
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
	forEachPatternParameterName(nodesPassed, definition, [&](const std::string &, PatternTreeNode *node) {
		if (acceptsVoid || currentArgumentIndex != argumentIndex) {
			currentArgumentIndex++;
			return;
		}
		const DefinitionPatternElement *element = matchedPatternParameterElement(definition, node);
		requireCompilerInvariant(element != nullptr, "matched pattern parameter has no definition element");
		acceptsVoid = element->resolvedTypeConstraint.isResolved() &&
					  element->resolvedTypeConstraint.accepts(DataType{DataType::Kind::Void}, false);
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

static void resetSectionExpressionTypes(Section *section, InstantiatedSectionBody *body) {
	if (!section)
		return;
	for (size_t i = 0; i < section->codeLines.size(); i++) {
		Expression *expression = body ? body->lineExpression(i) : section->codeLines[i]->expression;
		if (expression)
			resetExpressionTypes(expression);
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

struct ScopedSectionLocalVariableState {
	struct Entry {
		Variable *variable;
		DataType type;
		Range typeOriginRange;
		std::string typeOriginFloatLiteralReplacement;
	};
	std::vector<Entry> entries;

	explicit ScopedSectionLocalVariableState(Section *section) {
		std::function<void(Section *)> collect = [&](Section *current) {
			if (!current)
				return;
			for (const auto &[name, variable] : current->variables) {
				(void)name;
				if (!variable || variable->isGlobal)
					continue;
				entries.push_back(
					{variable, variable->type, variable->typeOriginRange, variable->typeOriginFloatLiteralReplacement}
				);
			}
			for (Section *child : current->children)
				collect(child);
		};
		collect(section);
	}

	~ScopedSectionLocalVariableState() {
		for (const Entry &entry : entries) {
			entry.variable->type = entry.type;
			entry.variable->typeOriginRange = entry.typeOriginRange;
			entry.variable->typeOriginFloatLiteralReplacement = entry.typeOriginFloatLiteralReplacement;
		}
	}
};

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

static bool expressionIsLValueOnlyUse(const InferenceContext &context, Expression *expr) {
	if (!expr || context.expressionStack.size() < 2)
		return false;
	Expression *consumer = context.expressionStack[context.expressionStack.size() - 2];
	if (!consumer || consumer->kind != Expression::Kind::IntrinsicCall)
		return false;
	IntrinsicKind kind = intrinsicKind(consumer->intrinsicName);
	if ((kind == IntrinsicKind::Store || kind == IntrinsicKind::AddressOf) && consumer->arguments.size() > 1)
		return expressionContainsTargetExpression(consumer->arguments[1], expr);
	return false;
}

static std::optional<double> parseCompileTimeNumericToken(std::string_view token);

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
	const Range &deepestNonFlexReferenceRange, std::unordered_set<std::string> &visited
) {
	if (!definition || !definition->section)
		return {};

	std::string visitKey = std::to_string(reinterpret_cast<uintptr_t>(definition)) + "|" + parameterName;
	if (!visited.insert(visitKey).second)
		return {};

	Expression *firstReference = findFirstNamedReferenceInSection(definition->section, parameterName);
	if (!firstReference || !firstReference->range.line || !firstReference->range.line->expression)
		return {};

	Range currentDeepestNonFlexReferenceRange = deepestNonFlexReferenceRange;
	if (!definition->section->isFlex && definition->section->type == SectionType::Function)
		currentDeepestNonFlexReferenceRange = firstReference->range;

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
					currentDeepestNonFlexReferenceRange.line ? currentDeepestNonFlexReferenceRange : firstReference->range;
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
				currentDeepestNonFlexReferenceRange, visited
			);
		}
	}

	return {};
}

static ImplicitPromotionTraceResult traceImplicitPromotionUse(PatternDefinition *definition, const std::string &parameterName) {
	std::unordered_set<std::string> visited;
	return traceImplicitPromotionUse(definition, parameterName, {}, {}, visited);
}

static void appendImplicitPromotionTrace(
	std::vector<RelatedInfo> &relatedInfo, PatternDefinition *definition, const std::string &parameterName
) {
	ImplicitPromotionTraceResult traceResult = traceImplicitPromotionUse(definition, parameterName);
	if (!traceResult.reachesStore || !traceResult.reportRange.line)
		return;
	auto appendUnique = [&](std::string message, Range range) {
		for (const RelatedInfo &existing : relatedInfo) {
			if (existing.message == message && existing.range.line == range.line &&
				existing.range.subString.data() == range.subString.data() &&
				existing.range.subString.size() == range.subString.size())
				return;
		}
		relatedInfo.push_back({std::move(message), range});
	};
	appendUnique("'" + parameterName + "' is a parameter because it was used here:", traceResult.reportRange);
	if (traceResult.firstNameMismatchReferenceRange.line) {
		appendUnique("The first nested parameter name mismatch was here:", traceResult.firstNameMismatchReferenceRange);
	}
}

// Find the pattern element range of an unbound parameter in the sections that
// own the active instantiation, or an empty range when the name is not an
// unbound parameter of this call.
static Range findUnboundParameterElementRange(InferenceContext &context, const std::string &name) {
	if (!context.currentInstantiation || !context.currentInstantiation->body)
		return {};
	if (context.currentInstantiation->parameterTypesByName.contains(name))
		return {};
	Section *ownerSection = context.currentInstantiation->body->sourceSection;
	if (!ownerSection)
		return {};
	for (PatternDefinition *definition : ownerSection->patternDefinitions) {
		DefinitionPatternElement *element = findParameterElement(definition->patternElements, name);
		if (!element)
			continue;
		return Range(
			definition->range.line, definition->range.start() + static_cast<int>(element->startPos),
			definition->range.start() + static_cast<int>(element->startPos + element->text.length())
		);
	}
	return {};
}

// An argument can stay unresolved because it reads a pattern parameter the
// active call's match did not bind (its choice alternative was not taken).
// The generic overload failure would hide that cause, so name the parameter.
static void
appendUnboundParameterTrace(InferenceContext &context, Expression *argumentExpression, std::vector<RelatedInfo> &relatedInfo) {
	std::function<void(Expression *)> visit = [&](Expression *expression) {
		if (!expression || expression->type.isDeduced())
			return;
		if (expression->kind == Expression::Kind::Variable && expression->variable) {
			const std::string &name = expression->variable->name;
			Range elementRange = findUnboundParameterElementRange(context, name);
			if (!elementRange.line)
				return;
			std::string message = renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, elementRange), "unbound choice parameter", "message",
				{{"name", name}}
			);
			for (const RelatedInfo &existing : relatedInfo) {
				if (existing.message == message)
					return;
			}
			relatedInfo.push_back({std::move(message), elementRange});
			return;
		}
		for (Expression *argument : expression->arguments)
			visit(argument);
	};
	visit(argumentExpression);
}

static DataType requestKnownOrInferExpressionType(
	Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack, bool preserveCurrentGrouping
) {
	auto readKnownType = [&](Expression *expression) -> DataType {
		if (!expression || !expression->type.isDeduced())
			return {};
		return concretizeClassType(expression->type);
	};
	DataType type = readKnownType(expr);
	if (type.isDeduced())
		return type;
	if (!expr)
		return {};
	bool inferred = preserveCurrentGrouping ? inferExpressionWithCurrentGrouping(expr, context, bindingFrameStack)
											: inferExpression(expr, context, false, bindingFrameStack);
	if (!inferred)
		return {};
	return readKnownType(expr);
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
	if (trueType.isNumeric() && falseType.isNumeric())
		return DataType::promoteArithmetic(trueType, falseType, outType);
	if (trueType.isVector() && falseType.isVector() && trueType.vectorSize() == falseType.vectorSize())
		return DataType::promoteArithmetic(trueType, falseType, outType);
	if (trueType.isMatrix() && falseType.isMatrix() && trueType.matrixRows() == falseType.matrixRows() &&
		trueType.matrixColumns() == falseType.matrixColumns()) {
		return DataType::promoteArithmetic(trueType, falseType, outType);
	}
	return false;
}

static void commitVariableTypeFromValue(Variable *var, Expression *valueExpr, const DataType &valueType) {
	if (!var)
		return;
	var->type = concretizeClassType(valueType);
	var->typeOriginRange = valueExpr ? valueExpr->range : Range();
	var->typeOriginFloatLiteralReplacement = makeFloatLiteralReplacement(valueExpr);
}

static bool isVariableAssignmentCompatible(const DataType &targetType, const DataType &valueType) {
	DataType concreteTargetType = targetType;
	DataType concreteValueType = valueType;
	if (!concreteTargetType.isDeduced() || !concreteValueType.isDeduced())
		return false;
	if (concreteTargetType == concreteValueType)
		return true;
	return concreteTargetType.kind == DataType::Kind::Int && concreteValueType.kind == DataType::Kind::Int &&
		   concreteTargetType.pointerDepth == 0 && concreteValueType.pointerDepth == 0;
}

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Expression *valueExpr, const DataType &valueType, ParseContext &parseContext);
static Diagnostic buildAssignmentTypeChangeDiagnostic(
	const std::string &name, const DataType &currentType, Range currentTypeOriginRange,
	const std::string &currentTypeOriginFloatLiteralReplacement, Expression *valueExpr, const DataType &valueType,
	ParseContext &parseContext
);
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

template <typename ReadArgumentValueFn, typename ReadStoredValueFn>
static CompileTimeValue evaluatePureIntrinsicCompileTimeValue(
	Expression *expr, ParseContext &parseContext, ReadArgumentValueFn &&readArgumentValue, ReadStoredValueFn &&readStoredValue
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
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	if (kind == IntrinsicKind::BuildInfo) {
		CompileTimeValue keyValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (auto *key = std::get_if<std::string>(&keyValue))
			return currentBuildInfoValue(parseContext, *key);
		return {};
	}
	if (kind == IntrinsicKind::TargetIs) {
		CompileTimeValue targetValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (auto *targetName = std::get_if<std::string>(&targetValue))
			if (std::optional<bool> result = evaluateTargetIs(parseContext, *targetName))
				return *result;
		return {};
	}
	if (kind == IntrinsicKind::ShaderStageIs) {
		CompileTimeValue shaderStageValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (auto *shaderStageName = std::get_if<std::string>(&shaderStageValue))
			if (std::optional<bool> result = evaluateShaderStageIs(parseContext, *shaderStageName))
				return *result;
		return {};
	}
	if (kind == IntrinsicKind::SizeOf) {
		CompileTimeValue typeValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		auto *typeRef = std::get_if<TypeReferenceValue>(&typeValue);
		if (!typeRef || typeRef->type.kind != DataType::Kind::Type)
			return {};
		DataType valueType = typeRef->type.toReferencedType();
		if (valueType.kind == DataType::Kind::Class && valueType.classDefinition && valueType.classInstIndex < 0 &&
			!valueType.classDefinition->instantiations.empty()) {
			valueType.classInstIndex = 0;
		}
		return static_cast<double>(valueType.getByteSize());
	}
	if (kind == IntrinsicKind::Select) {
		CompileTimeValue conditionValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		CompileTimeValue leftValue = readArgumentValue(requireArgument(2, expr->intrinsicName));
		CompileTimeValue rightValue = readArgumentValue(requireArgument(3, expr->intrinsicName));
		(void)leftValue;
		(void)rightValue;
		auto *condition = std::get_if<bool>(&conditionValue);
		if (!condition)
			return {};
		return readArgumentValue(requireArgument(*condition ? 2 : 3, expr->intrinsicName));
	}
	if (kind == IntrinsicKind::Cast && expr->arguments.size() > 2) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (!isCompileTimeKnown(value))
			return {};
		CompileTimeValue typeValue = readArgumentValue(requireArgument(2, expr->intrinsicName));
		auto *typeRef = std::get_if<TypeReferenceValue>(&typeValue);
		if (!typeRef || typeRef->type.kind != DataType::Kind::Type)
			return {};
		DataType targetType = typeRef->type.toReferencedType();
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
	if (kind == IntrinsicKind::Type || kind == IntrinsicKind::Fix || kind == IntrinsicKind::TypeOf ||
		kind == IntrinsicKind::Array || kind == IntrinsicKind::Vector || kind == IntrinsicKind::Matrix ||
		kind == IntrinsicKind::AddPointerDepth) {
		return readStoredValue(expr);
	}

	if (kind == IntrinsicKind::Not) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		auto *boolean = std::get_if<bool>(&value);
		return boolean ? CompileTimeValue(!*boolean) : CompileTimeValue{};
	}
	if (kind == IntrinsicKind::BitwiseNot) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		std::optional<std::int64_t> integerValue = getCompileTimeIntegerValue(value);
		return integerValue.has_value() ? CompileTimeValue(static_cast<double>(compileTimeBitwiseNot(*integerValue)))
										: CompileTimeValue{};
	}
	if (kind == IntrinsicKind::Negate) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
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
		CompileTimeValue leftValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		CompileTimeValue rightValue = readArgumentValue(requireArgument(2, expr->intrinsicName));
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
			std::optional<std::int64_t> leftInteger = getCompileTimeIntegerValue(leftValue);
			std::optional<std::int64_t> rightInteger = getCompileTimeIntegerValue(rightValue);
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

static CompileTimeValue
inferIntrinsicCompileTimeValue(Expression *expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	(void)bindingFrameStack;
	if (!expr)
		return {};
	if (intrinsicKind(expr->intrinsicName) == IntrinsicKind::Return && expr->arguments.size() > 1)
		return context.lookupExpressionValue(expr->arguments[1]);
	return evaluatePureIntrinsicCompileTimeValue(expr, context.parseContext, [&](Expression *argumentExpression) {
		return context.lookupExpressionValue(argumentExpression);
	}, [&](Expression *expression) {
		return context.lookupExpressionValue(expression);
	});
}

static Variable *findExecutionSectionVariable(Section *section, const std::string &name) {
	if (!section)
		return nullptr;
	auto it = section->variables.find(name);
	return it != section->variables.end() ? it->second : nullptr;
}

struct PureExecutionState {
	ParseContext &parseContext;
	InferenceContext *inferenceContext{};
	std::vector<std::pair<Section *, std::vector<CompileTimeValue>>> activeCalls;
};

static Expression *pureExecutionFlexExpansion(Expression *expr, const PureExecutionState &state) {
	(void)state;
	if (!expr)
		return nullptr;
	return expr->inferredFlexExpansion;
}

struct PureExpressionExecutionResult {
	CompileTimeValue value;
	bool returned = false;
};

struct PureExecutionFrame {
	Instantiation *instantiation{};
	std::unordered_map<VariableReference *, CompileTimeValue> localValues;
};

static Expression *resolvePureExecutionBinding(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack *outBindingFrameStack = nullptr
) {
	if (outBindingFrameStack)
		*outBindingFrameStack = bindingFrameStack;
	if (expr && expr->kind == Expression::Kind::Pending && expr->patternReference) {
		auto &elements = expr->patternReference->patternElements;
		if (elements.empty())
			elements = getPatternElements(expr->patternReference->pattern.text);
		if (elements.size() == 1 &&
			(elements[0].type == PatternElement::Type::Variable || elements[0].type == PatternElement::Type::VariableLike)) {
			if (Expression *boundExpression = bindingFrameStack.lookup(elements[0].text))
				return boundExpression;
		}
	}
	return resolveVariableBindingAcrossFrames(expr, bindingFrameStack);
}

static PatternDefinition *selectedDefinitionForPureExecution(Expression *expr, const Instantiation *) {
	if (!expr || expr->kind != Expression::Kind::PatternCall)
		return nullptr;
	return expr->selectedPatternDefinition;
}

static void setPureExecutionLocalValue(PureExecutionFrame &frame, VariableReference *reference, const CompileTimeValue &value) {
	VariableReference *normalized = normalizeBindingReference(reference);
	if (!normalized)
		return;
	frame.localValues[normalized] = value;
}

static bool
lookupPureExecutionLocalValue(const PureExecutionFrame &frame, VariableReference *reference, CompileTimeValue &outValue) {
	VariableReference *normalized = normalizeBindingReference(reference);
	if (!normalized)
		return false;
	auto it = frame.localValues.find(normalized);
	if (it == frame.localValues.end())
		return false;
	outValue = it->second;
	return true;
}

template <typename ReadArgumentValueFn>
static bool collectKnownCallArgumentValues(
	Expression *expr, PatternDefinition *definition, ReadArgumentValueFn &&readArgumentValue,
	std::vector<std::pair<std::string, Expression *>> &outBindings,
	std::vector<std::pair<std::string, CompileTimeValue>> &outValues
) {
	outBindings.clear();
	outValues.clear();
	collectPatternCallBindingPairs(expr, definition, outBindings);
	outValues.reserve(outBindings.size());
	for (const auto &[parameterName, argumentExpression] : outBindings) {
		CompileTimeValue argumentValue = readArgumentValue(argumentExpression);
		if (!isCompileTimeKnown(argumentValue))
			return false;
		outValues.push_back({parameterName, argumentValue});
	}
	return true;
}

static std::vector<CompileTimeValue>
compileTimeArgumentValueVector(const std::vector<std::pair<std::string, CompileTimeValue>> &argumentValues) {
	std::vector<CompileTimeValue> values;
	values.reserve(argumentValues.size());
	for (const auto &[name, value] : argumentValues) {
		(void)name;
		values.push_back(value);
	}
	return values;
}

static PureExpressionExecutionResult evaluatePureExpression(
	Expression *expr, PureExecutionState &state, PureExecutionFrame &frame, const BindingFrameStack &bindingFrameStack
);

static PureExpressionExecutionResult executePureSection(
	PureExecutionState &state, PureExecutionFrame &frame, Section *section, InstantiatedSectionBody *body,
	Expression *openingExpression, const BindingFrameStack &bindingFrameStack
);

static CompileTimeValue pureExecutionImmediateValue(Expression *expr) {
	if (!expr)
		return {};
	switch (expr->kind) {
	case Expression::Kind::Literal:
		if (const auto *number = std::get_if<double>(&expr->literalValue))
			return *number;
		if (const auto *text = std::get_if<std::string>(&expr->literalValue))
			return *text;
		return {};
	case Expression::Kind::Variable:
		if (expr->variable) {
			if (std::optional<double> numericLiteral = parseCompileTimeNumericToken(expr->variable->name))
				return *numericLiteral;
		}
		return {};
	case Expression::Kind::Pending:
		if (expr->patternReference) {
			auto &elements = expr->patternReference->patternElements;
			if (elements.empty())
				elements = getPatternElements(expr->patternReference->pattern.text);
			if (elements.size() == 1 && (elements[0].type == PatternElement::Type::Variable ||
										 elements[0].type == PatternElement::Type::VariableLike)) {
				if (std::optional<double> numericLiteral = parseCompileTimeNumericToken(elements[0].text))
					return *numericLiteral;
			}
		}
		return {};
	default:
		return {};
	}
}

static CompileTimeValue pureExecutionStoredValue(Expression *expr, const PureExecutionState &state) {
	return state.inferenceContext ? state.inferenceContext->lookupExpressionValue(expr) : getExpressionCompileTimeValue(expr);
}

static CompileTimeValue executePureInstantiationReturnValue(
	PureExecutionState &state, Section *section, Instantiation &instantiation,
	const std::vector<std::pair<std::string, CompileTimeValue>> &argumentValues
) {
	if (!section || instantiation.purity != InstantiationPurity::Pure || instantiation.inferring ||
		instantiation.needsReinfer || !instantiation.valid || !instantiation.returnType.isDeduced())
		return {};
	std::vector<CompileTimeValue> argumentValueKey = compileTimeArgumentValueVector(argumentValues);
	auto cachedIt = instantiation.pureReturnValuesByArguments.find(argumentValueKey);
	if (cachedIt != instantiation.pureReturnValuesByArguments.end())
		return cachedIt->second;
	for (const auto &[activeSection, activeArguments] : state.activeCalls) {
		if (activeSection == section && activeArguments == argumentValueKey)
			return {};
	}
	state.activeCalls.push_back({section, argumentValueKey});
	PureExecutionFrame frame;
	frame.instantiation = &instantiation;
	requireCompilerInvariant(
		static_cast<bool>(instantiation.body), "pure execution encountered an instantiation without an inferred body"
	);
	requireCompilerInvariant(
		instantiation.body->sourceSection == section, "pure execution encountered an instantiation body for another section"
	);
	for (const auto &[parameterName, value] : argumentValues) {
		Variable *parameterVariable = findExecutionSectionVariable(section, parameterName);
		if (!parameterVariable || !parameterVariable->definition)
			continue;
		setPureExecutionLocalValue(frame, parameterVariable->definition, value);
	}
	PureExpressionExecutionResult executionResult{};
	if (!section->forEachDefinitionBodySection([&](Section *bodySection) {
		InstantiatedSectionBody *activeBody =
			bodySection == section ? instantiation.body.get() : instantiation.body->bodyForChild(bodySection);
		requireCompilerInvariant(activeBody, "pure execution could not find the inferred definition body");
		executionResult = executePureSection(state, frame, bodySection, activeBody, nullptr, {});
		return !executionResult.returned;
	})) {
		state.activeCalls.pop_back();
		if (!isCompileTimeKnown(executionResult.value))
			return {};
		instantiation.pureReturnValuesByArguments.emplace(std::move(argumentValueKey), executionResult.value);
		return executionResult.value;
	}
	state.activeCalls.pop_back();
	if (!executionResult.returned || !isCompileTimeKnown(executionResult.value))
		return {};
	instantiation.pureReturnValuesByArguments.emplace(std::move(argumentValueKey), executionResult.value);
	return executionResult.value;
}

static std::optional<std::tuple<std::string, Expression *, BindingFrameStack>> pureExecutionControlHeaderInfo(
	Expression *lineExpression, PureExecutionState &state, PureExecutionFrame &frame, const BindingFrameStack &bindingFrameStack
) {
	if (!lineExpression)
		return std::nullopt;
	Expression *header = lineExpression;
	BindingFrameStack headerBindingFrameStack = bindingFrameStack;
	if (header->kind == Expression::Kind::PatternCall) {
		PatternDefinition *selectedDefinition = selectedDefinitionForPureExecution(header, frame.instantiation);
		if (selectedDefinition && selectedDefinition->section && selectedDefinition->section->isFlex) {
			BindingFrame innerBindings;
			Expression *expanded = pureExecutionFlexExpansion(header, state);
			if (expanded) {
				collectPatternCallBindings(header, selectedDefinition, innerBindings);
				header = expanded;
				if (!innerBindings.empty()) {
					materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
					headerBindingFrameStack.pushFrame(std::move(innerBindings));
				}
			}
		}
	}
	if (!header || header->kind != Expression::Kind::IntrinsicCall)
		return std::nullopt;
	if (header->intrinsicName != "if" && header->intrinsicName != "else if" && header->intrinsicName != "else" &&
		header->intrinsicName != "loop while") {
		return std::nullopt;
	}
	return std::make_optional(std::make_tuple(header->intrinsicName, header, std::move(headerBindingFrameStack)));
}

static PureExpressionExecutionResult evaluatePureExpression(
	Expression *expr, PureExecutionState &state, PureExecutionFrame &frame, const BindingFrameStack &bindingFrameStack
) {
	if (!expr)
		return {};
	BindingFrameStack resolvedBindingFrameStack;
	Expression *resolvedExpression = resolvePureExecutionBinding(expr, bindingFrameStack, &resolvedBindingFrameStack);
	if (resolvedExpression && resolvedExpression != expr)
		return evaluatePureExpression(resolvedExpression, state, frame, resolvedBindingFrameStack);

	CompileTimeValue immediateValue = pureExecutionImmediateValue(expr);
	if (isCompileTimeKnown(immediateValue))
		return {immediateValue, false};

	switch (expr->kind) {
	case Expression::Kind::Literal:
	case Expression::Kind::TypedPlaceholder:
	case Expression::Kind::Pending:
	case Expression::Kind::ArrayLiteral:
		return {pureExecutionStoredValue(expr, state), false};

	case Expression::Kind::Variable: {
		CompileTimeValue localValue{};
		if (lookupPureExecutionLocalValue(frame, expr->variable, localValue))
			return {localValue, false};
		return {pureExecutionStoredValue(expr, state), false};
	}

	case Expression::Kind::IntrinsicCall: {
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		if (kind == IntrinsicKind::Return) {
			if (expr->arguments.size() <= 1)
				return {{}, true};
			PureExpressionExecutionResult valueResult =
				evaluatePureExpression(expr->arguments[1], state, frame, bindingFrameStack);
			valueResult.returned = true;
			return valueResult;
		}
		if (kind == IntrinsicKind::Store) {
			if (expr->arguments.size() <= 2)
				crashCompilerBug("store intrinsic missing destination or value during pure execution");
			BindingFrameStack destinationBindingFrameStack;
			Expression *destinationExpression =
				resolvePureExecutionBinding(expr->arguments[1], bindingFrameStack, &destinationBindingFrameStack);
			PureExpressionExecutionResult valueResult =
				evaluatePureExpression(expr->arguments[2], state, frame, bindingFrameStack);
			if (valueResult.returned)
				crashCompilerBug("store value evaluation returned unexpectedly during pure execution");
			if (!destinationExpression || destinationExpression->kind != Expression::Kind::Variable ||
				!destinationExpression->variable) {
				return {};
			}
			setPureExecutionLocalValue(frame, destinationExpression->variable, valueResult.value);
			return {};
		}

		std::unordered_map<Expression *, CompileTimeValue> argumentValues;
		for (size_t i = 1; i < expr->arguments.size(); i++) {
			PureExpressionExecutionResult argumentResult =
				evaluatePureExpression(expr->arguments[i], state, frame, bindingFrameStack);
			if (argumentResult.returned)
				crashCompilerBug("intrinsic argument evaluation returned unexpectedly during pure execution");
			argumentValues[expr->arguments[i]] = argumentResult.value;
		}
		return {
			evaluatePureIntrinsicCompileTimeValue(
				expr, state.parseContext,
				[&](Expression *argumentExpression) {
			auto it = argumentValues.find(argumentExpression);
			return it != argumentValues.end() ? it->second : CompileTimeValue{};
		},
				[&](Expression *expression) {
			return pureExecutionStoredValue(expression, state);
		}
			),
			false
		};
	}

	case Expression::Kind::PatternCall: {
		PatternDefinition *selectedDefinition = selectedDefinitionForPureExecution(expr, frame.instantiation);
		if (!selectedDefinition || !selectedDefinition->section)
			crashCompilerBug("pure execution encountered a pattern call without a selected definition");
		Section *matchedSection = selectedDefinition->section;
		if (matchedSection->type == SectionType::Class && !matchedSection->isFlex)
			return {pureExecutionStoredValue(expr, state), false};
		if (matchedSection->isFlex) {
			BindingFrame innerBindings;
			Expression *expandedBody = pureExecutionFlexExpansion(expr, state);
			if (!expandedBody)
				crashCompilerBug("pure execution encountered a selected flex call without its inferred expansion");
			collectPatternCallBindings(expr, selectedDefinition, innerBindings);
			BindingFrameStack expandedBindingFrameStack = bindingFrameStack;
			if (!innerBindings.empty()) {
				materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
				pushBindingScope(expandedBindingFrameStack, std::move(innerBindings));
			}
			return evaluatePureExpression(expandedBody, state, frame, expandedBindingFrameStack);
		}
		std::vector<std::pair<std::string, Expression *>> parameterBindings;
		std::vector<std::pair<std::string, CompileTimeValue>> argumentValues;
		if (!collectKnownCallArgumentValues(expr, selectedDefinition, [&](Expression *argumentExpression) {
			return evaluatePureExpression(argumentExpression, state, frame, bindingFrameStack).value;
		}, parameterBindings, argumentValues)) {
			return {};
		}
		Instantiation *selectedInstantiation = expr->selectedInstantiation;
		if (!selectedInstantiation)
			crashCompilerBug("pure execution encountered a non-flex call without its selected instantiation");
		return {executePureInstantiationReturnValue(state, matchedSection, *selectedInstantiation, argumentValues), false};
	}
	}

	crashCompilerBug("unhandled expression kind during pure execution");
}

static PureExpressionExecutionResult executePureSectionBodyOnce(
	PureExecutionState &state, PureExecutionFrame &frame, Section *section, InstantiatedSectionBody *body,
	const BindingFrameStack &bindingFrameStack
) {
	requireCompilerInvariant(body && body->sourceSection == section, "pure execution received the wrong inferred section body");
	auto executeOpenedSection = [&](CodeLine *line, Expression *lineExpression) -> PureExpressionExecutionResult {
		if (!line || !line->sectionOpening || dynamic_cast<DefinitionSection *>(line->sectionOpening))
			return {};
		InstantiatedSectionBody *openedBody = body->bodyForChild(line->sectionOpening);
		requireCompilerInvariant(openedBody, "pure execution could not find an inferred child section body");
		return executePureSection(state, frame, line->sectionOpening, openedBody, lineExpression, bindingFrameStack);
	};

	for (size_t i = 0; i < section->codeLines.size(); i++) {
		CodeLine *line = section->codeLines[i];
		Expression *lineExpression = body->lineExpression(i);
		auto headerInfo = pureExecutionControlHeaderInfo(lineExpression, state, frame, bindingFrameStack);
		if (headerInfo && std::get<0>(*headerInfo) == "if") {
			size_t chainEnd = i;
			while (chainEnd + 1 < section->codeLines.size()) {
				CodeLine *next = section->codeLines[chainEnd + 1];
				if (!next->sectionOpening || dynamic_cast<DefinitionSection *>(next->sectionOpening))
					break;
				auto nextInfo =
					pureExecutionControlHeaderInfo(body->lineExpression(chainEnd + 1), state, frame, bindingFrameStack);
				if (!nextInfo)
					break;
				const std::string &nextKind = std::get<0>(*nextInfo);
				if (nextKind != "else if" && nextKind != "else")
					break;
				chainEnd++;
			}

			std::optional<size_t> selectedBranch;
			for (size_t k = i; k <= chainEnd; k++) {
				auto branchInfo = pureExecutionControlHeaderInfo(body->lineExpression(k), state, frame, bindingFrameStack);
				if (!branchInfo)
					return {};
				const std::string &branchKind = std::get<0>(*branchInfo);
				if (branchKind == "else") {
					selectedBranch = k;
					break;
				}
				Expression *headerExpression = std::get<1>(*branchInfo);
				BindingFrameStack headerBindingFrameStack = std::get<2>(*branchInfo);
				if (headerExpression->arguments.size() < 2)
					return {};
				PureExpressionExecutionResult conditionResult =
					evaluatePureExpression(headerExpression->arguments[1], state, frame, headerBindingFrameStack);
				if (conditionResult.returned)
					crashCompilerBug("if condition evaluation returned unexpectedly during pure execution");
				auto *condition = std::get_if<bool>(&conditionResult.value);
				if (!condition)
					return {};
				if (*condition) {
					selectedBranch = k;
					break;
				}
			}
			if (selectedBranch.has_value()) {
				PureExpressionExecutionResult branchResult =
					executeOpenedSection(section->codeLines[*selectedBranch], body->lineExpression(*selectedBranch));
				if (branchResult.returned)
					return branchResult;
			}
			i = chainEnd;
			continue;
		}

		if (lineExpression) {
			PureExpressionExecutionResult lineResult = evaluatePureExpression(lineExpression, state, frame, bindingFrameStack);
			if (lineResult.returned)
				return lineResult;
			if ((section->type == SectionType::Get || section->type == SectionType::Replacement) &&
				isCompileTimeKnown(lineResult.value)) {
				lineResult.returned = true;
				return lineResult;
			}
		}

		PureExpressionExecutionResult openedSectionResult = executeOpenedSection(line, lineExpression);
		if (openedSectionResult.returned)
			return openedSectionResult;
	}
	return {};
}

static PureExpressionExecutionResult executePureSection(
	PureExecutionState &state, PureExecutionFrame &frame, Section *section, InstantiatedSectionBody *body,
	Expression *openingExpression, const BindingFrameStack &bindingFrameStack
) {
	if (!section)
		return {};
	requireCompilerInvariant(body && body->sourceSection == section, "pure execution received the wrong inferred section body");
	auto openingInfo = pureExecutionControlHeaderInfo(openingExpression, state, frame, bindingFrameStack);
	if (!openingInfo || std::get<0>(*openingInfo) != "loop while")
		return executePureSectionBodyOnce(state, frame, section, body, bindingFrameStack);
	constexpr size_t maxPureLoopIterations = 100000;
	for (size_t iteration = 0; iteration < maxPureLoopIterations; iteration++) {
		auto headerInfo = pureExecutionControlHeaderInfo(openingExpression, state, frame, bindingFrameStack);
		if (!headerInfo || std::get<0>(*headerInfo) != "loop while")
			return {};
		Expression *headerExpression = std::get<1>(*headerInfo);
		BindingFrameStack headerBindingFrameStack = std::get<2>(*headerInfo);
		if (headerExpression->arguments.size() < 2)
			return {};
		PureExpressionExecutionResult conditionResult =
			evaluatePureExpression(headerExpression->arguments[1], state, frame, headerBindingFrameStack);
		if (conditionResult.returned)
			crashCompilerBug("loop condition evaluation returned unexpectedly during pure execution");
		auto *condition = std::get_if<bool>(&conditionResult.value);
		if (!condition)
			return {};
		if (!*condition)
			return {};
		PureExpressionExecutionResult bodyResult = executePureSectionBodyOnce(state, frame, section, body, bindingFrameStack);
		if (bodyResult.returned)
			return bodyResult;
	}
	return {};
}

static CompileTimeValue evaluatePureFunctionCallReturnValue(
	Expression *expr, PatternDefinition *definition, Section *section, Instantiation &instantiation, InferenceContext &context
) {
	if (!expr || !definition || !section || instantiation.purity != InstantiationPurity::Pure || instantiation.inferring ||
		!instantiation.valid || instantiation.needsReinfer || !instantiation.returnType.isDeduced())
		return {};
	std::vector<std::pair<std::string, Expression *>> parameterBindings;
	std::vector<std::pair<std::string, CompileTimeValue>> argumentValues;
	if (!collectKnownCallArgumentValues(expr, definition, [&](Expression *argumentExpression) {
		return context.lookupExpressionValue(argumentExpression);
	}, parameterBindings, argumentValues)) {
		return {};
	}
	PureExecutionState executionState{context.parseContext, &context, {}};
	return executePureInstantiationReturnValue(executionState, section, instantiation, argumentValues);
}

static Instantiation *ensureCallableFunctionInstantiationInferred(
	PatternDefinition *definition, InferenceContext &context, const Range &referenceRange
) {
	if (!definition || !definition->section || definition->section->type != SectionType::Function ||
		definition->section->isFlex) {
		context.setTypeFailure("function reference requires a non-flex function");
		return nullptr;
	}
	if (context.parseContext.options.emitSPIRV) {
		context.setTypeFailure("function references are unavailable for SPIR-V targets");
		return nullptr;
	}
	std::vector<std::pair<std::string, DataType>> parameters;
	collectCallableFunctionParameters(definition, parameters);
	std::vector<std::unique_ptr<Expression>> ownedArguments;
	std::vector<std::pair<std::string, Expression *>> parameterBindings;
	std::vector<DataType> argumentTypes;
	ownedArguments.reserve(parameters.size());
	parameterBindings.reserve(parameters.size());
	argumentTypes.reserve(parameters.size());
	for (const auto &[parameterName, parameterType] : parameters) {
		if (!parameterType.isDeduced()) {
			context.setTypeFailure("function reference requires a concrete type for parameter '" + parameterName + "'");
			return nullptr;
		}
		if (parameterType.isMetaType() || parameterType.kind == DataType::Kind::Void) {
			context.setTypeFailure("function reference parameter '" + parameterName + "' is not a runtime value");
			return nullptr;
		}
		DefinitionPatternElement *parameterElement = findParameterElement(definition->patternElements, parameterName);
		requireCompilerInvariant(parameterElement != nullptr, "callable parameter has no definition element");
		if (patternParameterRequiresCompileTimeValue(*parameterElement, parameterType)) {
			context.setTypeFailure("function reference cannot bind fixed parameter '" + parameterName + "'");
			return nullptr;
		}
		auto argument = std::make_unique<Expression>();
		argument->kind = Expression::Kind::TypedPlaceholder;
		argument->type = parameterType;
		argument->range = referenceRange;
		parameterBindings.push_back({parameterName, argument.get()});
		argumentTypes.push_back(parameterType);
		ownedArguments.push_back(std::move(argument));
	}
	if (!ensureSectionInstantiationInferred(
			context.parseContext, definition->section, definition, parameterBindings, argumentTypes, {},
			context.currentInstantiation, &context
		)) {
		return nullptr;
	}
	InstantiationKey key{.argumentTypes = argumentTypes, .compileTimeParameters = {}};
	auto instantiation = definition->section->instantiations.find(key);
	requireCompilerInvariant(
		instantiation != definition->section->instantiations.end(),
		"callable inference did not retain its selected instantiation"
	);
	if (!context.trial)
		definition->callableInstantiation = &instantiation->second;
	return &instantiation->second;
}

static CompileTimeValue
inferVariableCompileTimeValue(Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack) {
	if (!expr || !expr->variable)
		return {};
	CompileTimeValue computedValue{};
	Expression *boundExpression = flexBindingFrameStack.lookup(expr->variable);
	if (boundExpression && boundExpression != expr) {
		computedValue = resolveStoredCompileTimeValue(boundExpression, flexBindingFrameStack, &context);
		if (isCompileTimeKnown(computedValue)) {
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
		computedValue = TypeReferenceValue::exact(expr->type);
	return computedValue;
}

#include "intrinsics/store_inference.inl"
#include "variable_flow.inl"

// Infer types for a section's code lines with operand reordering. Returns false on failure.
static bool inferSection(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack = BindingFrameStack{}
);

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
			if (!context.trial && context.currentInstantiation && var->isGlobal && !expressionIsLValueOnlyUse(context, expr))
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

	case Expression::Kind::IntrinsicCall: {
		const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		if (info) {
			for (size_t argumentIndex = 1; argumentIndex < expr->arguments.size(); argumentIndex++) {
				if (!intrinsicArgumentIsCompileTimeOnly(expr->intrinsicName, static_cast<int>(argumentIndex)))
					continue;
				Expression *argumentExpression = expr->arguments[argumentIndex];
				if (!argumentExpression)
					crashCompilerBug("intrinsic compile-time argument validation encountered null argument expression");
				CompileTimeValue argumentValue =
					resolveStoredCompileTimeValue(argumentExpression, flexBindingFrameStack, &context);
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
					expr->type = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				} else {
					DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
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
					DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					if (!isBitwiseOperandType(valueType)) {
						setConfiguredTypeFailure(
							expr->range, "bitwise operator operand invalid", "message",
							{{"operator", expr->intrinsicName}, {"value_type", typeToUserName(valueType, context.parseContext)}}
						);
						break;
					}
					expr->type = valueType;
				} else {
					DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
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
					DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
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
					DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					if (!isLogicalOperandType(valueType)) {
						setConfiguredTypeFailure(
							expr->range, "logical operator operand invalid", "message",
							{{"operator", "not"}, {"value_type", typeToUserName(valueType, context.parseContext)}}
						);
						break;
					}
				} else {
					DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
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
					DataType retType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					if (retType.isDeduced() && context.currentInstantiation)
						context.currentInstantiation->returnType = retType;
					context.setExpressionValue(expr, context.lookupExpressionValue(expr->arguments[1]));
				}
				if ((kind == IntrinsicKind::If || kind == IntrinsicKind::ElseIf || kind == IntrinsicKind::LoopWhile) &&
					expr->arguments.size() > 1) {
					DataType conditionType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
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
					inferStoreEffects(expr, context, flexBindingFrameStack);
				expr->type = {DataType::Kind::Void};
				break;
			case IntrinsicReturnKind::Float:
				expr->type = {DataType::Kind::Float, 4};
				break;
			case IntrinsicReturnKind::Custom:
				if (kind == IntrinsicKind::CommandLineArgumentCount || kind == IntrinsicKind::CommandLineArgumentValues) {
					if (context.parseContext.options.emitWASM || context.parseContext.options.emitSPIRV) {
						failWithDetail(expr->range, "Command-line arguments are unavailable for this target", 0);
						break;
					}
					if (kind == IntrinsicKind::CommandLineArgumentCount) {
						expr->type = {DataType::Kind::Int, 4};
					} else {
						expr->type = {DataType::Kind::Int, 1};
						expr->type.pointerDepth = 2;
					}
				} else if (kind == IntrinsicKind::AddressOf) {
					DataType varType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					if (varType.isDeduced())
						expr->type = varType.pointed();
				} else if (kind == IntrinsicKind::Dereference) {
					DataType ptrType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					if (ptrType.isDeduced() && ptrType.isPointer())
						expr->type = concretizeClassType(ptrType.dereferenced());
				} else if (kind == IntrinsicKind::LoadAt) {
					DataType ptrType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
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
					DataType retTypeRef = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
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
						DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
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
					Expression *functionExpr = resolveThroughFlexBindings(expr->arguments[1]);
					requireCompilerInvariant(
						functionExpr != nullptr, "function intrinsic lost its argument expression during type inference"
					);
					if (!std::holds_alternative<std::string>(functionExpr->literalValue)) {
						setConfiguredTypeFailure(functionExpr->range, "function intrinsic requires string literal");
						if (!context.trial)
							context.addDiagnosticWithCurrentTrace(Diagnostic(
								context.parseContext, Diagnostic::Level::Error, "function intrinsic requires string literal",
								functionExpr->range
							));
						break;
					}
					std::string signature = std::get<std::string>(functionExpr->literalValue);
					std::vector<PatternDefinition *> callableMatches =
						findCallableFunctionDefinitionsBySignature(context.parseContext, signature);
					if (callableMatches.empty()) {
						setConfiguredTypeFailure(
							functionExpr->range, "unknown function reference", "message", {{"signature", signature}}
						);
						if (!context.trial)
							context.addDiagnosticWithCurrentTrace(Diagnostic(
								context.parseContext, Diagnostic::Level::Error, "unknown function reference",
								functionExpr->range, "signature", signature
							));
						break;
					}
					if (callableMatches.size() > 1) {
						setConfiguredTypeFailure(
							functionExpr->range, "ambiguous function reference", "message", {{"signature", signature}}
						);
						if (!context.trial)
							context.addDiagnosticWithCurrentTrace(Diagnostic(
								context.parseContext, Diagnostic::Level::Error, "ambiguous function reference",
								functionExpr->range, "signature", signature
							));
						break;
					}
					Instantiation *callableInstantiation =
						ensureCallableFunctionInstantiationInferred(callableMatches.front(), context, functionExpr->range);
					if (!callableInstantiation)
						break;
					expr->selectedCallableDefinition = callableMatches.front();
					expr->selectedInstantiation = callableInstantiation;
					expr->type = {DataType::Kind::Int, 1};
					expr->type.pointerDepth = 1;
				} else if (kind == IntrinsicKind::Cast) {
					DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					DataType typeArgType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
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
					std::string kindStr;
					CompileTimeValue kindValue =
						resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
					if (auto *str = std::get_if<std::string>(&kindValue))
						kindStr = *str;
					if (kindStr.empty()) {
						failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type kind string");
						break;
					}
					std::optional<DataType> typeRefType =
						makeBuiltinTypeReference(kindStr, context.parseContext.options.emitSPIRV);
					if (!typeRefType) {
						failWithDetail(
							expr->arguments[1]->range,
							"Unknown compile-time type kind '" + kindStr + "' in intrinsic '" + expr->intrinsicName + "'", 0
						);
						break;
					}
					std::optional<int> numericByteSize;
					if (expr->arguments.size() > 2) {
						int bitCount = 0;
						if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, bitCount, &context) ||
							bitCount <= 0 || bitCount % 8 != 0) {
							failIntrinsicArgumentRequirement(2, "a positive integer divisible by 8");
							break;
						}
						numericByteSize = bitCount / 8;
						typeRefType =
							makeBuiltinTypeReference(kindStr, context.parseContext.options.emitSPIRV, numericByteSize);
						if (!typeRefType) {
							failIntrinsicArgumentRequirement(2, "a numeric type kind that accepts a bit width");
							break;
						}
					}
					TypeReferenceValue typeRefValue =
						TypeReferenceValue::builtin(kindStr, context.parseContext.options.emitSPIRV, numericByteSize);
					expr->type = typeRefValue.type;
					context.setExpressionValue(expr, typeRefValue);
				} else if (kind == IntrinsicKind::Fix) {
					CompileTimeValue sourceValue =
						resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
					std::optional<TypeConstraint> constraint = getCompileTimeConstraintValue(sourceValue);
					if (!constraint) {
						failCompileTimeOnlyIntrinsicArgument(1, "a type or constraint value");
						break;
					}
					constraint->requiresCompileTimeValue = true;
					expr->type = {DataType::Kind::Constraint};
					context.setExpressionValue(expr, *constraint);
				} else if (kind == IntrinsicKind::TypeOf) {
					DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
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
						context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
					}
				} else if (kind == IntrinsicKind::Select) {
					DataType conditionType = expr->arguments[1]->type;
					if (!conditionType.isDeduced())
						conditionType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					if (!isLogicalOperandType(conditionType)) {
						setConfiguredTypeFailure(
							expr->range, "logical operator operand invalid", "message",
							{{"operator", "select condition"},
							 {"value_type", typeToUserName(conditionType, context.parseContext)}}
						);
						break;
					}
					CompileTimeValue conditionValue =
						resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
					if (auto *condition = std::get_if<bool>(&conditionValue)) {
						size_t selectedArgumentIndex = *condition ? 2 : 3;
						DataType selectedType = expr->arguments[selectedArgumentIndex]->type;
						if (!selectedType.isDeduced())
							selectedType =
								ensureExpressionType(expr->arguments[selectedArgumentIndex], context, flexBindingFrameStack);
						if (selectedType.isDeduced())
							expr->type = selectedType;
						context.setExpressionValue(expr, context.lookupExpressionValue(expr->arguments[selectedArgumentIndex]));
						break;
					}
					DataType trueType = expr->arguments[2]->type;
					if (!trueType.isDeduced())
						trueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
					DataType falseType = expr->arguments[3]->type;
					if (!falseType.isDeduced())
						falseType = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
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
					Expression *typeExpression = expr->arguments[1];
					if (!inferExpression(typeExpression, context, false, flexBindingFrameStack))
						break;
					expr->arguments[1] = typeExpression;
					DataType typeArgType;
					if (!resolveCompileTimeTypeReference(
							context.parseContext, expr->arguments[1], flexBindingFrameStack, typeArgType, &context
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
					Expression *keyExpr = expr->arguments[1];
					if (!inferExpression(keyExpr, context, false, flexBindingFrameStack))
						break;
					expr->arguments[1] = keyExpr;
					CompileTimeValue keyValue = context.lookupExpressionValue(keyExpr);
					auto *key = std::get_if<std::string>(&keyValue);
					if (!key) {
						setConfiguredTypeFailure(expr->range, "build info key must be string literal");
						break;
					}
					std::optional<DataType> infoType = buildInfoValueType(*key);
					if (!infoType) {
						setConfiguredTypeFailure(
							expr->range, "unknown build info key", "message", {{"key", std::string(*key)}}
						);
						break;
					}
					expr->type = *infoType;
				} else if (kind == IntrinsicKind::TargetIs) {
					Expression *targetExpr = expr->arguments[1];
					if (!inferExpression(targetExpr, context, false, flexBindingFrameStack))
						break;
					expr->arguments[1] = targetExpr;
					CompileTimeValue targetValue = context.lookupExpressionValue(targetExpr);
					auto *targetName = std::get_if<std::string>(&targetValue);
					if (!targetName) {
						setConfiguredTypeFailure(expr->range, "build target must be string literal");
						break;
					}
					if (!evaluateTargetIs(context.parseContext, *targetName).has_value()) {
						setConfiguredTypeFailure(
							expr->range, "unknown build target", "message", {{"target", std::string(*targetName)}}
						);
						break;
					}
					expr->type = {DataType::Kind::Bool};
				} else if (kind == IntrinsicKind::ShaderStageIs) {
					Expression *shaderStageExpr = expr->arguments[1];
					if (!inferExpression(shaderStageExpr, context, false, flexBindingFrameStack))
						break;
					expr->arguments[1] = shaderStageExpr;
					CompileTimeValue shaderStageValue = context.lookupExpressionValue(shaderStageExpr);
					auto *shaderStageName = std::get_if<std::string>(&shaderStageValue);
					if (!shaderStageName) {
						setConfiguredTypeFailure(expr->range, "shader stage must be string literal");
						break;
					}
					if (!evaluateShaderStageIs(context.parseContext, *shaderStageName).has_value()) {
						setConfiguredTypeFailure(
							expr->range, "unknown shader stage", "message", {{"stage", std::string(*shaderStageName)}}
						);
						break;
					}
					expr->type = {DataType::Kind::Bool};
				} else if (kind == IntrinsicKind::Array) {
					int arraySize = 0;
					if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, arraySize, &context) ||
						arraySize < 0) {
						failIntrinsicArgumentRequirement(1, "a non-negative integer");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Array;
					expr->type.arraySize = arraySize;
					if (expr->arguments.size() > 2) {
						DataType elemTypeRef = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
						if (elemTypeRef.kind == DataType::Kind::Constraint) {
							std::optional<TypeConstraint> elementConstraint = getCompileTimeConstraintValue(
								resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
							);
							if (!elementConstraint) {
								failCompileTimeOnlyIntrinsicArgument(2, "a compile-time element constraint");
								break;
							}
							TypeConstraint arrayConstraint = TypeConstraint::any();
							arrayConstraint.kind = DataType::Kind::Array;
							arrayConstraint.pointerDepth = 0;
							arrayConstraint.arraySize = arraySize;
							arrayConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
							expr->type = {DataType::Kind::Constraint};
							context.setExpressionValue(expr, arrayConstraint);
							break;
						}
						if (elemTypeRef.kind != DataType::Kind::Type) {
							failCompileTimeOnlyIntrinsicArgument(2, "a compile-time element type reference");
							break;
						}
						expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
					TypeConstraint arrayConstraint = TypeConstraint::any();
					arrayConstraint.kind = DataType::Kind::Array;
					arrayConstraint.pointerDepth = 0;
					arrayConstraint.arraySize = arraySize;
					if (expr->arguments.size() > 2) {
						std::optional<TypeReferenceValue> elementValue = getCompileTimeTypeReferenceValue(
							resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
						);
						if (!elementValue)
							crashCompilerBug("inferred array element type is missing its type-reference value");
						arrayConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
					}
					context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(arrayConstraint)});
				} else if (kind == IntrinsicKind::Vector) {
					int vectorSize = 0;
					if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, vectorSize, &context) ||
						vectorSize < 1) {
						failIntrinsicArgumentRequirement(1, "an integer greater than 0");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Vector;
					expr->type.arraySize = vectorSize;
					expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
					if (expr->arguments.size() > 2) {
						DataType elemTypeRef = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
						if (elemTypeRef.kind == DataType::Kind::Constraint) {
							std::optional<TypeConstraint> elementConstraint = getCompileTimeConstraintValue(
								resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
							);
							if (!elementConstraint) {
								failCompileTimeOnlyIntrinsicArgument(2, "a compile-time vector element constraint");
								break;
							}
							TypeConstraint vectorConstraint = TypeConstraint::any();
							vectorConstraint.kind = DataType::Kind::Vector;
							vectorConstraint.pointerDepth = 0;
							vectorConstraint.arraySize = vectorSize;
							vectorConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
							expr->type = {DataType::Kind::Constraint};
							context.setExpressionValue(expr, vectorConstraint);
							break;
						}
						if (elemTypeRef.kind != DataType::Kind::Type) {
							failCompileTimeOnlyIntrinsicArgument(2, "a compile-time vector element type reference");
							break;
						}
						expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
					if (!context.typesValid)
						break;
					TypeConstraint vectorConstraint = TypeConstraint::fromTypeReference(expr->type);
					if (expr->arguments.size() > 2) {
						std::optional<TypeReferenceValue> elementValue = getCompileTimeTypeReferenceValue(
							resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
						);
						if (!elementValue)
							crashCompilerBug("inferred vector element type is missing its type-reference value");
						vectorConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
					}
					context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(vectorConstraint)});
				} else if (kind == IntrinsicKind::Matrix) {
					int rows = 0;
					int columns = 0;
					if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, rows, &context) ||
						rows < 1) {
						failIntrinsicArgumentRequirement(1, "an integer greater than 0");
						break;
					}
					if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, columns, &context) ||
						columns < 1) {
						failIntrinsicArgumentRequirement(2, "an integer greater than 0");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Matrix;
					expr->type.matrixRowCount = rows;
					expr->type.arraySize = columns;
					expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
					if (expr->arguments.size() > 3) {
						DataType elemTypeRef = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
						if (elemTypeRef.kind == DataType::Kind::Constraint) {
							std::optional<TypeConstraint> elementConstraint = getCompileTimeConstraintValue(
								resolveStoredCompileTimeValue(expr->arguments[3], flexBindingFrameStack, &context)
							);
							if (!elementConstraint) {
								failCompileTimeOnlyIntrinsicArgument(3, "a compile-time matrix element constraint");
								break;
							}
							TypeConstraint matrixConstraint = TypeConstraint::any();
							matrixConstraint.kind = DataType::Kind::Matrix;
							matrixConstraint.pointerDepth = 0;
							matrixConstraint.matrixRows = rows;
							matrixConstraint.matrixColumns = columns;
							matrixConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
							expr->type = {DataType::Kind::Constraint};
							context.setExpressionValue(expr, matrixConstraint);
							break;
						}
						if (elemTypeRef.kind != DataType::Kind::Type) {
							failCompileTimeOnlyIntrinsicArgument(3, "a compile-time matrix element type reference");
							break;
						}
						expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
					if (!context.typesValid)
						break;
					TypeConstraint matrixConstraint = TypeConstraint::fromTypeReference(expr->type);
					if (expr->arguments.size() > 3) {
						std::optional<TypeReferenceValue> elementValue = getCompileTimeTypeReferenceValue(
							resolveStoredCompileTimeValue(expr->arguments[3], flexBindingFrameStack, &context)
						);
						if (!elementValue)
							crashCompilerBug("inferred matrix element type is missing its type-reference value");
						matrixConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
					}
					context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(matrixConstraint)});
				} else if (kind == IntrinsicKind::AddPointerDepth) {
					DataType typeArgType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
					if (typeArgType.kind == DataType::Kind::Constraint) {
						CompileTimeValue sourceValue =
							resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
						std::optional<TypeConstraint> constraint = getCompileTimeConstraintValue(sourceValue);
						if (!constraint || (constraint->kind && (*constraint->kind == DataType::Kind::Type ||
																 *constraint->kind == DataType::Kind::Constraint))) {
							setConfiguredTypeFailure(expr->range, "pointer to type invalid");
							break;
						}
						constraint->pointerDepth = constraint->pointerDepth.value_or(0) + 1;
						expr->type = {DataType::Kind::Constraint};
						context.setExpressionValue(expr, *constraint);
						break;
					}
					if (typeArgType.kind != DataType::Kind::Type) {
						failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type or constraint reference");
						break;
					}
					if (typeArgType.referencedKind == DataType::Kind::Type ||
						typeArgType.referencedKind == DataType::Kind::Unresolved) {
						setConfiguredTypeFailure(expr->range, "pointer to type invalid");
						break;
					}
					expr->type = typeArgType;
					expr->type.pointerDepth++;
					std::optional<TypeReferenceValue> sourceTypeValue = getCompileTimeTypeReferenceValue(
						resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context)
					);
					if (!sourceTypeValue)
						crashCompilerBug("pointer type shaping is missing its source type-reference value");
					sourceTypeValue->type = expr->type;
					sourceTypeValue->constraint.pointerDepth = sourceTypeValue->constraint.pointerDepth.value_or(0) + 1;
					context.setExpressionValue(expr, *sourceTypeValue);
				} else if (kind == IntrinsicKind::Construct) {
					std::vector<DataType> constructionArgumentTypes;
					constructionArgumentTypes.reserve(expr->arguments.size() - 2);
					bool allConstructionArgumentsDeduced = true;
					for (size_t i = 2; i < expr->arguments.size(); i++) {
						DataType argumentType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
						if (!argumentType.isDeduced()) {
							allConstructionArgumentsDeduced = false;
							break;
						}
						constructionArgumentTypes.push_back(argumentType);
					}
					DataType typeRefType;
					if (!resolveCompileTimeTypeReference(
							context.parseContext, expr->arguments[1], flexBindingFrameStack, typeRefType, &context,
							allConstructionArgumentsDeduced ? &constructionArgumentTypes : nullptr
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
								DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
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
								DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
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
							DataType valueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
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
						DataType instantiatedTypeRef;
						if (allConstructionArgumentsDeduced && instantiateClassFromArgumentTypes(
																   typeRefType.classDefinition, constructionArgumentTypes,
																   instantiatedTypeRef, typeRefType.classInstIndex
															   )) {
							expr->type = instantiatedTypeRef.toReferencedType();
						} else {
							DataType targetType = concretizeClassType(typeRefType.toReferencedType());
							if (expr->arguments.size() == targetType.classDefinition->fields.size() + 2 &&
								targetType.classInstIndex >= 0) {
								const auto &fieldTypes =
									targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
								bool allCompatible = constructionArgumentTypes.size() == fieldTypes.size();
								for (size_t i = 0; allCompatible && i < fieldTypes.size(); i++) {
									if (!DataType::supportsRuntimeConversion(
											concretizeClassType(constructionArgumentTypes[i]), fieldTypes[i]
										))
										allCompatible = false;
								}
								if (allCompatible)
									expr->type = targetType;
							}
						}
					} else if (expr->arguments.size() == 3) {
						DataType targetType = typeRefType.toReferencedType();
						DataType valueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
						if (valueType.isDeduced())
							expr->type = targetType;
					}
				} else if (kind == IntrinsicKind::Property) {
					DataType instType =
						concretizeClassType(ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack));
					if (!instType.isDeduced()) {
						context.typesValid = false;
						break;
					}
					if (instType.isPointer() && instType.kind == DataType::Kind::Class)
						instType = concretizeClassType(instType.dereferenced());
					std::string fieldName;
					CompileTimeValue propertyValue =
						resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context);
					if (const auto *propertyName = std::get_if<std::string>(&propertyValue)) {
						fieldName = *propertyName;
					} else {
						Expression *propExpr = resolveThroughFlexBindings(expr->arguments[2]);
						fieldName = extractFieldName(propExpr);
					}
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
		if (context.typesValid)
			markIntrinsicImpurityIfNeeded(expr, context, flexBindingFrameStack);
		context.setExpressionValue(expr, inferIntrinsicCompileTimeValue(expr, context, flexBindingFrameStack));
		break;
	}

	case Expression::Kind::PatternCall: {
		expr->selectedPatternDefinition = nullptr;
		expr->selectedInstantiation = nullptr;
		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
		if (definitionsHaveUnresolvedTypeConstraints(defs)) {
			if (!context.unresolvedPatternConstraintSignal)
				crashCompilerBug("normal type inference encountered an unresolved pattern type constraint");
			*context.unresolvedPatternConstraintSignal = true;
			context.typesValid = false;
			break;
		}

		// Build argument types for overload selection.
		// Arguments are sorted by source position and include both Variable and Word captures.
		std::vector<DataType> argTypesForOverload;
		std::vector<bool> argCompileTimeKnown;
		argCompileTimeKnown.reserve(expr->arguments.size());
		for (size_t ai = 0; ai < expr->arguments.size(); ai++) {
			Expression *inferArg = expr->arguments[ai];
			DataType argumentType =
				requestKnownOrInferExpressionType(inferArg, context, flexBindingFrameStack, preserveCurrentGrouping);
			argTypesForOverload.push_back(argumentType);
			expr->arguments[ai] = inferArg;
			argCompileTimeKnown.push_back(
				argumentType.isMetaType() || isCompileTimeKnown(context.lookupExpressionValue(inferArg))
			);
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
		PatternDefinition *def =
			selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload, argCompileTimeKnown);
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
			std::vector<RelatedInfo> implicitPromotionRelatedInfo;
			for (PatternDefinition *candidate : defs) {
				std::vector<std::pair<std::string, Expression *>> candidateBindings;
				collectPatternCallBindingPairs(expr, candidate, candidateBindings);
				for (const auto &[parameterName, argumentExpression] : candidateBindings) {
					if (!argumentExpression || argumentExpression->type.isDeduced() ||
						argumentExpression->kind != Expression::Kind::Variable || !argumentExpression->variable ||
						argumentExpression->variable->name != parameterName) {
						continue;
					}
					DefinitionPatternElement *parameterElement =
						findParameterElement(candidate->patternElements, parameterName);
					if (parameterElement && parameterElement->promotedFromVariableLike)
						appendImplicitPromotionTrace(implicitPromotionRelatedInfo, candidate, parameterName);
				}
			}
			for (Expression *argumentExpression : expr->arguments)
				appendUnboundParameterTrace(context, argumentExpression, implicitPromotionRelatedInfo);
			context.typeFailureRelatedInfo.insert(
				context.typeFailureRelatedInfo.end(), implicitPromotionRelatedInfo.begin(), implicitPromotionRelatedInfo.end()
			);
			context.fail(
				buildFailureDetailDiagnostic(expr->range, detail, implicitPromotionRelatedInfo),
				overloadFailurePriority(expr->range)
			);
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
		expr->selectedPatternDefinition = def;

		Section *matchedSection = def->section;

		if (matchedSection->type == SectionType::Class && !matchedSection->isFlex) {
			BindingFrame callBindings;
			collectPatternCallBindings(expr, def, callBindings);
			for (auto &[parameterName, argumentExpression] : callBindings.bindings) {
				(void)parameterName;
				if (Expression *resolvedArgument = resolveThroughBindings(argumentExpression, flexBindingFrameStack))
					argumentExpression = resolvedArgument;
			}
			for (auto &[parameterDefinition, argumentExpression] : callBindings.parameterBindings) {
				(void)parameterDefinition;
				if (Expression *resolvedArgument = resolveThroughBindings(argumentExpression, flexBindingFrameStack))
					argumentExpression = resolvedArgument;
			}
			BindingFrameStack callBindingFrameStack = flexBindingFrameStack;
			pushBindingScope(callBindingFrameStack, std::move(callBindings));
			auto *classSec = static_cast<ClassSection *>(matchedSection);
			expr->type =
				instantiateBoundClassType(context.parseContext, classSec->classDefinition, callBindingFrameStack, &context);
			if (expr->type.kind == DataType::Kind::Type)
				context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
		} else if (matchedSection->isFlex) {
			BindingFrameStack callBindingFrameStack = flexBindingFrameStack;
			BindingFrame callBindings;
			collectPatternCallBindings(expr, def, callBindings);
			materializeFlexBindingsInCallerScope(callBindings, flexBindingFrameStack);
			pushBindingScope(callBindingFrameStack, std::move(callBindings));
			if (matchedSection->inferring) {
				context.setTypeFailure("recursive flex expansion");
				break;
			}
			std::shared_ptr<InstantiatedSectionBody> flexBody = context.parseContext.cloneSectionBody(matchedSection);
			matchedSection->inferring = true;
			bool bodyInferred = matchedSection->forEachDefinitionBodySection([&](Section *definitionBodySection) {
				InstantiatedSectionBody *activeBody =
					definitionBodySection == matchedSection ? flexBody.get() : flexBody->bodyForChild(definitionBodySection);
				requireCompilerInvariant(activeBody, "flex clone is missing its definition body");
				return inferSection(definitionBodySection, activeBody, nullptr, context, callBindingFrameStack);
			});
			matchedSection->inferring = false;
			if (!bodyInferred || !context.typesValid)
				break;
			Expression *templateBodyExpression = flexPatternBodyExpression(def);
			Expression *bodyExpr = flexBody->findCloneOf(templateBodyExpression);
			if (!bodyExpr) {
				crashCompilerBug("flex clone is missing its replacement expression");
				break;
			}
			bodyExpr->isExplicitGroup = true;
			expr->inferredFlexBody = std::move(flexBody);
			expr->inferredFlexExpansion = bodyExpr;
			DataType resolvedType = bodyExpr->type;
			if (resolvedType.isDeduced())
				expr->type = resolvedType;
			context.setExpressionValue(expr, context.lookupExpressionValue(bodyExpr));
		} else {
			// Non-flex function: infer body per-instantiation
			// Build parameter bindings and argTypes in nodesPassed order (must match codegen's paramBindings order)
			std::vector<std::pair<std::string, Expression *>> paramBindings;
			collectPatternCallBindingPairs(expr, def, paramBindings);
			std::vector<DataType> argTypes;
			for (auto &[parameterName, argumentExpression] : paramBindings) {
				ArgumentTypeInferenceResult argTypeResult =
					ensureArgumentTypeForPatternCall(argumentExpression, context, flexBindingFrameStack);
				DataType argType = argTypeResult.type;
				if (!argType.isDeduced()) {
					if (argTypeResult.deferred && context.currentInstantiation) {
						if (!context.trial)
							context.currentInstantiation->needsReinfer = true;
						return;
					}
					if (context.trial) {
						setConfiguredTypeFailure(expr->range, "undeduced argument type in trial inference");
						DefinitionPatternElement *parameterElement = findParameterElement(def->patternElements, parameterName);
						if (parameterElement && parameterElement->promotedFromVariableLike && argumentExpression &&
							argumentExpression->kind == Expression::Kind::Variable && argumentExpression->variable &&
							argumentExpression->variable->name == parameterName) {
							appendImplicitPromotionTrace(context.typeFailureRelatedInfo, def, parameterName);
						}
						return;
					}
					requireCompilerInvariant(
						argType.isDeduced(), "Undeduced argument type encountered during non-flex pattern-call inference"
					);
				}
				argTypes.push_back(argType);
			}
			std::unordered_set<std::string> explicitCompileTimeParameters =
				collectExplicitCompileTimeParameters(def, paramBindings, expr->patternMatch->nodesPassed, argTypes);
			std::vector<std::pair<std::string, CompileTimeValue>> trialCompileTimeParameters;
			std::string trialCacheKey;
			if (context.trial && context.allowTrialSummaryReuse) {
				trialCompileTimeParameters =
					collectTrialCompileTimeParameters(context, paramBindings, explicitCompileTimeParameters);
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
					context.setExpressionValue(expr, cachedTrial->second.returnValue);
					if (cachedTrial->second.purity == InstantiationPurity::Impure)
						markCurrentInstantiationImpure(context);
					break;
				}
				traceTrialInstantiationCacheEvent("miss", def, trialCacheKey);
			}

			auto evaluateParameterValue = [&](Expression *argumentExpression) {
				(void)flexBindingFrameStack;
				if (!argumentExpression)
					crashCompilerBug("missing non-flex argument while building inference instantiation key");
				return context.lookupExpressionValue(argumentExpression);
			};
			InstantiationKey instantiationKey = getOrCreateNonFlexInstantiationKey(
				matchedSection, paramBindings, argTypes, explicitCompileTimeParameters, evaluateParameterValue
			);
			if (context.trial && context.trialJournal)
				context.trialJournal->recordSectionInstantiationWrite(matchedSection, instantiationKey);
			Instantiation &inst = matchedSection->instantiations[instantiationKey];
			expr->selectedInstantiation = &inst;
			if (inst.argumentTypes.empty()) {
				inst.argumentTypes = argTypes;
			} else {
				requireCompilerInvariant(inst.argumentTypes == argTypes, "Instantiation argumentTypes diverged from map key");
			}
			std::optional<InstantiationKey> refinedInstantiationKey;
			bool hasReusableInstantiation = inst.valid && inst.returnType.isDeduced() && !inst.needsReinfer;
			if (!inst.inferring && !hasReusableInstantiation) {
				ScopedSectionLocalVariableState calleeVariableState(matchedSection);
				if (context.trial)
					inst.body = context.parseContext.cloneSectionBody(matchedSection);
				Instantiation *savedInst = context.currentInstantiation;
				auto callerKnownConstants = context.currentKnownConstants;
				bool inferenceSucceeded = runInstantiationReinferenceLoop(
					context, inst, def, expr->range, (std::string)def->range.subString, savedInst != nullptr,
					[&]() -> bool {
					seedInstantiationParameterTypes(inst, paramBindings, argTypes);
					inst.writtenGlobalReferences.clear();
					inst.finalGlobalConstantValues.clear();
					inst.requiredCompileTimeParameters = explicitCompileTimeParameters;
					inst.purity = InstantiationPurity::Pure;
					inst.pureReturnValuesByArguments.clear();
					seedInstantiationCompileTimeParameters(
						inst, paramBindings, argTypes, inst.requiredCompileTimeParameters,
						[&](Expression *argumentExpression) {
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
						return getExpressionCompileTimeValue(argumentExpression);
					}
					);
					inst.needsReinfer = false;
					inst.inferring = true;
					context.currentInstantiation = &inst;
					// A cached instantiation serves every future call site, so its
					// inference must not consume the calling context's tracked
					// constants: the only entries visible inside the callee are
					// globals, and their values differ per call. Parameter
					// constants arrive through the instantiation key instead.
					context.currentKnownConstants.clear();
					bool savedReinferSuppression = context.suppressReinferPassDiagnostics;
					context.suppressReinferPassDiagnostics = true;
					if (!inst.body)
						inst.body = context.parseContext.cloneSectionBody(matchedSection);
					bool passSucceeded = inferSection(matchedSection, inst.body.get(), nullptr, context, {});
					inst.finalGlobalConstantValues.clear();
					for (VariableReference *reference : inst.writtenGlobalReferences) {
						auto knownIt = context.currentKnownConstants.find(reference);
						if (knownIt != context.currentKnownConstants.end() && isCompileTimeKnown(knownIt->second))
							inst.finalGlobalConstantValues[reference] = knownIt->second;
					}
					context.suppressReinferPassDiagnostics = savedReinferSuppression;
					inst.inferring = false;
					return passSucceeded;
				}
				);
				context.currentKnownConstants = std::move(callerKnownConstants);
				context.currentInstantiation = savedInst;
				inst.valid = inferenceSucceeded;
				if (inst.needsReinfer && savedInst && !context.trial)
					savedInst->needsReinfer = true;
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
			mergeCalleeGlobalWritesIntoCaller(context, inst);
			mergeInstantiationPurityIntoCaller(context, inst);

			// If no return intrinsic was found, default to Void
			if (!inst.inferring && !inst.needsReinfer && !context.observedInProgressUndeducedInstantiation &&
				inst.returnType.kind == DataType::Kind::Any) {
				inst.returnType = {DataType::Kind::Void};
			}
			CompileTimeValue inferredReturnValue{};
			if (inst.returnType.isDeduced()) {
				expr->type = inst.returnType;
				inferredReturnValue = evaluatePureFunctionCallReturnValue(expr, def, matchedSection, inst, context);
				context.setExpressionValue(expr, inferredReturnValue);
			}
			bool canCacheStableTrialInstantiation = context.trial && context.allowTrialSummaryReuse && !trialCacheKey.empty() &&
													context.typesValid && inst.valid && !inst.inferring && !inst.needsReinfer &&
													inst.returnType.isDeduced() &&
													!context.observedInProgressUndeducedInstantiation;
			if (canCacheStableTrialInstantiation) {
				TrialInstantiationSummary summary;
				summary.returnType = inst.returnType;
				summary.returnValue = inferredReturnValue;
				summary.purity = inst.purity;
				(*context.ensureTrialInstantiationCache())[trialCacheKey] = std::move(summary);
				traceTrialInstantiationCacheEvent("store", def, trialCacheKey);
			}
			if (refinedInstantiationKey && *refinedInstantiationKey != instantiationKey) {
				retargetTrialSectionInstantiationWriteOrCrash(
					context, matchedSection, instantiationKey, *refinedInstantiationKey, "trial inference"
				);
				auto instIt = matchedSection->instantiations.find(instantiationKey);
				requireCompilerInvariant(
					instIt != matchedSection->instantiations.end(), "Missing provisional instantiation to refine"
				);
				auto node = matchedSection->instantiations.extract(instIt);
				node.key() = *refinedInstantiationKey;
				auto insertResult = matchedSection->instantiations.insert(std::move(node));
				requireCompilerInvariant(insertResult.inserted, "Refined instantiation key collided with existing entry");
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
	if (!isCompileTimeKnown(inferredValue) && expr->type.kind == DataType::Kind::Type && !expr->inferredFlexExpansion)
		context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
}

static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	inferOrderedExpression(expr, context, bindingFrameStack, true);
	return context.typesValid;
}
