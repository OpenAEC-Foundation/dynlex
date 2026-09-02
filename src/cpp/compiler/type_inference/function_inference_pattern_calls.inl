struct PatternCallResolution {
	PatternDefinition *definition;
	size_t pathIndex;
	std::vector<DataType> argumentTypes;
	std::vector<TypeConstraint> argumentConstraints;
};

static bool inferPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack, bool preserveCurrentGrouping
);

static std::optional<ResolvedPatternConstraint> resolvePatternConstraintForInference(
	InferenceContext &context, PatternDefinition *definition, size_t pathIndex, size_t argumentIndex,
	const std::vector<DataType> &argumentTypes, const std::vector<CompileTimeValue> &argumentValues
) {
	if (context.unresolvedPatternConstraintSignal)
		return resolveInitialPatternConstraint(definition, pathIndex, argumentIndex);
	return resolveCompiledPatternConstraint(definition, pathIndex, argumentIndex, argumentTypes, argumentValues);
}

struct ConversionCandidate {
	PatternDefinition *definition{};
	size_t pathIndex{};
	TypeConstraint sourceConstraint;
	DataType outcomeType;
};

struct ConversionLookup {
	std::optional<ConversionCandidate> selected;
	std::vector<ConversionCandidate> ambiguous;
};

struct ConversionOutcome {
	DataType type;
	bool compileTimeKnown = false;
};

static Expression *
createConversionCall(InferenceContext &context, Expression *source, PatternDefinition *definition, size_t pathIndex) {
	requireCompilerInvariant(source != nullptr, "conversion call requires a source expression");
	requireCompilerInvariant(
		definition && definition->section && definition->section->isConversion,
		"conversion call requires a conversion definition"
	);
	requireCompilerInvariant(
		pathIndex < definition->indexedNodePaths.size() && definition->indexedNodePaths[pathIndex].size() == 1,
		"conversion definition does not have one indexed parameter"
	);

	auto match = std::make_unique<PatternMatch>();
	match->nodesPassed = definition->indexedNodePaths[pathIndex];
	match->matchedEndNode = match->nodesPassed.back();
	match->matchingDefinitions = {definition};
	match->lineStartPos = static_cast<size_t>(std::max(0, source->range.start()));
	match->lineEndPos = static_cast<size_t>(std::max(0, source->range.end()));
	PatternMatch *matchPointer = match.get();
	context.parseContext.ownedSyntheticPatternMatches.push_back(std::move(match));

	auto *call = new Expression();
	context.parseContext.ownedClonedExpressions.push_back(call);
	call->kind = Expression::Kind::PatternCall;
	call->range = source->range;
	call->patternMatch = matchPointer;
	Expression *sourceClone = context.parseContext.cloneExpressionTree(source, true);
	if (!source->reusableTemplateExpression)
		sourceClone->reusableTemplateExpression = nullptr;
	context.setExpressionEvaluation(
		sourceClone,
		{
			.value = context.lookupExpressionValue(source),
			.minimumIntegerEffects = context.lookupExpressionMinimumIntegerEffects(source),
		}
	);
	call->arguments.push_back(sourceClone);
	return call;
}

static std::optional<ConversionOutcome> probeConversionOutcome(
	Expression *source, const BindingFrameStack &bindingFrameStack, InferenceContext &context,
	const ConversionCandidate &candidate
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
	trialContext.unresolvedPatternConstraintSignal = context.unresolvedPatternConstraintSignal;
	trialContext.detectGroupingAmbiguity = false;

	Expression *call = createConversionCall(trialContext, source, candidate.definition, candidate.pathIndex);
	bool inferred = inferPatternCall(call, trialContext, bindingFrameStack, true) && trialContext.typesValid;
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
	if (!source || source->inferredConversion || !source->type.isDeduced() || !targetConstraint.isResolved()) {
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
		if (!sourceSelection)
			continue;
		std::optional<ResolvedPatternConstraint> sourceConstraint =
			resolveSourceConstraint(definition, sourceSelection.pathIndex, 0);
		requireCompilerInvariant(sourceConstraint.has_value(), "selected conversion constraint could not be materialized");
		ConversionCandidate candidate{definition, sourceSelection.pathIndex, std::move(sourceConstraint->constraint), {}};
		std::optional<ConversionOutcome> outcome = probeConversionOutcome(source, bindingFrameStack, context, candidate);
		if (outcome && targetConstraint.accepts(outcome->type, outcome->compileTimeKnown)) {
			candidate.outcomeType = outcome->type;
			matches.push_back(candidate);
		}
	}
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
	if (source->inferredConversion)
		return source->inferredConversion->type == targetType;
	if (source->type == targetType)
		return true;
	TypeConstraint targetConstraint = TypeConstraint::fromValueType(targetType);
	targetConstraint.requiresCompileTimeValue = requireCompileTimeResult;
	ConversionLookup lookup = findUserConversion(source, targetConstraint, implicitOnly, context, bindingFrameStack);
	if (!lookup.ambiguous.empty()) {
		reportAmbiguousUserConversion(source, typeToUserName(targetType), lookup.ambiguous, context);
		return false;
	}
	if (!lookup.selected)
		return false;
	Expression *call = createConversionCall(context, source, lookup.selected->definition, lookup.selected->pathIndex);
	if (!inferPatternCall(call, context, bindingFrameStack, true) || !context.typesValid)
		return false;
	requireCompilerInvariant(
		lookup.selected->outcomeType == targetType, "selected conversion outcome differs from its target type"
	);
	requireCompilerInvariant(call->type == targetType, "committed conversion outcome differs from its successful probe");
	source->inferredConversion = call;
	context.parseContext.expressionsWithInferredConversions.push_back(source);
	context.setExpressionEvaluation(
		source,
		{
			.value = context.lookupExpressionValue(call),
			.minimumIntegerEffects = context.lookupExpressionMinimumIntegerEffects(call),
		}
	);
	return true;
}

struct ImplicitPatternOverload {
	PatternDefinition *definition{};
	size_t pathIndex{};
	std::vector<std::optional<DataType>> conversionTargets;
	std::vector<bool> conversionRequiresCompileTime;
	std::vector<TypeConstraint> constraints;
	size_t conversionCount = 0;
};

struct DeferredConversionAmbiguity {
	Expression *source{};
	std::string targetDescription;
	std::vector<ConversionCandidate> candidates;
};

static std::optional<ImplicitPatternOverload> selectOverloadUsingImplicitConversions(
	const std::vector<PatternDefinition *> &definitions, const std::vector<PatternTreeNode *> &nodesPassed,
	const std::vector<Expression *> &arguments, const std::vector<DataType> &argumentTypes,
	const std::vector<bool> &argumentCompileTimeKnown, const std::vector<CompileTimeValue> &argumentValues,
	InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	std::vector<ImplicitPatternOverload> candidates;
	std::optional<DeferredConversionAmbiguity> deferredAmbiguity;
	for (PatternDefinition *definition : definitions) {
		for (size_t pathIndex : matchingPatternPathIndices(nodesPassed, definition)) {
			ImplicitPatternOverload candidate;
			candidate.definition = definition;
			candidate.pathIndex = pathIndex;
			candidate.conversionTargets.resize(arguments.size());
			candidate.conversionRequiresCompileTime.resize(arguments.size());
			bool possible = true;
			std::optional<DeferredConversionAmbiguity> candidateAmbiguity;
			size_t argumentIndex = 0;
			forEachPatternParameterName(definition, pathIndex, [&](const std::string &, PatternTreeNode *, size_t) {
				if (argumentIndex >= argumentTypes.size()) {
					possible = false;
					argumentIndex++;
					return;
				}
				if (!possible) {
					argumentIndex++;
					return;
				}
				std::optional<ResolvedPatternConstraint> resolvedConstraint = resolvePatternConstraintForInference(
					context, definition, pathIndex, argumentIndex, argumentTypes, argumentValues
				);
				if (!resolvedConstraint) {
					possible = false;
					argumentIndex++;
					return;
				}
				TypeConstraint parameterConstraint = resolvedConstraint->effectiveConstraint();
				candidate.constraints.push_back(parameterConstraint);
				const DataType &argumentType = argumentTypes[argumentIndex];
				bool compileTimeKnown =
					argumentIndex < argumentCompileTimeKnown.size() && argumentCompileTimeKnown[argumentIndex];
				if (resolvedConstraint->accepts(argumentType, compileTimeKnown)) {
					argumentIndex++;
					return;
				}
				ConversionLookup lookup =
					findUserConversion(arguments[argumentIndex], parameterConstraint, true, context, bindingFrameStack);
				if (!lookup.selected) {
					if (!lookup.ambiguous.empty() && !candidateAmbiguity) {
						candidateAmbiguity = DeferredConversionAmbiguity{
							arguments[argumentIndex], typeToUserName(parameterConstraint), std::move(lookup.ambiguous)
						};
					} else if (lookup.ambiguous.empty()) {
						possible = false;
					}
					argumentIndex++;
					return;
				}
				candidate.conversionTargets[argumentIndex] = lookup.selected->outcomeType;
				candidate.conversionRequiresCompileTime[argumentIndex] = parameterConstraint.requiresCompileTimeValue;
				candidate.conversionCount++;
				argumentIndex++;
			});
			if (argumentIndex != argumentTypes.size())
				possible = false;
			if (possible && candidateAmbiguity) {
				if (!deferredAmbiguity)
					deferredAmbiguity = std::move(candidateAmbiguity);
			} else if (possible && candidate.conversionCount > 0) {
				candidates.push_back(std::move(candidate));
			}
		}
	}
	if (candidates.empty()) {
		if (deferredAmbiguity) {
			reportAmbiguousUserConversion(
				deferredAmbiguity->source, deferredAmbiguity->targetDescription, deferredAmbiguity->candidates, context
			);
		}
		return std::nullopt;
	}
	std::vector<size_t> maximalCandidates;
	for (size_t candidateIndex = 0; candidateIndex < candidates.size(); candidateIndex++) {
		bool dominated = false;
		for (size_t otherIndex = 0; otherIndex < candidates.size(); otherIndex++) {
			if (candidateIndex == otherIndex)
				continue;
			ConstraintSpecificity relation =
				compareConstraintSpecificity(candidates[candidateIndex].constraints, candidates[otherIndex].constraints);
			if (relation == ConstraintSpecificity::RightMoreSpecific ||
				(relation == ConstraintSpecificity::Equivalent &&
				 candidates[otherIndex].conversionCount < candidates[candidateIndex].conversionCount)) {
				dominated = true;
				break;
			}
		}
		if (!dominated)
			maximalCandidates.push_back(candidateIndex);
	}
	requireCompilerInvariant(!maximalCandidates.empty(), "implicit conversion domains have no maximal candidate");
	std::ranges::sort(maximalCandidates, [&](size_t left, size_t right) {
		const ImplicitPatternOverload &leftCandidate = candidates[left];
		const ImplicitPatternOverload &rightCandidate = candidates[right];
		if (leftCandidate.definition != rightCandidate.definition)
			return patternDefinitionComesBefore(leftCandidate.definition, rightCandidate.definition);
		return leftCandidate.pathIndex < rightCandidate.pathIndex;
	});
	if (maximalCandidates.size() > 1) {
		std::string detail = "More than one overload is equally specific after implicit conversion";
		context.setTypeFailure(detail);
		Diagnostic diagnostic = buildFailureDetailDiagnostic(arguments.empty() ? Range() : arguments.front()->range, detail);
		diagnostic.relatedInfo.push_back(
			{"Candidate overload is defined here", candidates[maximalCandidates[0]].definition->range}
		);
		diagnostic.relatedInfo.push_back(
			{"Candidate overload is defined here", candidates[maximalCandidates[1]].definition->range}
		);
		context.fail(std::move(diagnostic), 1);
		return std::nullopt;
	}
	return candidates[maximalCandidates.front()];
}

static std::optional<PatternCallResolution> resolvePatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack, bool preserveCurrentGrouping
) {
	expr->selectedPatternDefinition = nullptr;
	expr->selectedPatternPathIndex = std::nullopt;
	expr->selectedInstantiation = nullptr;
	auto &defs = expr->patternMatch->matchingDefinitions;
	if (definitionsHaveUnresolvedTypeConstraints(defs)) {
		if (!context.unresolvedPatternConstraintSignal)
			crashCompilerBug("normal type inference encountered an unresolved pattern type constraint");
		*context.unresolvedPatternConstraintSignal = true;
		context.typesValid = false;
		return std::nullopt;
	}

	// Build argument types for overload selection.
	// Arguments are sorted by source position and include captured variables.
	std::vector<DataType> argTypesForOverload;
	std::vector<bool> argCompileTimeKnown;
	std::vector<CompileTimeValue> argCompileTimeValues;
	argCompileTimeKnown.reserve(expr->arguments.size());
	argCompileTimeValues.reserve(expr->arguments.size());
	for (size_t ai = 0; ai < expr->arguments.size(); ai++) {
		Expression *inferArg = expr->arguments[ai];
		DataType argumentType =
			requestKnownOrInferExpressionType(inferArg, context, flexBindingFrameStack, preserveCurrentGrouping);
		argTypesForOverload.push_back(argumentType);
		expr->arguments[ai] = inferArg;
		CompileTimeValue argumentValue = resolveStoredCompileTimeValue(inferArg, flexBindingFrameStack, &context);
		argCompileTimeKnown.push_back(argumentType.isMetaType() || isCompileTimeKnown(argumentValue));
		argCompileTimeValues.push_back(std::move(argumentValue));
	}
	auto overloadFailurePriority = [&](Range diagnosticRange) {
		(void)diagnosticRange;
		for (const DataType &argType : argTypesForOverload) {
			if (argType.kind == DataType::Kind::Void)
				return 0;
		}
		return 1;
	};

	std::map<std::tuple<PatternDefinition *, size_t, size_t>, ResolvedPatternConstraint> resolvedConstraints;
	PatternConstraintResolver resolveConstraint = [&](PatternDefinition *candidate, size_t pathIndex,
													  size_t argumentIndex) -> std::optional<ResolvedPatternConstraint> {
		auto key = std::make_tuple(candidate, pathIndex, argumentIndex);
		auto existing = resolvedConstraints.find(key);
		if (existing != resolvedConstraints.end())
			return existing->second;
		std::optional<ResolvedPatternConstraint> resolution = resolvePatternConstraintForInference(
			context, candidate, pathIndex, argumentIndex, argTypesForOverload, argCompileTimeValues
		);
		if (!resolution)
			return std::nullopt;
		resolvedConstraints.emplace(key, *resolution);
		return resolution;
	};

	// Select the best overload based on argument types
	PatternOverloadSelection overload = selectOverload(
		defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload, argCompileTimeKnown, resolveConstraint
	);
	if (overload.ambiguous) {
		std::string detail = renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, expr->range), "ambiguous overload for call", "message",
			{{"call", (std::string)expr->range.subString}, {"argument_types", formatTypeList(argTypesForOverload)}}
		);
		context.setTypeFailure(detail);
		context.fail(buildFailureDetailDiagnostic(expr->range, detail), overloadFailurePriority(expr->range));
		return std::nullopt;
	}
	if (!overload) {
		std::optional<ImplicitPatternOverload> convertedOverload = selectOverloadUsingImplicitConversions(
			defs, expr->patternMatch->nodesPassed, expr->arguments, argTypesForOverload, argCompileTimeKnown,
			argCompileTimeValues, context, flexBindingFrameStack
		);
		if (convertedOverload) {
			for (size_t argumentIndex = 0; argumentIndex < convertedOverload->conversionTargets.size(); argumentIndex++) {
				if (!convertedOverload->conversionTargets[argumentIndex])
					continue;
				if (!tryApplyUserConversion(
						expr->arguments[argumentIndex], *convertedOverload->conversionTargets[argumentIndex], true, context,
						flexBindingFrameStack, convertedOverload->conversionRequiresCompileTime[argumentIndex]
					)) {
					return std::nullopt;
				}
				argTypesForOverload[argumentIndex] = effectiveInferredExpressionType(expr->arguments[argumentIndex]);
				argCompileTimeKnown[argumentIndex] =
					isCompileTimeKnown(context.lookupExpressionValue(expr->arguments[argumentIndex]));
			}
			overload = {convertedOverload->definition, convertedOverload->pathIndex};
		}
	}
	if (!overload) {
		if (!context.typesValid)
			return std::nullopt;
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
			 {"argument_types", formatTypeList(argTypesForOverload)},
			 {"overloads", candidates}}
		);
		context.setTypeFailure(detail);
		std::vector<RelatedInfo> implicitPromotionRelatedInfo;
		for (PatternDefinition *candidate : defs) {
			for (size_t pathIndex : matchingPatternPathIndices(expr->patternMatch->nodesPassed, candidate)) {
				std::vector<std::pair<std::string, Expression *>> candidateBindings;
				collectPatternCallBindingPairsForPath(expr, candidate, pathIndex, candidateBindings);
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
		return std::nullopt;
	}
	PatternDefinition *def = overload.definition;
	std::vector<TypeConstraint> argumentConstraints;
	argumentConstraints.reserve(argTypesForOverload.size());
	size_t constraintArgumentIndex = 0;
	forEachPatternParameterName(
		def, overload.pathIndex,
		[&](const std::string &parameterName, PatternTreeNode *, size_t startPos) {
		const DefinitionPatternElement *parameterElement = matchedPatternParameterElement(def, parameterName, startPos);
		requireCompilerInvariant(parameterElement, "selected overload parameter has no definition element");
		std::optional<ResolvedPatternConstraint> constraint =
			resolveConstraint(def, overload.pathIndex, constraintArgumentIndex);
		requireCompilerInvariant(constraint.has_value(), "selected overload lost its resolved parameter constraint");
		constraint->constraint.requiresCompileTimeValue =
			constraint->constraint.requiresCompileTimeValue || constraint->requiresCompileTimeValue;
		argumentConstraints.push_back(std::move(constraint->constraint));
		constraintArgumentIndex++;
	}
	);
	requireCompilerInvariant(
		argumentConstraints.size() == argTypesForOverload.size(), "selected overload constraints and arguments diverged"
	);
	expr->selectedPatternDefinition = def;
	expr->selectedPatternPathIndex = overload.pathIndex;
	return PatternCallResolution{def, overload.pathIndex, std::move(argTypesForOverload), std::move(argumentConstraints)};
}

static void inferClassPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack,
	const PatternCallResolution &resolution
) {
	PatternDefinition *def = resolution.definition;
	Section *matchedSection = def->section;
	BindingFrameStack callBindingFrameStack = flexBindingFrameStack;
	pushPatternCallBindingScope(callBindingFrameStack, expr, def);
	auto *classSec = static_cast<ClassSection *>(matchedSection);
	expr->type = instantiateBoundClassType(context.parseContext, classSec->classDefinition, callBindingFrameStack, &context);
	if (expr->type.kind == DataType::Kind::Type)
		context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
}

static void inferFlexPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack,
	const PatternCallResolution &resolution
) {
	PatternDefinition *def = resolution.definition;
	Section *matchedSection = def->section;
	const std::vector<DataType> &argumentTypes = resolution.argumentTypes;
	struct FlexInferenceScope {
		InferenceContext &context;
		size_t activeDefinitionCount;
		size_t activeExpansionKeyCount;
		size_t activeCallCount;
		size_t callSiteCount;
		size_t bodyFrameCount;
		~FlexInferenceScope() {
			context.activeFlexDefinitionStack.resize(activeDefinitionCount);
			context.activeFlexExpansionKeys.resize(activeExpansionKeyCount);
			context.activeFlexCallStack.resize(activeCallCount);
			context.flexCallSiteSectionStack.resize(callSiteCount);
			context.sectionFlexBodyFrames.resize(bodyFrameCount);
		}
	} flexInferenceScope{
		context,
		context.activeFlexDefinitionStack.size(),
		context.activeFlexExpansionKeys.size(),
		context.activeFlexCallStack.size(),
		context.flexCallSiteSectionStack.size(),
		context.sectionFlexBodyFrames.size()
	};
	FlexExpansionKey expansionKey;
	expansionKey.definition = def;
	expansionKey.pathIndex = resolution.pathIndex;
	expansionKey.argumentTypes = argumentTypes;
	expansionKey.compileTimeArguments.reserve(expr->arguments.size());
	for (size_t argumentIndex = 0; argumentIndex < expr->arguments.size(); argumentIndex++) {
		CompileTimeValue value = context.lookupExpressionValue(expr->arguments[argumentIndex]);
		if (!isCompileTimeKnown(value) && argumentTypes[argumentIndex].kind == DataType::Kind::Type)
			value = TypeReferenceValue::exact(argumentTypes[argumentIndex]);
		expansionKey.compileTimeArguments.push_back(std::move(value));
	}
	if (std::ranges::any_of(context.activeFlexExpansionKeys, [&](const std::optional<FlexExpansionKey> &activeKey) {
		return activeKey && *activeKey == expansionKey;
	})) {
		context.setTypeFailure("recursive flex expansion");
		return;
	}
	context.activeFlexDefinitionStack.push_back(matchedSection);
	context.activeFlexExpansionKeys.push_back(std::move(expansionKey));
	context.activeFlexCallStack.push_back(expr);
	Section *callSiteSection = expr->range.line ? expr->range.line->section : nullptr;
	if (callSiteSection)
		context.flexCallSiteSectionStack.push_back(callSiteSection);
	std::optional<size_t> sectionBodyFrameIndex;
	if (matchedSection->type == SectionType::Section) {
		Section *callerBodySection = expr->range.line ? expr->range.line->sectionOpening : nullptr;
		if (callerBodySection) {
			InferenceContext::SectionFlexBodyInferenceFrame frame;
			frame.definitionSection = matchedSection;
			frame.bodySection = callerBodySection;
			frame.callerBindings = flexBindingFrameStack;
			frame.instantiatedBody = context.currentInstantiatedSectionBody
										 ? context.currentInstantiatedSectionBody->bodyForChild(callerBodySection)
										 : nullptr;
			sectionBodyFrameIndex = context.sectionFlexBodyFrames.size();
			context.sectionFlexBodyFrames.push_back(std::move(frame));
		}
	}
	BindingFrameStack callBindingFrameStack = flexBindingFrameStack;
	pushPatternCallBindingScope(callBindingFrameStack, expr, def);
	std::shared_ptr<InstantiatedSectionBody> flexBody = context.parseContext.cloneSectionBody(matchedSection);
	if (sectionBodyFrameIndex)
		context.sectionFlexBodyFrames[*sectionBodyFrameIndex].definitionBody = flexBody.get();
	bool flexFallsThrough = true;
	bool bodyInferred = matchedSection->forEachDefinitionBodySection([&](Section *definitionBodySection) {
		InstantiatedSectionBody *activeBody =
			definitionBodySection == matchedSection ? flexBody.get() : flexBody->bodyForChild(definitionBodySection);
		requireCompilerInvariant(activeBody, "flex clone is missing its definition body");
		bool definitionBodyFallsThrough = true;
		bool inferred = inferSection(
			definitionBodySection, activeBody, nullptr, context, callBindingFrameStack, &definitionBodyFallsThrough
		);
		flexFallsThrough = flexFallsThrough && definitionBodyFallsThrough;
		return inferred;
	});
	if (!bodyInferred || !context.typesValid)
		return;
	Expression *templateBodyExpression = flexPatternBodyExpression(def);
	Expression *bodyExpr = flexBody->findCloneOf(templateBodyExpression);
	if (!bodyExpr) {
		crashCompilerBug("flex clone is missing its replacement expression");
		return;
	}
	bodyExpr->isExplicitGroup = true;
	expr->inferredFlexBody = std::move(flexBody);
	expr->inferredFlexExpansion = bodyExpr;
	Expression *outcomeExpression = bodyExpr;
	if (matchedSection->type == SectionType::Function) {
		Expression *templateOutcomeExpression = singleExpressionFlexFunctionOutcome(def);
		outcomeExpression =
			templateOutcomeExpression ? expr->inferredFlexBody->findCloneOf(templateOutcomeExpression) : nullptr;
	}
	// A nested flex may transfer control through this active call while
	// its replacement is being inferred. That direct outcome takes
	// precedence over the replacement expression's forwarded outcome.
	if (expr->sectionOutcome.kind == Expression::SectionOutcome::Kind::None)
		expr->sectionOutcome = outcomeExpression ? outcomeExpression->sectionOutcome : Expression::SectionOutcome{};
	if (!flexFallsThrough && expr->sectionOutcome.kind == Expression::SectionOutcome::Kind::None)
		expr->sectionOutcome.kind = Expression::SectionOutcome::Kind::FunctionReturn;
	if (sectionBodyFrameIndex) {
		size_t frameIndex = *sectionBodyFrameIndex;
		if (!context.sectionFlexBodyFrames[frameIndex].bodyInferred &&
			expr->sectionOutcome.kind == Expression::SectionOutcome::Kind::None) {
			Section *executionSection = bodyExpr->range.line ? bodyExpr->range.line->section : matchedSection;
			if (!inferSectionFlexCallerBodyFrame(frameIndex, executionSection, expr, context)) {
				return;
			}
		}
		expr->sectionBodyInferred = context.sectionFlexBodyFrames[frameIndex].bodyInferred;
		expr->sectionBodyFallsThrough = context.sectionFlexBodyFrames[frameIndex].bodyFallsThrough;
	}
	DataType resolvedType = matchedSection->type == SectionType::Section ? DataType{DataType::Kind::Void} : bodyExpr->type;
	if (resolvedType.isDeduced())
		expr->type = resolvedType;
	context.setExpressionValue(expr, context.lookupExpressionValue(bodyExpr));
}

static bool inferNonFlexPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack,
	const PatternCallResolution &resolution
) {
	PatternDefinition *def = resolution.definition;
	Section *matchedSection = def->section;
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
			if (argTypeResult.deferred && context.currentInstantiation)
				return false;
			if (context.trial) {
				context.setTypeFailure(renderConfiguredMessage(
					syntaxConfigForRange(context.parseContext, expr->range), "undeduced argument type in trial inference"
				));
				DefinitionPatternElement *parameterElement = findParameterElement(def->patternElements, parameterName);
				if (parameterElement && parameterElement->promotedFromVariableLike && argumentExpression &&
					argumentExpression->kind == Expression::Kind::Variable && argumentExpression->variable &&
					argumentExpression->variable->name == parameterName) {
					appendImplicitPromotionTrace(context.typeFailureRelatedInfo, def, parameterName);
				}
				return false;
			}
			requireCompilerInvariant(
				argType.isDeduced(), "Undeduced argument type encountered during non-flex pattern-call inference"
			);
		}
		argTypes.push_back(argType);
	}
	std::unordered_set<std::string> explicitCompileTimeParameters = collectExplicitCompileTimeParameters(
		def, paramBindings, resolution.pathIndex, argTypes, resolution.argumentConstraints
	);
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
		auto callerKnownConstants = context.currentVariableValues;
		auto callerAddressState = context.currentAddressState;
		InferenceContext::SubjectState callerSubject = context.currentSubject;
		bool instantiationFallsThrough = true;
		bool inferenceSucceeded = runInstantiationReinferenceLoop(
			context, inst, def, expr->range, (std::string)def->range.subString, savedInst != nullptr,
			[&]() -> bool {
			seedInstantiationParameterTypes(inst, paramBindings, argTypes);
			inst.parameterOutputTypesByName.clear();
			inst.writtenGlobalReferences.clear();
			inst.finalGlobalConstantValues.clear();
			inst.finalGlobalAddressProvenance.clear();
			inst.addressTakenGlobalReferences.clear();
			inst.externallyEscapedGlobalProvenance = {};
			inst.returnAddressProvenance = {};
			inst.hasReturnAddressProvenance = false;
			inst.returnPointerStorageParameterName.reset();
			inst.returnPointerStorageAmbiguous = false;
			inst.writesThroughUnknownAddress = false;
			inst.externallyEscapesUnknownAddress = false;
			inst.requiredCompileTimeParameters = explicitCompileTimeParameters;
			inst.purity = InstantiationPurity::Pure;
			inst.pureReturnValuesByArguments.clear();
			seedInstantiationCompileTimeParameters(
				inst, paramBindings, argTypes, inst.requiredCompileTimeParameters,
				[&](Expression *argumentExpression) {
				if (context.trial) {
					auto trialIt = context.trialExpressionValues.find(argumentExpression);
					if (trialIt != context.trialExpressionValues.end())
						return trialIt->second.value;
					if (context.inheritedTrialExpressionValues) {
						auto inheritedIt = context.inheritedTrialExpressionValues->find(argumentExpression);
						if (inheritedIt != context.inheritedTrialExpressionValues->end())
							return inheritedIt->second.value;
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
			context.currentVariableValues.clear();
			context.currentAddressState = {};
			context.currentSubject = {};
			bool savedReinferSuppression = context.suppressReinferPassDiagnostics;
			context.suppressReinferPassDiagnostics = true;
			if (!inst.body)
				inst.body = context.parseContext.cloneSectionBody(matchedSection);
			bool passSucceeded =
				inferSection(matchedSection, inst.body.get(), nullptr, context, {}, &instantiationFallsThrough);
			inst.fallsThrough = instantiationFallsThrough;
			inst.finalGlobalConstantValues.clear();
			for (VariableReference *reference : inst.writtenGlobalReferences) {
				const KnownConstantStorage &knownConstants = context.currentVariableValues.read();
				auto knownIt = knownConstants.find(reference);
				if (knownIt != knownConstants.end() && isCompileTimeKnown(knownIt->second))
					inst.finalGlobalConstantValues[reference] = knownIt->second;
				const VariableAddressProvenance &variables = context.currentAddressState.read().variables;
				auto provenance = variables.find(reference);
				inst.finalGlobalAddressProvenance[reference] =
					provenance != variables.end() ? provenance->second : AddressProvenance{.mayTargets = {}, .unknown = true};
			}
			for (VariableReference *reference : context.currentAddressState.read().addressTakenVariables) {
				Variable *variable = variableForAddressTarget(reference);
				if (variable && variable->isGlobal)
					inst.addressTakenGlobalReferences.insert(reference);
			}
			for (VariableReference *reference : context.currentAddressState.read().externallyEscaped.mayTargets) {
				Variable *variable = variableForAddressTarget(reference);
				if (variable && variable->isGlobal)
					inst.externallyEscapedGlobalProvenance.mayTargets.insert(reference);
			}
			context.suppressReinferPassDiagnostics = savedReinferSuppression;
			inst.inferring = false;
			return passSucceeded;
		}
		);
		context.currentVariableValues = std::move(callerKnownConstants);
		context.currentAddressState = std::move(callerAddressState);
		context.currentSubject = callerSubject;
		context.currentInstantiation = savedInst;
		inst.valid = inferenceSucceeded;
		if (inferenceSucceeded && inst.needsReinfer)
			propagateUnresolvedRecursiveDependencyToCaller(context, savedInst);
		refinedInstantiationKey =
			buildInstantiationKey(inst.requiredCompileTimeParameters, paramBindings, argTypes, evaluateParameterValue);
	} else if (inst.returnType.isDeduced()) {
		expr->type = inst.returnType;
	} else {
		observeUnresolvedRecursiveDependency(context);
	}
	if (!inst.valid) {
		context.typesValid = false;
		return true;
	}
	if (!context.typesValid)
		return true;
	expr->inferredPointerStorage = nullptr;
	if (!inst.returnPointerStorageAmbiguous && inst.returnPointerStorageParameterName) {
		auto storageBinding = std::ranges::find_if(paramBindings, [&](const auto &entry) {
			return entry.first == *inst.returnPointerStorageParameterName;
		});
		if (storageBinding != paramBindings.end())
			expr->inferredPointerStorage = storageBinding->second;
	}
	for (const auto &[parameterName, outputType] : inst.parameterOutputTypesByName) {
		auto binding = std::ranges::find_if(paramBindings, [&](const auto &entry) {
			return entry.first == parameterName;
		});
		if (binding != paramBindings.end())
			refineStorageExpressionType(binding->second, outputType, context, flexBindingFrameStack);
	}
	mergeCalleeGlobalWritesIntoCaller(context, inst);
	mergeInstantiationPurityIntoCaller(context, inst);
	AddressProvenance callArgumentProvenance;
	for (const auto &[parameterName, argumentExpression] : paramBindings) {
		(void)parameterName;
		joinAddressProvenance(
			callArgumentProvenance, inferAddressProvenance(argumentExpression, context, flexBindingFrameStack)
		);
	}
	if (inst.purity == InstantiationPurity::Impure) {
		invalidateAddressTargets(context, callArgumentProvenance, true);
	}
	if (inst.writesThroughUnknownAddress)
		invalidateAddressTargets(context, {.mayTargets = {}, .unknown = true}, true);
	if (inst.externallyEscapesUnknownAddress) {
		callArgumentProvenance.unknown = true;
		retainExternalAddress(context, callArgumentProvenance);
	}

	// If no return intrinsic was found, default to Void
	if (!inst.inferring && !inst.needsReinfer && inst.returnType.kind == DataType::Kind::Any) {
		inst.returnType = {DataType::Kind::Void};
	}
	CompileTimeEvaluation inferredReturnValue{};
	if (inst.returnType.isDeduced()) {
		expr->type = inst.returnType;
		inferredReturnValue =
			evaluatePureFunctionCallReturnValue(expr, def, matchedSection, inst, context, flexBindingFrameStack);
		context.setExpressionEvaluation(expr, std::move(inferredReturnValue));
	}
	if (refinedInstantiationKey && *refinedInstantiationKey != instantiationKey) {
		retargetTrialSectionInstantiationWriteOrCrash(
			context, matchedSection, instantiationKey, *refinedInstantiationKey, "trial inference"
		);
		auto instIt = matchedSection->instantiations.find(instantiationKey);
		requireCompilerInvariant(instIt != matchedSection->instantiations.end(), "Missing provisional instantiation to refine");
		auto node = matchedSection->instantiations.extract(instIt);
		node.key() = *refinedInstantiationKey;
		auto insertResult = matchedSection->instantiations.insert(std::move(node));
		requireCompilerInvariant(insertResult.inserted, "Refined instantiation key collided with existing entry");
	}
	context.setExpressionEvaluation(
		expr,
		{
			.value = context.lookupExpressionValue(expr),
			.minimumIntegerEffects = context.lookupExpressionMinimumIntegerEffects(expr),
		}
	);
	return true;
}

static bool inferPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack, bool preserveCurrentGrouping
) {
	std::optional<PatternCallResolution> resolution =
		resolvePatternCall(expr, context, flexBindingFrameStack, preserveCurrentGrouping);
	if (!resolution)
		return true;
	Section *matchedSection = resolution->definition->section;
	if (matchedSection->type == SectionType::Class && !matchedSection->isFlex) {
		inferClassPatternCall(expr, context, flexBindingFrameStack, *resolution);
	} else if (matchedSection->isFlex) {
		inferFlexPatternCall(expr, context, flexBindingFrameStack, *resolution);
	} else {
		return inferNonFlexPatternCall(expr, context, flexBindingFrameStack, *resolution);
	}
	return true;
}
