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
			inst.body = context.cloneSectionBody(matchedSection);
		Instantiation *savedInst = context.currentInstantiation;
		size_t callerMemoryFlexBase = context.memoryFlexBase;
		auto callerKnownConstants = context.currentVariableValues;
		auto callerAddressState = context.currentAddressState;
		InferenceContext::SubjectState callerSubject = context.currentSubject;
		bool instantiationFallsThrough = true;
		bool inferenceSucceeded = runInstantiationReinferenceLoop(
			context, inst, def, expr->range, (std::string)def->range.subString, savedInst != nullptr, [&]() -> bool {
			seedInstantiationParameterTypes(inst, paramBindings, argTypes);
			beginParameterOutputReinference(inst);
			inst.writtenGlobalReferences.clear();
			inst.finalGlobalConstantValues.clear();
			inst.finalGlobalAddressProvenance.clear();
			inst.addressTakenGlobalReferences.clear();
			inst.externallyEscapedGlobalProvenance = {};
			inst.returnAddressProvenance = {};
			inst.memoryViolation.reset();
			inst.hasReturnAddressProvenance = false;
			inst.returnPointerStorageParameterName.reset();
			inst.returnPointerStorageAmbiguous = false;
			inst.writesThroughUnknownAddress = false;
			inst.externallyEscapesUnknownAddress = false;
			inst.requiredCompileTimeParameters = explicitCompileTimeParameters;
			inst.purity = InstantiationPurity::Pure;
			inst.pureReturnValuesByArguments.clear();
			seedInstantiationCompileTimeParameters(
				inst, paramBindings, argTypes, inst.requiredCompileTimeParameters, [&](Expression *argumentExpression) {
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
			context.memoryFlexBase = context.activeFlexCallStack.size();
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
				inst.body = context.cloneSectionBody(matchedSection);
			bool passSucceeded =
				inferSection(matchedSection, inst.body.get(), nullptr, context, {}, &instantiationFallsThrough);
			finishParameterOutputReinference(context, inst, passSucceeded);
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
		context.memoryFlexBase = callerMemoryFlexBase;
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

	if (!validateDefinitionReturnContract(matchedSection, def, inst.returnType, context))
		return true;
	CompileTimeValue inferredReturnValue;
	if (inst.returnType.isDeduced()) {
		expr->type = inst.returnType;
		inferredReturnValue =
			evaluatePureFunctionCallReturnValue(expr, def, matchedSection, inst, context, flexBindingFrameStack);
		if (!context.typesValid)
			return true;
		refineDirectMinimumSignedLiteralType(expr, inferredReturnValue);
		context.setExpressionValue(expr, inferredReturnValue);
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
	context.setExpressionValue(expr, context.lookupExpressionValue(expr));
	return true;
}
