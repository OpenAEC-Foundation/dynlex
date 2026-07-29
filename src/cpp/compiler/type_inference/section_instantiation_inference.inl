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
	if (!inst.body)
		inst.body = parseContext.cloneSectionBody(section);
	if (inst.argumentTypes.empty())
		inst.argumentTypes = argTypes;
	else
		requireCompilerInvariant(inst.argumentTypes == argTypes, "Instantiation argumentTypes diverged from map key");
	inst.requiredCompileTimeParameters = explicitCompileTimeParameters;
	seedInstantiationParameterTypes(inst, paramBindings, argTypes);
	seedInstantiationCompileTimeParameters(
		inst, paramBindings, argTypes, inst.requiredCompileTimeParameters, evaluateParameterValue
	);
	if (inst.returnType.isDeduced() && !inst.needsReinfer)
		return inst.valid;
	if (inst.inferring)
		return inst.returnType.isDeduced() && inst.valid;

	ScopedSectionLocalVariableState calleeVariableState(section);
	InferenceContext context(parseContext, callerContext && callerContext->trial);
	if (callerContext) {
		// Cached instantiations cannot consume the caller's tracked constants.
		// Only globals and instantiation-key parameter constants enter a pass.
		context.inheritedTrialExpressionValues =
			callerContext->trial ? &callerContext->trialExpressionValues : callerContext->inheritedTrialExpressionValues;
		context.trialJournal = callerContext->trialJournal;
		context.unresolvedPatternConstraintSignal = callerContext->unresolvedPatternConstraintSignal;
		context.suppressDiagnostics = callerContext->suppressDiagnostics;
		context.suppressReinferPassDiagnostics = callerContext->suppressReinferPassDiagnostics;
	}
	Instantiation *savedInst = context.currentInstantiation;
	Range fallbackRange = definition			 ? definition->range
						  : section->openingLine ? Range(section->openingLine, section->openingLine->patternText)
												 : Range{};
	std::string functionName = definition ? (std::string)definition->range.subString : "";
	bool instantiationFallsThrough = true;
	bool inferenceSucceeded = runInstantiationReinferenceLoop(
		context, inst, definition, fallbackRange, std::move(functionName), callerInstantiation != nullptr,
		[&]() {
		seedInstantiationParameterTypes(inst, paramBindings, argTypes);
		inst.writtenGlobalReferences.clear();
		inst.finalGlobalConstantValues.clear();
		inst.finalGlobalAddressProvenance.clear();
		inst.addressTakenGlobalReferences.clear();
		inst.externallyEscapedGlobalProvenance = {};
		inst.returnAddressProvenance = {};
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
		for (const auto &[name, value] : inst.constantParameterValues) {
			Variable *var = findOwnSectionVariable(section, name);
			if (var)
				context.setKnownConstant(var->definition, value);
		}
		if (context.trial)
			inst.body = parseContext.cloneSectionBody(section);
		bool passSucceeded = inferSection(section, inst.body.get(), nullptr, context, {}, &instantiationFallsThrough);
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
		return passSucceeded;
	}
	);
	context.currentInstantiation = savedInst;
	inst.inferring = false;
	inst.valid = inferenceSucceeded;
	if (callerContext) {
		if (inferenceSucceeded && inst.needsReinfer)
			propagateUnresolvedRecursiveDependencyToCaller(*callerContext, callerInstantiation);
		callerContext->inheritTypeFailureFrom(context);
		if (!inferenceSucceeded || !context.typesValid)
			callerContext->typesValid = false;
	}
	if (!inst.valid || !context.typesValid)
		return false;

	bool unresolvedLocalFound = false;
	std::function<void(Section *)> validateInstantiatedVariables = [&](Section *currentSection) {
		if (unresolvedLocalFound || !currentSection)
			return;
		for (auto &[name, var] : currentSection->variables) {
			if (var->type.isDeduced())
				continue;
			if (!context.suppressReinferPassDiagnostics) {
				context.addDiagnostic(Diagnostic(
					parseContext, Diagnostic::Level::Error, "variable has no type", var->definition->range, "name", name
				));
			}
			unresolvedLocalFound = true;
			return;
		}
		for (Section *child : currentSection->children)
			validateInstantiatedVariables(child);
	};
	validateInstantiatedVariables(section);
	if (unresolvedLocalFound) {
		inst.valid = false;
		return false;
	}
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
	}
	if (!inst.needsReinfer && inst.returnType.kind == DataType::Kind::Any)
		inst.returnType = {DataType::Kind::Void};
	return inst.returnType.isDeduced();
}
