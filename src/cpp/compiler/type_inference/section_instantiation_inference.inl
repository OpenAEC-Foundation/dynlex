#pragma once

bool ensureSectionInstantiationInferred(
	ParseContext &parseContext, Section *section, PatternDefinition *definition,
	const std::vector<std::pair<std::string, Expression *>> &paramBindings, const std::vector<DataType> &argTypes,
	const std::unordered_set<std::string> &explicitCompileTimeParameters, const Instantiation *callerInstantiation,
	InferenceContext *callerContext
) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	if (!section)
		return false;
	requireCompilerInvariant(
		!callerInstantiation || (callerContext && callerContext->currentInstantiation == callerInstantiation),
		"section instantiation caller owner does not match its inference context"
	);
	auto evaluateParameterValue = [&](Expression *argumentExpression) {
		if (!argumentExpression)
			crashCompilerBug("missing section parameter expression while building instantiation key");
		return callerContext ? callerContext->lookupExpressionValue(argumentExpression)
							 : getExpressionCompileTimeValue(argumentExpression);
	};
	InstantiationKey instantiationKey =
		findMatchingInstantiationKey(section, paramBindings, argTypes, evaluateParameterValue)
			.value_or(buildInstantiationKey(explicitCompileTimeParameters, paramBindings, argTypes, evaluateParameterValue));
	if (callerContext && callerContext->trial) {
		if (!callerContext->trialJournal)
			crashCompilerBug("trial section-instantiation inference started without an active trial journal");
		callerContext->trialJournal->recordSectionInstantiationWrite(section, instantiationKey);
	}
	auto instIt = section->instantiations.find(instantiationKey);
	if (instIt == section->instantiations.end())
		instIt = section->instantiations.emplace(instantiationKey, Instantiation{}).first;
	Instantiation &inst = instIt->second;
	inst.invokedDefinition = definition;
	auto storageScope = parseContext.storageScopeForInstantiation(section, instantiationKey);
	inst.storageScope = storageScope;
	if (callerContext)
		callerContext->recordBodyRead(*section, storageScope);
	if (inst.argumentTypes.empty())
		inst.argumentTypes = argTypes;
	else
		requireCompilerInvariant(inst.argumentTypes == argTypes, "Instantiation argumentTypes diverged from map key");
	if (inst.inferring) {
		requireCompilerInvariant(callerContext, "recursive section inference requires its caller context");
		// A reference needs the callable's signature, not another traversal
		// of its active body. Retain its identity while the owning body pass
		// resolves the result, just as for an unresolved recursive call.
		if (!inst.returnType.isDeduced())
			observeUnresolvedRecursiveDependency(*callerContext);
		return true;
	}
	inst.requiredCompileTimeParameters = explicitCompileTimeParameters;
	seedInstantiationParameterTypes(inst, paramBindings, argTypes);
	seedInstantiationCompileTimeParameters(
		inst, paramBindings, argTypes, inst.requiredCompileTimeParameters, evaluateParameterValue
	);
	bool unavailable = parseContext.inferenceProducts && !inst.inferring &&
					   !inferenceInstanceAvailable(parseContext, inst.storageScope, callerContext);
	if ((!inst.returnType.isDeduced() || (inst.inferenceReusedFromCache && unavailable)) &&
		parseContext.reuseInferredInstantiation) {
		bool prepared = parseContext.reuseInferredInstantiation(
			section, instantiationKey, inst, ParseContext::InferenceReuseUse::Callable, callerContext, nullptr
		);
		if (!prepared && unavailable) {
			inst.failure.reset();
			inst.needsReinfer = true;
		}
	}
	if (inst.inferenceReusedFromCache && parseContext.reuseInferredInstantiation)
		parseContext.reuseInferredInstantiation(
			section, instantiationKey, inst, ParseContext::InferenceReuseUse::Callable, callerContext, nullptr
		);
	if (parseContext.inferenceProducts && inst.valid && inst.returnType.isDeduced() && !inst.inferring &&
		!supportsGroupingQuery(inst, callerContext))
		inst.needsReinfer = true;
	if (inst.valid && inst.returnType.isDeduced() && !inst.needsReinfer &&
		(!parseContext.inferenceProducts || inferenceInstanceAvailable(parseContext, inst.storageScope, callerContext))) {
		if (callerContext)
			observeGroupingResult(inst, *callerContext);
		InferenceContext publication(parseContext, callerContext && callerContext->trial);
		if (callerContext)
			publication.inheritDiagnosticContext(*callerContext);
		publishDeferredInferenceDiagnostics(publication, inst);
		return inst.valid;
	}
	if (inst.failure) {
		if (parseContext.inferenceProducts && callerContext && !inst.groupingProduct->providedInstances.empty())
			recordInferenceInstance(*callerContext, inst.storageScope, false);
		if (callerContext)
			replayInferenceFailure(*callerContext, *inst.failure);
		return false;
	}

	ScopedSectionLocalVariableState calleeVariableState(parseContext, section);
	InferenceContext context(parseContext, callerContext && callerContext->trial);
	struct RetainTrialProducts {
		InferenceContext &child;
		InferenceContext *caller;
		~RetainTrialProducts() {
			if (caller)
				caller->groupingTrace = child.groupingTrace;
			if (child.trial)
				caller->absorbTrialResultsFrom(child);
		}
	} retainTrialProducts{context, callerContext};
	if (callerContext) {
		context.inheritDiagnosticContext(*callerContext);
		context.inheritedGroupingContext = callerContext;
		context.groupingTrace = callerContext->groupingTrace;
		context.dependencyReads = callerContext->dependencyReads;
		// Cached instantiations cannot consume the caller's tracked constants.
		// Only globals and instantiation-key parameter constants enter a pass.
		context.inheritedTrialContext = callerContext->trial ? callerContext : callerContext->inheritedTrialContext;
		context.trialJournal = callerContext->trialJournal;
		context.unresolvedPatternConstraintSignal = callerContext->unresolvedPatternConstraintSignal;
	}
	Instantiation *savedInst = context.currentInstantiation;
	int failureBase = static_cast<int>(context.diagnosticTraceDepth());
	Range fallbackRange = definition			 ? definition->range
						  : section->openingLine ? Range(section->openingLine, section->openingLine->patternText)
												 : Range{};
	std::string functionName = definition ? (std::string)definition->range.subString : "";
	bool instantiationFallsThrough = true;
	bool inferenceSucceeded = runInstantiationReinferenceLoop(
		context, inst, definition, fallbackRange, std::move(functionName), callerInstantiation != nullptr, [&]() {
		seedInstantiationParameterTypes(inst, paramBindings, argTypes);
		beginParameterOutputReinference(inst);
		inst.accessedGlobalReferences.clear();
		inst.writtenGlobalReferences.clear();
		inst.finalGlobalConstantValues.clear();
		inst.finalGlobalAddressProvenance.clear();
		inst.addressTakenGlobalReferences.clear();
		inst.externallyEscapedGlobalProvenance = {};
		inst.returnAddressProvenance = {};
		inst.memoryViolation.reset();
		inst.hasReturnAddressProvenance = false;
		inst.writesThroughUnknownAddress = false;
		inst.externallyEscapesUnknownAddress = false;
		inst.requiredCompileTimeParameters = explicitCompileTimeParameters;
		inst.purity = InstantiationPurity::Pure;
		inst.pureReturnValuesByArguments.clear();
		seedInstantiationCompileTimeParameters(
			inst, paramBindings, argTypes, inst.requiredCompileTimeParameters, evaluateParameterValue
		);
		inst.needsReinfer = false;
		inst.inferring = true;
		context.currentInstantiation = &inst;
		context.currentVariableValues.clear();
		context.currentAddressState = {};
		context.currentSubject = {};
		if (context.trial || !inst.body)
			inst.body = context.cloneSectionBody(section, nullptr, storageScope);
		parseContext.functionBodyInferencePasses++;
		bool passSucceeded = inferSection(section, inst.body.get(), nullptr, context, {}, &instantiationFallsThrough);
		finishParameterOutputReinference(context, inst, passSucceeded);
		inst.fallsThrough = instantiationFallsThrough;
		inst.finalGlobalConstantValues.clear();
		for (VariableReference *reference : inst.writtenGlobalReferences) {
			const KnownConstantStorage &knownConstants = context.currentVariableValues.read();
			auto knownIt = knownConstants.find({reference, nullptr});
			if (knownIt != knownConstants.end() && isCompileTimeKnown(knownIt->second))
				inst.finalGlobalConstantValues[reference] = knownIt->second;
			const VariableAddressProvenance &variables = context.currentAddressState.read().variables;
			auto provenance = variables.find({reference, nullptr});
			inst.finalGlobalAddressProvenance[reference] =
				provenance != variables.end() ? provenance->second : AddressProvenance{.mayTargets = {}, .unknown = true};
		}
		for (const VariableStorageKey &reference : context.currentAddressState.read().addressTakenVariables) {
			Variable *variable = variableForAddressTarget(reference);
			if (variable && variable->isGlobal)
				inst.addressTakenGlobalReferences.insert(reference.definition);
		}
		for (const VariableStorageKey &reference : context.currentAddressState.read().externallyEscaped.mayTargets) {
			Variable *variable = variableForAddressTarget(reference);
			if (variable && variable->isGlobal)
				inst.externallyEscapedGlobalProvenance.mayTargets.insert(reference);
		}
		return passSucceeded;
	}
	);
	context.currentInstantiation = savedInst;
	inst.inferring = false;
	inst.valid = inferenceSucceeded && context.typesValid;
	if (inst.valid && !inst.needsReinfer)
		inst.valid = validateLocalVariableTypes(section, *inst.body, context);
	if (!inst.valid && !inst.needsReinfer)
		inst.failure = captureInferenceFailure(context, failureBase);
	if (callerContext) {
		if (inst.valid && inst.needsReinfer)
			propagateUnresolvedRecursiveDependencyToCaller(*callerContext, callerInstantiation);
		callerContext->inheritTypeFailureFrom(context);
		if (!inst.valid)
			callerContext->typesValid = false;
	}
	if (!inst.valid) {
		noteInferenceProduct(inst, context);
		return false;
	}

	inst.bodyInferred = !inst.needsReinfer;
	InstantiationKey refinedKey =
		buildInstantiationKey(inst.requiredCompileTimeParameters, paramBindings, argTypes, evaluateParameterValue);
	if (refinedKey != instIt->first) {
		retargetTrialSectionInstantiationWriteOrCrash(
			context, section, instantiationKey, refinedKey, "trial section-instantiation inference"
		);
		auto node = section->instantiations.extract(instIt);
		node.key() = refinedKey;
		auto insertResult = section->instantiations.insert(std::move(node));
		requireCompilerInvariant(insertResult.inserted, "Refined instantiation key collided with existing entry");
		parseContext.refineInstantiationStorageScope(section, instantiationKey, refinedKey);
	}
	// The body and its trial metadata must survive together. A child context
	// owns constants, groupings and callable selections until its caller commits.
	noteInferenceProduct(inst, context);
	return inst.returnType.isDeduced();
}
