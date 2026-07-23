case Expression::Kind::PatternCall: {
	expr->selectedPatternDefinition = nullptr;
	expr->selectedInstantiation = nullptr;
	auto &defs = expr->patternMatch->matchingDefinitions;
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
		argCompileTimeKnown.push_back(argumentType.isMetaType() || isCompileTimeKnown(context.lookupExpressionValue(inferArg)));
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
				DefinitionPatternElement *parameterElement = findParameterElement(candidate->patternElements, parameterName);
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
		struct FlexInferenceScope {
			InferenceContext &context;
			size_t activeDefinitionCount;
			size_t activeCallCount;
			size_t callSiteCount;
			size_t bodyFrameCount;
			~FlexInferenceScope() {
				context.activeFlexDefinitionStack.resize(activeDefinitionCount);
				context.activeFlexCallStack.resize(activeCallCount);
				context.flexCallSiteSectionStack.resize(callSiteCount);
				context.sectionFlexBodyFrames.resize(bodyFrameCount);
			}
		} flexInferenceScope{
			context, context.activeFlexDefinitionStack.size(), context.activeFlexCallStack.size(),
			context.flexCallSiteSectionStack.size(), context.sectionFlexBodyFrames.size()
		};
		context.activeFlexDefinitionStack.push_back(matchedSection);
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
		BindingFrame callBindings;
		collectPatternCallBindings(expr, def, callBindings);
		materializeFlexBindingsInCallerScope(callBindings, flexBindingFrameStack);
		pushBindingScope(callBindingFrameStack, std::move(callBindings));
		if (matchedSection->inferring) {
			context.setTypeFailure("recursive flex expansion");
			break;
		}
		std::shared_ptr<InstantiatedSectionBody> flexBody = context.parseContext.cloneSectionBody(matchedSection);
		if (sectionBodyFrameIndex)
			context.sectionFlexBodyFrames[*sectionBodyFrameIndex].definitionBody = flexBody.get();
		matchedSection->inferring = true;
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
		Expression *outcomeExpression = bodyExpr;
		if (matchedSection->type == SectionType::Function) {
			Expression *templateOutcomeExpression = singleExpressionFlexFunctionOutcome(def);
			outcomeExpression =
				templateOutcomeExpression ? expr->inferredFlexBody->findCloneOf(templateOutcomeExpression) : nullptr;
		}
		expr->sectionOutcome = outcomeExpression ? outcomeExpression->sectionOutcome : Expression::SectionOutcome{};
		if (!flexFallsThrough && expr->sectionOutcome.kind == Expression::SectionOutcome::Kind::None)
			expr->sectionOutcome.kind = Expression::SectionOutcome::Kind::FunctionReturn;
		if (sectionBodyFrameIndex) {
			size_t frameIndex = *sectionBodyFrameIndex;
			if (!context.sectionFlexBodyFrames[frameIndex].bodyInferred &&
				expr->sectionOutcome.kind == Expression::SectionOutcome::Kind::None) {
				Section *executionSection = bodyExpr->range.line ? bodyExpr->range.line->section : matchedSection;
				if (!inferSectionFlexCallerBodyFrame(frameIndex, executionSection, expr, context)) {
					break;
				}
			}
			expr->sectionBodyInferred = context.sectionFlexBodyFrames[frameIndex].bodyInferred;
			expr->sectionBodyFallsThrough = context.sectionFlexBodyFrames[frameIndex].bodyFallsThrough;
		}
		DataType resolvedType = matchedSection->type == SectionType::Section ? DataType{DataType::Kind::Void} : bodyExpr->type;
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
					markInstantiationForReinference(context, context.currentInstantiation);
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
			InferenceContext::SubjectState callerSubject = context.currentSubject;
			bool instantiationFallsThrough = true;
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
				context.currentVariableValues.clear();
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
					auto knownIt = context.currentVariableValues.find(reference);
					if (knownIt != context.currentVariableValues.end() && isCompileTimeKnown(knownIt->second))
						inst.finalGlobalConstantValues[reference] = knownIt->second;
				}
				context.suppressReinferPassDiagnostics = savedReinferSuppression;
				inst.inferring = false;
				return passSucceeded;
			}
			);
			context.currentVariableValues = std::move(callerKnownConstants);
			context.currentSubject = callerSubject;
			context.currentInstantiation = savedInst;
			inst.valid = inferenceSucceeded;
			if (inst.needsReinfer)
				markInstantiationForReinference(context, savedInst);
			refinedInstantiationKey =
				buildInstantiationKey(inst.requiredCompileTimeParameters, paramBindings, argTypes, evaluateParameterValue);
		} else if (inst.returnType.isDeduced()) {
			expr->type = inst.returnType;
		} else {
			context.observedInProgressUndeducedInstantiation = true;
			markInstantiationForReinference(context, context.currentInstantiation);
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
			inferredReturnValue =
				evaluatePureFunctionCallReturnValue(expr, def, matchedSection, inst, context, flexBindingFrameStack);
			context.setExpressionValue(expr, inferredReturnValue);
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
