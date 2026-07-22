#pragma once

static Variable *findOwnSectionVariable(Section *section, const std::string &name) {
	if (!section)
		return nullptr;
	auto it = section->variables.find(name);
	return it != section->variables.end() ? it->second : nullptr;
}

static void seedNonFlexSectionParameterState(Section *section, InferenceContext &context) {
	if (!section || !context.currentInstantiation || section->isFlex || section->patternDefinitions.empty())
		return;
	for (const auto &[name, parameterType] : context.currentInstantiation->parameterTypesByName) {
		Variable *parameterVariable = findOwnSectionVariable(section, name);
		if (!parameterVariable || parameterVariable->isGlobal)
			continue;
		parameterVariable->type = concretizeClassType(parameterType);
		parameterVariable->typeOriginRange = parameterVariable->definition ? parameterVariable->definition->range : Range();
		parameterVariable->typeOriginFloatLiteralReplacement.clear();
	}
	for (const auto &[name, value] : context.currentInstantiation->constantParameterValues) {
		Variable *parameterVariable = findOwnSectionVariable(section, name);
		if (parameterVariable)
			context.setKnownConstant(parameterVariable->definition, value);
	}
}

static bool sectionOutcomeIsLoop(Expression *openingExpression) {
	return openingExpression && openingExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Loop;
}

static std::optional<Expression::SectionOutcome::Kind> finalizedBranchOutcome(Expression *expression) {
	if (!expression)
		return std::nullopt;
	Expression::SectionOutcome::Kind kind = expression->sectionOutcome.kind;
	if (kind != Expression::SectionOutcome::Kind::Conditional &&
		kind != Expression::SectionOutcome::Kind::AlternativeConditional &&
		kind != Expression::SectionOutcome::Kind::Alternative) {
		return std::nullopt;
	}
	return kind;
}

static void mergeSectionExecutionStates(
	InferenceContext &context, const std::vector<std::unordered_map<VariableReference *, CompileTimeValue>> &constantStates,
	const std::vector<InferenceContext::SubjectState> &subjectStates
) {
	requireCompilerInvariant(!constantStates.empty(), "section state merge requires a reachable path");
	requireCompilerInvariant(
		constantStates.size() == subjectStates.size(), "section constant and subject state counts diverged"
	);
	std::unordered_map<VariableReference *, CompileTimeValue> mergedConstants;
	for (const auto &state : constantStates) {
		for (const auto &[reference, value] : state) {
			(void)value;
			mergedConstants.try_emplace(reference, CompileTimeValue{});
		}
	}
	for (auto &[reference, mergedValue] : mergedConstants) {
		auto first = constantStates.front().find(reference);
		if (first == constantStates.front().end())
			continue;
		bool sameValueOnEveryPath = std::ranges::all_of(constantStates, [&](const auto &state) {
			auto value = state.find(reference);
			return value != state.end() && value->second == first->second;
		});
		if (sameValueOnEveryPath)
			mergedValue = first->second;
	}
	context.currentVariableValues = std::move(mergedConstants);
	context.currentSubject = subjectStates.front();
	for (size_t index = 1; index < subjectStates.size(); index++) {
		if (subjectStates[index] != context.currentSubject) {
			context.currentSubject = {.setter = nullptr, .ambiguous = true};
			break;
		}
	}
}

static std::optional<Expression::SectionOutcome::Kind> inferExpressionSectionOutcomeInTrial(
	Expression *expression, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &bindingFrameStack
) {
	if (!expression)
		return std::nullopt;
	GroupingSnapshot originalGrouping = captureGroupingSnapshot(expression);
	resetExpressionTypes(expression);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.inheritSectionExecutionState(context);
	trialContext.currentVariableValues = context.currentVariableValues;
	trialContext.currentSubject = context.currentSubject;
	trialContext.inheritedTrialExpressionValues =
		context.trial ? &context.trialExpressionValues : context.inheritedTrialExpressionValues;
	trialContext.trialJournal = &journal;
	trialContext.unresolvedPatternConstraintSignal = context.unresolvedPatternConstraintSignal;
	trialContext.detectGroupingAmbiguity = false;
	Expression *trialExpression = expression;
	std::optional<Expression::SectionOutcome::Kind> kind;
	if (inferExpression(trialExpression, trialContext, alreadyOrdered, bindingFrameStack, false) && trialContext.typesValid)
		kind = finalizedBranchOutcome(trialExpression);
	rollbackTrialJournal(journal);
	applyGroupingSnapshot(originalGrouping);
	recomputeRanges(expression);
	resetExpressionTypes(expression);
	return kind;
}

static bool inferNextLoopHeaderOutcomeInTrial(
	Expression *openingExpression, InferenceContext &context, const BindingFrameStack &bindingFrameStack,
	Expression::SectionOutcome &outcome
) {
	requireCompilerInvariant(sectionOutcomeIsLoop(openingExpression), "loop reinference has no loop header outcome");
	Expression *trialExpression = context.parseContext.cloneExpressionTree(openingExpression);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.inheritSectionExecutionState(context);
	trialContext.currentVariableValues = context.currentVariableValues;
	trialContext.currentSubject = context.currentSubject;
	trialContext.inheritedTrialExpressionValues =
		context.trial ? &context.trialExpressionValues : context.inheritedTrialExpressionValues;
	trialContext.trialJournal = &journal;
	trialContext.unresolvedPatternConstraintSignal = context.unresolvedPatternConstraintSignal;
	trialContext.detectGroupingAmbiguity = false;
	bool inferred = inferExpression(trialExpression, trialContext, true, bindingFrameStack, false) && trialContext.typesValid;
	if (inferred) {
		requireCompilerInvariant(
			trialExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Loop,
			"loop header reinference produced a different control-flow outcome"
		);
		outcome = trialExpression->sectionOutcome;
	} else {
		context.inheritTypeFailureFrom(trialContext);
		context.typesValid = false;
	}
	rollbackTrialJournal(journal);
	return inferred;
}
