struct ConversionCandidate {
	PatternDefinition *definition{};
	size_t pathIndex{};
	TypeConstraint sourceConstraint;
	DataType outcomeType;
};

struct ConversionLookup {
	std::optional<ConversionCandidate> selected;
	std::vector<ConversionCandidate> ambiguous;
	bool deferred = false;
};

struct ConversionOutcome {
	DataType type;
	bool compileTimeKnown = false;
};

static bool isInsertedConversionCall(const Expression *expression) {
	return expression && expression->syntheticPatternMatch &&
		   expression->syntheticPatternMatch->matchingDefinitions.front()->section->isConversion;
}

static Expression *unconvertedExpression(Expression *expression) {
	while (isInsertedConversionCall(expression)) {
		requireCompilerInvariant(expression->arguments.size() == 1, "conversion call is not unary");
		expression = expression->arguments.front();
	}
	return expression;
}

static Expression makeConversionCall(Expression *source, PatternDefinition *definition, size_t pathIndex) {
	requireCompilerInvariant(source != nullptr, "conversion call requires a source expression");
	requireCompilerInvariant(
		definition && definition->section && definition->section->isConversion,
		"conversion call requires a conversion definition"
	);
	requireCompilerInvariant(
		pathIndex < definition->indexedNodePaths.size() && definition->indexedNodePaths[pathIndex].size() == 1,
		"conversion definition does not have one indexed parameter"
	);
	auto match = std::make_shared<PatternMatch>();
	match->nodesPassed = definition->indexedNodePaths[pathIndex];
	match->matchedEndNode = match->nodesPassed.back();
	match->matchingDefinitions = {definition};
	match->lineStartPos = static_cast<size_t>(std::max(0, source->range.start()));
	match->lineEndPos = static_cast<size_t>(std::max(0, source->range.end()));
	Expression call;
	call.kind = Expression::Kind::PatternCall;
	call.range = source->range;
	call.owningSectionBody = source->owningSectionBody;
	call.patternMatch = match.get();
	call.syntheticPatternMatch = std::move(match);
	call.arguments = {source};
	call.isExplicitGroup = true;
	call.reusableTemplateExpression = source->reusableTemplateExpression;
	call.executionFallsThrough = source->executionFallsThrough;
	call.sectionBodyReachable = source->sectionBodyReachable;
	call.sectionBodyExecution = source->sectionBodyExecution;
	call.sectionBodyFallsThrough = source->sectionBodyFallsThrough;
	call.branchSelection = source->branchSelection;
	return call;
}

static std::optional<ConversionOutcome> probeConversionOutcome(
	Expression *source, const BindingFrameStack &bindingFrameStack, InferenceContext &context,
	const ConversionCandidate &candidate, bool &deferred
) {
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.inheritSectionExecutionState(context);
	trialContext.currentVariableValues = context.currentVariableValues;
	trialContext.currentAddressState = context.currentAddressState;
	trialContext.currentSubject = context.currentSubject;
	trialContext.inheritedTrialExpressionValues =
		context.trial ? &context.trialExpressionValues : context.inheritedTrialExpressionValues;
	trialContext.trialJournal = &journal;
	if (context.unresolvedPatternConstraintSignal)
		trialContext.unresolvedPatternConstraintSignal = std::make_shared<bool>(false);
	trialContext.detectGroupingAmbiguity = false;

	journal.recordBorrowedExpressionWrite(source);
	auto storage = trialContext.createExpressionStorage();
	Expression *call = storage->create();
	*call = makeConversionCall(source, candidate.definition, candidate.pathIndex);
	bool inferred = inferPatternCall(call, trialContext, bindingFrameStack, true) && trialContext.typesValid;
	deferred = trialContext.unresolvedPatternConstraintSignal && *trialContext.unresolvedPatternConstraintSignal;
	std::optional<ConversionOutcome> result;
	if (inferred && call->type.isDeduced() && call->type.kind != DataType::Kind::Void)
		result = ConversionOutcome{call->type, isCompileTimeKnown(trialContext.lookupExpressionValue(call))};
	rollbackTrialJournal(journal);
	return result;
}

static ConversionLookup findUserConversion(
	Expression *source, const TypeConstraint &targetConstraint, bool implicitOnly, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack
) {
	if (!source || isInsertedConversionCall(source) || !source->type.isDeduced() || !targetConstraint.isResolved()) {
		return {};
	}
	PatternTreeNode *root = context.parseContext.patternTrees[(int)SectionType::Conversion];
	if (!root || !root->argumentChild || !source->range.line || !source->range.line->sourceFile)
		return {};

	const DataType sourceType = source->type;
	CompileTimeValue sourceValue = context.lookupExpressionValue(source);
	bool compileTimeKnown = sourceType.isMetaType() || isCompileTimeKnown(sourceValue);
	const std::vector<DataType> sourceTypes{sourceType};
	const std::vector<CompileTimeValue> sourceValues{sourceValue};
	PatternConstraintResolver resolveSourceConstraint = [&](PatternDefinition *definition, size_t pathIndex,
															size_t argumentIndex) {
		return resolvePatternConstraintForInference(context, definition, pathIndex, argumentIndex, sourceTypes, sourceValues);
	};
	std::vector<ConversionCandidate> matches;
	bool hasDeferredCandidate = false;
	for (PatternDefinition *definition : root->argumentChild->matchingDefinitions) {
		if (!definition || !definition->section || !definition->section->isConversion)
			continue;
		if (implicitOnly && !definition->section->isImplicitConversion)
			continue;
		if (!isPatternDefinitionVisibleFromSource(*definition, *source->range.line->sourceFile))
			continue;

		PatternOverloadSelection sourceSelection = selectOverload(
			{definition}, {source}, definition->indexedNodePaths.front(), {sourceType}, {compileTimeKnown},
			resolveSourceConstraint
		);
		if (sourceSelection.deferred) {
			hasDeferredCandidate = true;
			continue;
		}
		if (!sourceSelection)
			continue;
		std::optional<ResolvedPatternConstraint> sourceConstraint =
			resolveSourceConstraint(definition, sourceSelection.pathIndex, 0);
		requireCompilerInvariant(sourceConstraint.has_value(), "selected conversion constraint could not be materialized");
		ConversionCandidate candidate{definition, sourceSelection.pathIndex, std::move(sourceConstraint->constraint), {}};
		if (const auto &record = context.parseContext.recordInferenceDependency)
			context.recordDependencyRead([&](const Section *owner, std::optional<size_t> probe) {
				record(owner, probe, *definition->section);
			});
		bool deferred = false;
		std::optional<ConversionOutcome> outcome =
			probeConversionOutcome(source, bindingFrameStack, context, candidate, deferred);
		hasDeferredCandidate = hasDeferredCandidate || deferred;
		if (outcome && targetConstraint.accepts(outcome->type, outcome->compileTimeKnown)) {
			candidate.outcomeType = outcome->type;
			matches.push_back(candidate);
		}
	}
	if (hasDeferredCandidate)
		return {std::nullopt, {}, true};
	if (matches.empty())
		return {};

	std::vector<ConversionCandidate> maximalMatches;
	for (size_t candidateIndex = 0; candidateIndex < matches.size(); candidateIndex++) {
		bool dominated = false;
		for (size_t otherIndex = 0; otherIndex < matches.size(); otherIndex++) {
			if (candidateIndex == otherIndex)
				continue;
			if (matches[candidateIndex].sourceConstraint.compareSpecificity(matches[otherIndex].sourceConstraint) ==
				ConstraintSpecificity::RightMoreSpecific) {
				dominated = true;
				break;
			}
		}
		if (!dominated)
			maximalMatches.push_back(matches[candidateIndex]);
	}
	requireCompilerInvariant(!maximalMatches.empty(), "conversion domains have no maximal candidate");
	if (maximalMatches.size() == 1) {
		ConversionLookup result;
		result.selected = maximalMatches.front();
		return result;
	}
	ConversionLookup result;
	result.ambiguous = std::move(maximalMatches);
	return result;
}

static void reportAmbiguousUserConversion(
	Expression *source, const std::string &targetDescription, const std::vector<ConversionCandidate> &candidates,
	InferenceContext &context
) {
	requireCompilerInvariant(source != nullptr && candidates.size() > 1, "invalid ambiguous conversion diagnostic");
	std::string detail = renderConfiguredMessage(
		syntaxConfigForRange(context.parseContext, source->range), "ambiguous conversion", "message",
		{{"from_type", typeToUserName(source->type)}, {"to_type", targetDescription}}
	);
	context.setTypeFailure(detail);
	Diagnostic diagnostic = buildFailureDetailDiagnostic(source->range, detail);
	for (const ConversionCandidate &candidate : candidates)
		diagnostic.relatedInfo.push_back({"Conversion is defined here", candidate.definition->range});
	context.fail(std::move(diagnostic), 1);
}

static bool tryApplyUserConversion(
	Expression *source, const DataType &targetType, bool implicitOnly, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack, bool requireCompileTimeResult
) {
	if (!source)
		return false;
	if (isInsertedConversionCall(source))
		return source->type == targetType;
	if (source->type == targetType)
		return true;
	TypeConstraint targetConstraint = TypeConstraint::fromValueType(targetType);
	targetConstraint.requiresCompileTimeValue = requireCompileTimeResult;
	ConversionLookup lookup = findUserConversion(source, targetConstraint, implicitOnly, context, bindingFrameStack);
	if (lookup.deferred) {
		requireCompilerInvariant(
			context.unresolvedPatternConstraintSignal != nullptr, "deferred conversion outside signature inference"
		);
		*context.unresolvedPatternConstraintSignal = true;
		context.typesValid = false;
		return false;
	}
	if (!lookup.ambiguous.empty()) {
		reportAmbiguousUserConversion(source, typeToUserName(targetType), lookup.ambiguous, context);
		return false;
	}
	if (!lookup.selected)
		return false;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial conversion requires a journal");
		context.trialJournal->recordBorrowedExpressionWrite(source);
	}
	CompileTimeValue originalValue = context.lookupExpressionValue(source);
	Expression call = makeConversionCall(source, lookup.selected->definition, lookup.selected->pathIndex);
	auto storage = context.createExpressionStorage();
	Expression *argument = storage->create();
	*argument = std::move(*source);
	call.arguments = {argument};
	call.ownedArgument = std::shared_ptr<Expression>(std::move(storage), argument);
	*source = std::move(call);
	context.setExpressionValue(argument, originalValue);
	context.setExpressionValue(source, {});
	if (!inferPatternCall(source, context, bindingFrameStack, true) || !context.typesValid)
		return false;
	requireCompilerInvariant(
		lookup.selected->outcomeType == targetType, "selected conversion outcome differs from its target type"
	);
	requireCompilerInvariant(source->type == targetType, "committed conversion outcome differs from its successful probe");
	return true;
}
