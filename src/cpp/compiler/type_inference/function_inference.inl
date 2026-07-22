#pragma once

#include "compilerUtils.h"
#include "const_evaluation.inl"
#include "sectionFlexBody.h"
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
#include <memory>

static void resetSectionExpressionTypes(Section *section, InstantiatedSectionBody *body = nullptr, size_t startLineIndex = 0);
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
	bool fallsThrough;

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
		instantiation.fallsThrough,
	};
}

// Applies a callee's global write effects to the caller context, mirroring a
// direct store to each written global: the caller's own effect summary and the
// value produced by the callee in the current execution state.
static void mergeCalleeGlobalWritesIntoCaller(InferenceContext &context, const Instantiation &inst) {
	for (VariableReference *reference : inst.writtenGlobalReferences) {
		context.noteWrittenGlobalReference(reference);
		auto it = inst.finalGlobalConstantValues.find(reference);
		context.setKnownConstant(reference, it != inst.finalGlobalConstantValues.end() ? it->second : CompileTimeValue{});
	}
}

static void markCurrentInstantiationImpure(InferenceContext &context) {
	if (!context.currentInstantiation || context.currentInstantiation->purity == InstantiationPurity::Impure)
		return;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "Trial purity mutation requires a rollback journal");
		context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
	}
	context.currentInstantiation->purity = InstantiationPurity::Impure;
}

static bool setCurrentInstantiationReturnType(InferenceContext &context, const DataType &returnType, Range returnRange) {
	if (!context.currentInstantiation || context.currentInstantiation->returnType == returnType)
		return true;
	if (context.currentInstantiation->returnType.isDeduced()) {
		const DataType &existingType = context.currentInstantiation->returnType;
		std::vector<std::pair<std::string, std::string>> replacements = {
			{"first_type", typeToUserName(existingType, context.parseContext)},
			{"second_type", typeToUserName(returnType, context.parseContext)},
		};
		std::string detail = renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, returnRange), "multiple reachable return types", "message", replacements
		);
		context.setTypeFailure(detail);
		context.fail(buildFailureDetailDiagnostic(returnRange, detail));
		return false;
	}
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial return-type mutation requires a rollback journal");
		context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
	}
	context.currentInstantiation->returnType = returnType;
	return true;
}

static void mergeInstantiationPurityIntoCaller(InferenceContext &context, const Instantiation &inst) {
	if (inst.purity == InstantiationPurity::Impure)
		markCurrentInstantiationImpure(context);
}

static void markInstantiationForReinference(InferenceContext &context, Instantiation *instantiation) {
	if (!instantiation || instantiation->needsReinfer)
		return;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial reinference mutation requires a rollback journal");
		context.trialJournal->recordInstantiationWrite(instantiation);
	}
	instantiation->needsReinfer = true;
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
	for (auto it = journal.instantiationUndo.rbegin(); it != journal.instantiationUndo.rend(); ++it) {
		it->instantiation->returnType = it->returnType;
		it->instantiation->writtenGlobalReferences = std::move(it->writtenGlobalReferences);
		it->instantiation->purity = it->purity;
		it->instantiation->fallsThrough = it->fallsThrough;
		it->instantiation->needsReinfer = it->needsReinfer;
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

template <typename ReadCompileTimeFn>
static InstantiationKey getOrCreateNonFlexInstantiationKey(
	Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, const std::unordered_set<std::string> &requiredCompileTimeParameters,
	ReadCompileTimeFn &&readCompileTime
) {
	return findMatchingInstantiationKey(section, paramBindings, argTypes, readCompileTime)
		.value_or(buildInstantiationKey(requiredCompileTimeParameters, paramBindings, argTypes, readCompileTime));
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
	const BindingFrameStack &bindingFrameStack, bool *fallsThrough
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

static void resetSectionExpressionTypes(Section *section, InstantiatedSectionBody *body, size_t startLineIndex) {
	if (!section)
		return;
	for (size_t i = startLineIndex; i < section->codeLines.size(); i++) {
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
		candidateDefinitions = patternCallExpr->patternMatch->matchingDefinitions;

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

#include "function_inference_evaluation.inl"
#include "function_inference_ordered.inl"
