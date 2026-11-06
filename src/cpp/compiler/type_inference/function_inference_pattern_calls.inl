#include "direct_minimum_literal.inl"
struct PatternCallResolution {
	PatternDefinition *definition;
	size_t pathIndex;
	std::vector<DataType> argumentTypes;
	std::vector<TypeConstraint> argumentConstraints;
};

static bool inferPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack, bool preserveCurrentGrouping,
	const InferenceExecutionSnapshot *argumentEffects = nullptr
);

static std::optional<ResolvedPatternConstraint> resolveProvisionalPatternConstraint(
	InferenceContext &context, PatternDefinition *definition, size_t pathIndex, size_t argumentIndex,
	const std::vector<DataType> &argumentTypes, const std::vector<CompileTimeValue> &argumentValues
);

static std::optional<ResolvedPatternConstraint> resolvePatternConstraintForInference(
	InferenceContext &context, PatternDefinition *definition, size_t pathIndex, size_t argumentIndex,
	const std::vector<DataType> &argumentTypes, const std::vector<CompileTimeValue> &argumentValues
) {
	if (context.unresolvedPatternConstraintSignal) {
		std::optional<ResolvedPatternConstraint> constraint =
			resolveProvisionalPatternConstraint(context, definition, pathIndex, argumentIndex, argumentTypes, argumentValues);
		return constraint.value_or(ResolvedPatternConstraint{TypeConstraint::any(), false, true, true, false});
	}
	return resolveCompiledPatternConstraint(definition, pathIndex, argumentIndex, argumentTypes, argumentValues);
}

#include "conversion_inference.inl"

#include "data_section_inference.inl"

struct ImplicitPatternOverload {
	PatternDefinition *definition{};
	size_t pathIndex{};
	std::vector<std::optional<DataType>> conversionTargets;
	// Literals typed by a number rule that take the explicitly typed parameter's type instead.
	std::vector<std::optional<NumericLiteralTyping>> literalTypings;
	std::vector<bool> conversionRequiresCompileTime;
	std::vector<TypeConstraint> constraints;
	size_t conversionCount = 0;
};

struct DeferredConversionAmbiguity {
	Expression *source{};
	std::string targetDescription;
	std::vector<ConversionCandidate> candidates;
};

// A literal whose type came from a number rule yields to a parameter that explicitly requires another number type.
static std::optional<NumericLiteralTyping>
parameterLiteralTyping(Expression *argument, const TypeConstraint &parameterConstraint, InferenceContext &context) {
	std::optional<NumericLiteralValue> value = argument ? numericLiteralValueOf(*argument) : std::nullopt;
	if (!value || !typeNumericLiteral(context.parseContext, argument->range.line, *value).ruleTyped)
		return std::nullopt;
	return numericLiteralTypingForParameter(context.parseContext, *value, parameterConstraint);
}

static std::optional<ImplicitPatternOverload> selectOverloadUsingImplicitConversions(
	const std::vector<PatternDefinition *> &definitions, const std::vector<PatternTreeNode *> &nodesPassed,
	const std::vector<Expression *> &arguments, const std::vector<DataType> &argumentTypes,
	const std::vector<bool> &argumentCompileTimeKnown, const std::vector<CompileTimeValue> &argumentValues,
	InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	std::vector<ImplicitPatternOverload> candidates;
	std::optional<DeferredConversionAmbiguity> deferredAmbiguity;
	bool hasDeferredCandidate = false;
	for (PatternDefinition *definition : definitions) {
		for (size_t pathIndex : matchingPatternPathIndices(nodesPassed, definition)) {
			ImplicitPatternOverload candidate;
			candidate.definition = definition;
			candidate.pathIndex = pathIndex;
			candidate.conversionTargets.resize(arguments.size());
			candidate.literalTypings.resize(arguments.size());
			candidate.conversionRequiresCompileTime.resize(arguments.size());
			bool possible = true;
			bool constraintDeferred = false;
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
				constraintDeferred = constraintDeferred || !resolvedConstraint->complete;
				candidate.constraints.push_back(parameterConstraint);
				const DataType &argumentType = argumentTypes[argumentIndex];
				bool compileTimeKnown =
					argumentIndex < argumentCompileTimeKnown.size() && argumentCompileTimeKnown[argumentIndex];
				if (resolvedConstraint->accepts(argumentType, compileTimeKnown)) {
					argumentIndex++;
					return;
				}
				if (std::optional<NumericLiteralTyping> literalTyping =
						parameterLiteralTyping(arguments[argumentIndex], parameterConstraint, context)) {
					candidate.literalTypings[argumentIndex] = std::move(literalTyping);
					candidate.conversionCount++;
					argumentIndex++;
					return;
				}
				ConversionLookup lookup =
					findUserConversion(arguments[argumentIndex], parameterConstraint, true, context, bindingFrameStack);
				if (lookup.deferred) {
					constraintDeferred = true;
					argumentIndex++;
					return;
				}
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
			if (possible && constraintDeferred) {
				hasDeferredCandidate = true;
			} else if (possible && candidateAmbiguity) {
				if (!deferredAmbiguity)
					deferredAmbiguity = std::move(candidateAmbiguity);
			} else if (possible && candidate.conversionCount > 0) {
				candidates.push_back(std::move(candidate));
			}
		}
	}
	if (hasDeferredCandidate) {
		requireCompilerInvariant(
			context.unresolvedPatternConstraintSignal != nullptr, "deferred implicit overload outside signature inference"
		);
		*context.unresolvedPatternConstraintSignal = true;
		context.typesValid = false;
		return std::nullopt;
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

// Overload selection completes before recursive callee inference begins.
[[gnu::noinline]] static std::optional<PatternCallResolution> resolvePatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack, bool preserveCurrentGrouping
) {
	expr->selectedPatternDefinition = nullptr;
	expr->selectedPatternPathIndex = std::nullopt;
	expr->selectedInstantiation = nullptr;
	auto &defs = expr->patternMatch->matchingDefinitions;
	if (!context.unresolvedPatternConstraintSignal && definitionsHaveUnresolvedTypeConstraints(defs))
		crashCompilerBug("normal type inference encountered an unresolved pattern type constraint");

	// Build argument types for overload selection.
	// Arguments are sorted by source position and include captured variables.
	std::vector<DataType> argTypesForOverload;
	std::vector<bool> argCompileTimeKnown;
	std::vector<CompileTimeValue> argCompileTimeValues;
	bool hasUnresolvedArgument = false;
	bool unresolvedArgumentsAreRecursive = true;
	argCompileTimeKnown.reserve(expr->arguments.size());
	argCompileTimeValues.reserve(expr->arguments.size());
	for (size_t ai = 0; ai < expr->arguments.size(); ai++) {
		Expression *inferArg = expr->arguments[ai];
		ScopedRecursiveInferenceObservation argumentObservation(context, context.currentInstantiation);
		DataType argumentType =
			requestKnownOrInferExpressionType(inferArg, context, flexBindingFrameStack, preserveCurrentGrouping);
		expr->arguments[ai] = inferArg;
		if (!context.typesValid)
			return std::nullopt;
		if (!argumentType.isDeduced()) {
			hasUnresolvedArgument = true;
			unresolvedArgumentsAreRecursive = unresolvedArgumentsAreRecursive && argumentObservation.observed();
		}
		argTypesForOverload.push_back(argumentType);
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
	if (overload.deferred) {
		requireCompilerInvariant(
			context.unresolvedPatternConstraintSignal != nullptr, "deferred overload outside signature inference"
		);
		*context.unresolvedPatternConstraintSignal = true;
		context.typesValid = false;
		return std::nullopt;
	}
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
		// Flex overloads may intentionally accept unresolved operands. Otherwise,
		// retry after the owning recursive body pass supplies the argument types.
		if (hasUnresolvedArgument && unresolvedArgumentsAreRecursive)
			return std::nullopt;
		std::optional<ImplicitPatternOverload> convertedOverload = selectOverloadUsingImplicitConversions(
			defs, expr->patternMatch->nodesPassed, expr->arguments, argTypesForOverload, argCompileTimeKnown,
			argCompileTimeValues, context, flexBindingFrameStack
		);
		if (convertedOverload) {
			for (size_t argumentIndex = 0; argumentIndex < convertedOverload->conversionTargets.size(); argumentIndex++) {
				if (const std::optional<NumericLiteralTyping> &literalTyping =
						convertedOverload->literalTypings[argumentIndex]) {
					Expression *literal = expr->arguments[argumentIndex];
					literal->type = literalTyping->type;
					context.setExpressionValue(literal, literalTyping->value);
					argTypesForOverload[argumentIndex] = literalTyping->type;
					argCompileTimeKnown[argumentIndex] = true;
					continue;
				}
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

[[gnu::noinline]] static void inferClassPatternCall(
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

// A number type phrase written in a scope with a number rule gets the rule's properties where it leaves them open.
static void applyNumberRuleToTypePhrase(Expression *expr, InferenceContext &context) {
	if (!expr->type.isMetaType() || !expr->range.line)
		return;
	std::optional<EffectiveNumberRule> rule = effectiveNumberRule(context.parseContext, expr->range.line);
	if (!rule)
		return;
	std::optional<TypeConstraint> constraint = getCompileTimeConstraintValue(context.lookupExpressionValue(expr));
	if (!constraint || !constraint->isResolved())
		return;
	std::vector<const ScopeRuleSource *> contradictions;
	TypeConstraint adjusted = applyNumberRule(*constraint, *rule, &contradictions);
	// Recorded during trials too: whether a phrase contradicts a rule does not depend on how its line is grouped.
	for (const ScopeRuleSource *source : contradictions) {
		context.parseContext.scopeRuleWarnings.push_back(
			{"number rule contradicted",
			 expr->range,
			 source->range,
			 {{"explicit", std::string(expr->range.subString)}, {"rule", std::string(source->range.subString)}}}
		);
	}
	if (adjusted == *constraint)
		return;
	if (std::optional<TypeReferenceValue> typeValue =
			TypeReferenceValue::forConstraint(adjusted, context.parseContext.options.emitSPIRV)) {
		expr->type = typeValue->type;
		context.setExpressionValue(expr, *typeValue);
		return;
	}
	expr->type = {DataType::Kind::Constraint};
	context.setExpressionValue(expr, adjusted);
}

static void pushResolvedFlexBindingScope(
	BindingFrameStack &bindingFrameStack, Expression *expression, const PatternCallResolution &resolution
) {
	PatternDefinition *definition = resolution.definition;
	requireCompilerInvariant(
		expression && definition && resolution.argumentConstraints.size() == expression->arguments.size(),
		"resolved flex bindings and constraints diverged"
	);
	BindingFrame frame = patternDeclarationBindingFrame(definition);
	size_t argumentIndex = 0;
	forEachPatternParameterName(
		definition, resolution.pathIndex,
		[&](const std::string &name, PatternTreeNode *, size_t startPos) {
		requireCompilerInvariant(argumentIndex < expression->arguments.size(), "flex call has too few arguments");
		Expression *argument = expression->arguments[argumentIndex];
		frame.bindings[name] = argument;
		VariableReference *parameterDefinition = findPatternParameterDefinition(definition, name);
		requireCompilerInvariant(parameterDefinition, "flex parameter has no definition identity");
		frame.parameterBindings[parameterDefinition] = argument;

		const DefinitionPatternElement *element = matchedPatternParameterElement(definition, name, startPos);
		requireCompilerInvariant(element, "flex parameter has no definition element");
		std::string sourceConstraintName = element->typeConstraintName;
		Range sourceRange = sourceConstraintName.empty() ? Range{} : patternElementTypeConstraintRange(*definition, *element);
		auto parameter = definition->section->variables.find(name);
		if (parameter != definition->section->variables.end() && parameter->second) {
			for (VariableReference *reference : parameter->second->declaredTypeConstraintReferences) {
				if (!reference || reference->declaredTypeConstraintName.empty())
					continue;
				bool earlier = !sourceRange.line || std::pair(
														reference->declaredTypeConstraintRange.line->mergedLineIndex,
														reference->declaredTypeConstraintRange.start()
													) < std::pair(sourceRange.line->mergedLineIndex, sourceRange.start());
				if (earlier) {
					sourceConstraintName = reference->declaredTypeConstraintName;
					sourceRange = reference->declaredTypeConstraintRange;
				}
			}
		}
		if (!sourceConstraintName.empty()) {
			frame.parameterConstraints.emplace(
				parameterDefinition,
				BindingFrame::ParameterConstraint{
					resolution.argumentConstraints[argumentIndex], name, std::move(sourceConstraintName), sourceRange
				}
			);
		}
		argumentIndex++;
	}
	);
	requireCompilerInvariant(argumentIndex == expression->arguments.size(), "flex call has too many arguments");
	pushBindingScope(bindingFrameStack, std::move(frame));
}

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
	[[gnu::noinline]] bool enter(Expression *expr, const PatternCallResolution &resolution);
};

bool FlexInferenceScope::enter(Expression *expr, const PatternCallResolution &resolution) {
	PatternDefinition *def = resolution.definition;
	Section *matchedSection = def->section;
	const std::vector<DataType> &argumentTypes = resolution.argumentTypes;
	FlexExpansionKey expansionKey;
	expansionKey.definition = def;
	expansionKey.pathIndex = resolution.pathIndex;
	expansionKey.callSite = expr->range;
	expansionKey.argumentTypes = argumentTypes;
	expansionKey.compileTimeArguments.reserve(expr->arguments.size());
	for (size_t argumentIndex = 0; argumentIndex < expr->arguments.size(); argumentIndex++) {
		CompileTimeValue value = context.lookupExpressionValue(expr->arguments[argumentIndex]);
		if (!isCompileTimeKnown(value) && argumentTypes[argumentIndex].kind == DataType::Kind::Type)
			value = TypeReferenceValue::exact(argumentTypes[argumentIndex]);
		expansionKey.compileTimeArguments.push_back(std::move(value));
	}
	Section *callSiteSection = expr->range.line ? expr->range.line->section : nullptr;
	if (std::ranges::any_of(context.activeFlexExpansionKeys, [&](const std::optional<FlexExpansionKey> &activeKey) {
		return activeKey && (*activeKey == expansionKey || (activeKey->sameDefinitionAndArguments(expansionKey) &&
															sectionIsDescendantOrSame(callSiteSection, matchedSection)));
	})) {
		context.setTypeFailure("recursive flex expansion");
		return false;
	}
	context.activeFlexDefinitionStack.push_back(matchedSection);
	context.activeFlexExpansionKeys.push_back(std::move(expansionKey));
	context.activeFlexCallStack.push_back(expr);
	if (callSiteSection)
		context.flexCallSiteSectionStack.push_back(callSiteSection);
	return true;
}

[[gnu::noinline]] static void inferFlexPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack,
	const PatternCallResolution &resolution
) {
	PatternDefinition *def = resolution.definition;
	Section *matchedSection = def->section;
	FlexInferenceScope flexInferenceScope{
		context,
		context.activeFlexDefinitionStack.size(),
		context.activeFlexExpansionKeys.size(),
		context.activeFlexCallStack.size(),
		context.flexCallSiteSectionStack.size(),
		context.sectionFlexBodyFrames.size()
	};
	if (!flexInferenceScope.enter(expr, resolution))
		return;
	std::optional<size_t> sectionBodyFrameIndex;
	if (matchedSection->type == SectionType::Section && !matchedSection->dataSection) {
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
	pushResolvedFlexBindingScope(callBindingFrameStack, expr, resolution);
	ScopedSectionLocalVariableState calleeVariableState(matchedSection);
	resetSectionLocalVariableTypes(matchedSection);
	if (matchedSection->dataSection && !inferDataSectionFields(expr, matchedSection, context, callBindingFrameStack))
		return;
	std::shared_ptr<InstantiatedSectionBody> flexBody =
		context.cloneSectionBody(matchedSection, expr->owningSectionBody, context.parseContext.storageScopeForInvocation(expr));
	for (const auto &[storage, value] : context.currentVariableValues.read()) {
		(void)value;
		if (storage.scope == flexBody->storageScope)
			context.currentVariableValues.write()[storage] = {};
	}
	for (const auto &[storage, provenance] : context.currentAddressState.read().variables) {
		(void)provenance;
		if (storage.scope == flexBody->storageScope)
			context.currentAddressState.write().variables[storage] = {.mayTargets = {}, .unknown = true};
	}

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
	Expression *bodyExpr = flexBody->expressionForLine(templateBodyExpression->range.line);
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
		outcomeExpression = templateOutcomeExpression
								? expr->inferredFlexBody->expressionForLine(templateOutcomeExpression->range.line)
								: nullptr;
	}
	// A nested flex may transfer control through this active call while
	// its replacement is being inferred. That direct outcome takes
	// precedence over the replacement expression's forwarded outcome.
	if (expr->sectionOutcome.read().kind == Expression::SectionOutcome::Kind::None)
		expr->sectionOutcome =
			outcomeExpression ? outcomeExpression->sectionOutcome : CopyOnWrite<Expression::SectionOutcome>{};
	if (!flexFallsThrough && expr->sectionOutcome.read().kind == Expression::SectionOutcome::Kind::None)
		expr->sectionOutcome.write().kind = Expression::SectionOutcome::Kind::FunctionReturn;
	if (sectionBodyFrameIndex) {
		size_t frameIndex = *sectionBodyFrameIndex;
		if (!context.sectionFlexBodyFrames[frameIndex].bodyInferred &&
			expr->sectionOutcome.read().kind == Expression::SectionOutcome::Kind::None) {
			Section *executionSection = bodyExpr->range.line ? bodyExpr->range.line->section : matchedSection;
			if (!inferSectionFlexCallerBodyFrame(frameIndex, executionSection, expr, context)) {
				return;
			}
		}
		const auto &bodyFrame = context.sectionFlexBodyFrames[frameIndex];
		expr->sectionBodyExecution = Expression::SectionBodyExecution::None;
		if (bodyFrame.bodyInferred) {
			requireCompilerInvariant(
				bodyFrame.executeBodyCallSite != nullptr, "inferred section flex body has no execution site"
			);
			expr->sectionBodyExecution = bodyFrame.executeBodyCallSite == expr ? Expression::SectionBodyExecution::Implicit
																			   : Expression::SectionBodyExecution::Explicit;
		}
		expr->sectionBodyFallsThrough = context.sectionFlexBodyFrames[frameIndex].bodyFallsThrough;
	}
	DataType resolvedType = matchedSection->type == SectionType::Section && !matchedSection->dataSection
								? DataType{DataType::Kind::Void}
								: bodyExpr->type;
	if (matchedSection->dataSection &&
		!validateDataSectionResult(expr, matchedSection, resolvedType, context, callBindingFrameStack))
		return;
	if (resolvedType.isDeduced()) {
		expr->type = resolvedType;
		if (!validateDefinitionReturnContract(matchedSection, def, resolvedType, context))
			return;
	}
	context.setExpressionValue(expr, context.lookupExpressionValue(bodyExpr));
	if (expr->dataDeclaration) {
		Variable *variable = expr->dataDeclaration->range.section()->findVariable(expr->dataDeclaration->variable);
		requireCompilerInvariant(variable, "data declaration has no bound variable");
		if (context.trial && context.trialJournal)
			context.trialJournal->recordVariableWrite(variable);
		commitVariableTypeFromValue(variable, expr, resolvedType);
		expr->dataDeclaration->type = resolvedType;
		context.setKnownConstant(variable->definition, context.lookupExpressionValue(expr));
	}
	applyNumberRuleToTypePhrase(expr, context);
}

#include "non_flex_pattern_inference.inl"

static bool inferPatternCall(
	Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack, bool preserveCurrentGrouping,
	const InferenceExecutionSnapshot *argumentEffects
) {
	std::optional<PatternCallResolution> resolution =
		resolvePatternCall(expr, context, flexBindingFrameStack, preserveCurrentGrouping);
	if (!resolution)
		return true;
	Section *matchedSection = resolution->definition->section;
	if (!matchedSection->isFlex && std::ranges::any_of(expr->arguments, [](Expression *argument) {
		return argument->type.kind == DataType::Kind::ArgumentPack || argument->type.kind == DataType::Kind::ArgumentReference;
	})) {
		context.fail(Diagnostic(
			context.parseContext, Diagnostic::Level::Error, "argument packs require replacement definitions", expr->range
		));
		return false;
	}
	if (matchedSection->type == SectionType::Class && !matchedSection->isFlex) {
		inferClassPatternCall(expr, context, flexBindingFrameStack, *resolution);
	} else if (matchedSection->isFlex) {
		if (argumentEffects)
			argumentEffects->restore(context);
		inferFlexPatternCall(expr, context, flexBindingFrameStack, *resolution);
	} else {
		return inferNonFlexPatternCall(expr, context, flexBindingFrameStack, *resolution);
	}
	return true;
}
