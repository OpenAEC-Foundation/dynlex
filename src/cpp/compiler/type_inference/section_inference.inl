#pragma once

#include "operand_reordering.inl"

static Variable *findOwnSectionVariable(Section *section, const std::string &name) {
	if (!section)
		return nullptr;
	auto it = section->variables.find(name);
	return it != section->variables.end() ? it->second : nullptr;
}

static bool isLoopSectionOpening(CodeLine *line, ParseContext &parseContext) {
	if (!line || !line->expression)
		return false;
	Expression *header = line->expression;
	if (header->kind == Expression::Kind::PatternCall) {
		BindingFrame innerBindings;
		Expression *expanded = expandFlexPatternCall(parseContext, header, innerBindings);
		if (expanded)
			header = expanded;
	}
	return header && header->kind == Expression::Kind::IntrinsicCall &&
		   intrinsicKind(header->intrinsicName) == IntrinsicKind::LoopWhile;
}

static bool inferSection(Section *section, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	// The first instantiation determines operand ordering; subsequent ones reuse it.
	// size() > 1 because the current instantiation is already inserted before inferSection is called.
	bool alreadyOrdered = section->instantiations.size() > 1;
	if (context.trial && context.trialJournal)
		context.trialJournal->recordTouchedSection(section);
	if (context.trial && context.trialJournal) {
		for (auto &[name, variable] : section->variables) {
			(void)name;
			if (!variable || variable->isGlobal)
				continue;
			context.trialJournal->recordVariableWrite(variable);
		}
	}
	resetSectionExpressionTypes(section);
	resetSectionLocalVariableTypes(section);
	for (auto &[name, boundVar] : section->variables) {
		if (!boundVar)
			continue;
		Expression *boundExpr = bindingFrameStack.lookup(boundVar->definition);
		if (!boundExpr)
			continue;
		Expression *boundExprForType = boundExpr;
		DataType boundType = ensureExpressionTypeWithCurrentGrouping(boundExprForType, context, bindingFrameStack);
		if (!boundType.isDeduced())
			continue;
		if (context.trial && context.trialJournal)
			context.trialJournal->recordVariableWrite(boundVar);
		commitVariableTypeFromValue(boundVar, boundExpr, boundType);
		CompileTimeValue boundValue = context.lookupExpressionValue(boundExpr);
		context.setKnownConstant(boundVar->definition, boundValue);
		context.snapshotReferenceConstant(boundVar->definition);
	}

	if (context.currentInstantiation) {
		for (const auto &[name, value] : context.currentInstantiation->constantParameterValues) {
			Variable *var = findOwnSectionVariable(section, name);
			if (var)
				context.setKnownConstant(var->definition, value);
		}
	}

	bool loopSection = isLoopSectionOpening(section->openingLine, context.parseContext);
	std::unordered_map<VariableReference *, CompileTimeValue> constantsAtLoopEntry;
	if (loopSection) {
		constantsAtLoopEntry = context.currentKnownConstants;
		context.pushLoopMutationScope();
	}
	struct LoopMutationScopeGuard {
		InferenceContext &context;
		bool active;
		explicit LoopMutationScopeGuard(InferenceContext &context, bool active) : context(context), active(active) {}
		std::unordered_set<VariableReference *> finish() {
			if (!active)
				return {};
			active = false;
			return context.popLoopMutationScope();
		}
		~LoopMutationScopeGuard() {
			if (active)
				context.popLoopMutationScope();
		}
	} loopMutationScope(context, loopSection);

	auto controlHeaderInfo = [&](CodeLine *line) -> std::optional<std::tuple<std::string, Expression *, BindingFrameStack>> {
		if (!line || !line->expression)
			return std::nullopt;

		Expression *header = line->expression;
		BindingFrameStack headerBindingFrameStack = bindingFrameStack;
		if (header->kind == Expression::Kind::PatternCall) {
			BindingFrame innerBindings;
			Expression *expanded = expandFlexPatternCall(context.parseContext, header, innerBindings);
			if (expanded) {
				header = expanded;
				materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
				headerBindingFrameStack.pushFrame(std::move(innerBindings));
			}
		}
		if (!header || header->kind != Expression::Kind::IntrinsicCall)
			return std::nullopt;
		if (header->intrinsicName != "if" && header->intrinsicName != "else if" && header->intrinsicName != "else")
			return std::nullopt;
		return std::make_optional(std::make_tuple(header->intrinsicName, header, std::move(headerBindingFrameStack)));
	};

	auto inferOpenedSection = [&](CodeLine *line) {
		if (!line || !line->sectionOpening || dynamic_cast<DefinitionSection *>(line->sectionOpening))
			return true;
		if (!inferSection(line->sectionOpening, context, bindingFrameStack)) {
			context.typesValid = false;
			return false;
		}
		return true;
	};

	for (size_t i = 0; i < section->codeLines.size(); i++) {
		CodeLine *line = section->codeLines[i];
		auto headerInfo = controlHeaderInfo(line);
		if (headerInfo && std::get<0>(*headerInfo) == "if") {
			auto constantsBeforeChain = context.currentKnownConstants;
			size_t chainEnd = i;
			while (chainEnd + 1 < section->codeLines.size()) {
				CodeLine *next = section->codeLines[chainEnd + 1];
				if (!next->sectionOpening || dynamic_cast<DefinitionSection *>(next->sectionOpening))
					break;
				auto nextInfo = controlHeaderInfo(next);
				if (!nextInfo)
					break;
				const std::string &nextKind = std::get<0>(*nextInfo);
				if (nextKind != "else if" && nextKind != "else")
					break;
				chainEnd++;
			}

			for (size_t k = i; k <= chainEnd; k++) {
				CodeLine *header = section->codeLines[k];
				if (!header->expression)
					continue;
				if (!inferExpression(header->expression, context, alreadyOrdered, bindingFrameStack)) {
					context.typesValid = false;
					return false;
				}
			}

			std::optional<size_t> selectedBranch;
			bool branchKnown = true;
			for (size_t k = i; k <= chainEnd; k++) {
				auto branchInfo = controlHeaderInfo(section->codeLines[k]);
				if (!branchInfo) {
					branchKnown = false;
					break;
				}
				const std::string &branchKind = std::get<0>(*branchInfo);
				Expression *header = std::get<1>(*branchInfo);
				const auto &headerBindingFrameStack = std::get<2>(*branchInfo);
				if (branchKind == "else") {
					if (!selectedBranch.has_value())
						selectedBranch = k;
					break;
				}
				if (header->arguments.size() < 2) {
					branchKnown = false;
					break;
				}
				if (context.currentInstantiation)
					markCompileTimeParameterRequirements(
						header->arguments[1], headerBindingFrameStack, context.currentInstantiation
					);
				CompileTimeValue conditionValue = context.lookupExpressionValue(header->arguments[1]);
				auto *condition = std::get_if<bool>(&conditionValue);
				if (!condition) {
					branchKnown = false;
					break;
				}
				if (*condition) {
					selectedBranch = k;
					break;
				}
			}

			if (!context.trial) {
				Instantiation::IfChainSelection selection;
				selection.known = branchKnown;
				if (branchKnown && selectedBranch.has_value())
					selection.selectedBranchLine = section->codeLines[*selectedBranch];
				if (context.currentInstantiation)
					context.currentInstantiation->ifChainSelections[line] = selection;
				else
					context.parseContext.inferredIfChainSelections[line] = selection;
			}

			if (branchKnown && selectedBranch.has_value()) {
				context.currentKnownConstants = constantsBeforeChain;
				if (!inferOpenedSection(section->codeLines[*selectedBranch]))
					return false;
			} else {
				std::vector<std::unordered_map<VariableReference *, CompileTimeValue>> branchStates;
				bool hasElseBranch = false;
				for (size_t k = i; k <= chainEnd; k++) {
					auto branchInfo = controlHeaderInfo(section->codeLines[k]);
					if (branchInfo && std::get<0>(*branchInfo) == "else")
						hasElseBranch = true;
					context.currentKnownConstants = constantsBeforeChain;
					if (!inferOpenedSection(section->codeLines[k]))
						return false;
					branchStates.push_back(context.currentKnownConstants);
				}
				if (!hasElseBranch)
					branchStates.push_back(constantsBeforeChain);
				std::unordered_map<VariableReference *, CompileTimeValue> mergedConstants =
					branchStates.empty() ? constantsBeforeChain : branchStates.front();
				for (size_t idx = 1; idx < branchStates.size(); ++idx) {
					const auto &other = branchStates[idx];
					for (auto it = mergedConstants.begin(); it != mergedConstants.end();) {
						auto otherIt = other.find(it->first);
						if (otherIt == other.end() || otherIt->second != it->second) {
							it = mergedConstants.erase(it);
						} else {
							++it;
						}
					}
				}
				context.currentKnownConstants = std::move(mergedConstants);
			}

			i = chainEnd;
			continue;
		}

		if (line->expression) {
			if (!inferExpression(
					line->expression, context, alreadyOrdered, bindingFrameStack, section->type != SectionType::Replacement
				)) {
				context.typesValid = false;
				return false;
			}
			DataType lineType = line->expression ? line->expression->type : DataType{};
			if (section->type != SectionType::Replacement && (!lineType.isDeduced() || lineType.kind != DataType::Kind::Void)) {
				context.setTypeFailure(
					"Standalone expression '" + std::string(line->expression->range.subString) +
					"' must return nothing; use discard if you want to ignore a value"
				);
				return false;
			}
		}
		if (!inferOpenedSection(line))
			return false;
	}
	if (loopSection) {
		std::unordered_set<VariableReference *> loopMutations = loopMutationScope.finish();
		bool needsLoopReinference = false;
		for (VariableReference *ref : loopMutations) {
			if (constantsAtLoopEntry.contains(ref)) {
				needsLoopReinference = true;
				break;
			}
		}
		if (needsLoopReinference) {
			context.currentKnownConstants = constantsAtLoopEntry;
			for (VariableReference *ref : loopMutations)
				context.setKnownConstant(ref, {});
			return inferSection(section, context, bindingFrameStack);
		}
	}
	context.typesValid = true;
	return true;
}

bool inferTypes(ParseContext &parseContext) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	InferenceContext context(parseContext);
	parseContext.constantValuesByReference.clear();
	parseContext.constantValuesByExpression.clear();
	parseContext.inferredIfChainSelections.clear();
	context.currentKnownConstants.clear();
	if (!inferSection(parseContext.mainSection, context, {}))
		return false;

	// Validate variables — all must have deduced types
	// Skip definition body sections: their variables are inferred when the definition is instantiated/expanded.
	bool valid = true;
	std::function<void(Section *)> validateVariables = [&](Section *section) {
		if (!section->patternDefinitions.empty())
			return;
		if (section->parent && !section->parent->patternDefinitions.empty())
			return;
		for (auto &[name, var] : section->variables) {
			if (!var->type.isDeduced()) {
				parseContext.diagnostics.push_back(Diagnostic(
					parseContext, Diagnostic::Level::Error, "variable has no type", var->definition->range, "name", name
				));
				valid = false;
			}
		}
		for (Section *child : section->children)
			validateVariables(child);
	};
	validateVariables(parseContext.mainSection);

	// Validate non-flex functions have deduced return types
	std::function<void(Section *)> validateReturnTypes = [&](Section *section) {
		if (section->type == SectionType::Function && !section->isFlex && !section->patternDefinitions.empty()) {
			for (auto &[argTypes, inst] : section->instantiations) {
				(void)argTypes;
				if (!inst.valid)
					continue;
				if (!inst.returnType.isDeduced()) {
					parseContext.diagnostics.push_back(Diagnostic(
						parseContext, Diagnostic::Level::Error, "function has no deduced return type",
						section->patternDefinitions.front()->range, "function",
						(std::string)section->patternDefinitions.front()->range.subString
					));
					valid = false;
					break; // one error per section is enough
				}
			}
		}
		for (Section *child : section->children)
			validateReturnTypes(child);
	};
	validateReturnTypes(parseContext.mainSection);

	return valid;
}

bool ensureSectionInstantiationInferred(
	ParseContext &parseContext, Section *section, PatternDefinition *definition, const std::vector<std::string> &parameterNames,
	const BindingFrameStack &callerBindingFrameStack, const std::vector<DataType> &argTypes,
	const Instantiation *callerInstantiation, InferenceContext *callerContext
) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	if (!section)
		return false;
	(void)definition;
	(void)callerInstantiation;

	std::vector<std::pair<std::string, Expression *>> paramBindings;
	paramBindings.reserve(parameterNames.size());
	for (const std::string &parameterName : parameterNames)
		paramBindings.push_back({parameterName, callerBindingFrameStack.lookup(parameterName)});
	auto evaluateParameterValue = [&](Expression *argumentExpression) {
		if (!argumentExpression)
			crashCompilerBug("missing section parameter expression while building instantiation key");
		return callerContext ? callerContext->lookupExpressionValue(argumentExpression)
							 : getExpressionCompileTimeValue(parseContext, argumentExpression, callerInstantiation);
	};
	InstantiationKey instantiationKey =
		findMatchingInstantiationKey(section, paramBindings, argTypes, evaluateParameterValue)
			.value_or(buildInstantiationKey({}, paramBindings, argTypes, evaluateParameterValue));
	if (callerContext && callerContext->trial) {
		if (!callerContext->trialJournal)
			crashCompilerBug("trial section-instantiation inference started without an active trial journal");
		callerContext->trialJournal->recordSectionInstantiationWrite(section, instantiationKey);
	}
	auto instIt = section->instantiations.find(instantiationKey);
	if (instIt == section->instantiations.end())
		instIt = section->instantiations.emplace(instantiationKey, Instantiation{}).first;
	Instantiation &inst = instIt->second;
	if (inst.argumentTypes.empty())
		inst.argumentTypes = argTypes;
	else
		requireCompilerInvariant(inst.argumentTypes == argTypes, "Instantiation argumentTypes diverged from map key");
	size_t parameterCount = std::min(parameterNames.size(), argTypes.size());
	for (size_t i = 0; i < parameterCount; i++) {
		if (parameterRequiresCompileTimeInstantiationValue(inst.requiredCompileTimeParameters, parameterNames[i], argTypes[i]))
			inst.requiredCompileTimeParameters.insert(parameterNames[i]);
	}
	for (size_t i = 0; i < parameterNames.size(); i++) {
		const std::string &parameterName = parameterNames[i];
		Expression *argumentExpression = callerBindingFrameStack.lookup(parameterName);
		if (!argumentExpression) {
			inst.constantParameterValues.erase(parameterName);
			continue;
		}
		if (i < argTypes.size() && argTypes[i].kind == DataType::Kind::Type) {
			inst.constantParameterValues[parameterName] = argTypes[i];
			continue;
		}
		CompileTimeValue value = callerContext
									 ? callerContext->lookupExpressionValue(argumentExpression)
									 : getExpressionCompileTimeValue(parseContext, argumentExpression, callerInstantiation);
		if (isCompileTimeKnown(value))
			inst.constantParameterValues[parameterName] = value;
		else
			inst.constantParameterValues.erase(parameterName);
	}
	if (inst.returnType.isDeduced() && !inst.needsReinfer)
		return inst.valid;
	if (inst.inferring)
		return inst.returnType.isDeduced() && inst.valid;

	inst.constantValuesByReference.clear();
	inst.constantValuesByExpression.clear();
	inst.writtenGlobalReferences.clear();
	inst.finalGlobalConstantValues.clear();
	inst.selectedOverloadsByCall.clear();
	inst.ifChainSelections.clear();
	InferenceContext context(parseContext, callerContext && callerContext->trial);
	if (callerContext) {
		context.currentKnownConstants = callerContext->currentKnownConstants;
		context.inheritedTrialExpressionValues =
			callerContext->trial ? &callerContext->trialExpressionValues : callerContext->inheritedTrialExpressionValues;
		context.inheritedTrialFunctionFlexExpansions = callerContext->trial
														   ? &callerContext->trialFunctionFlexExpansions
														   : callerContext->inheritedTrialFunctionFlexExpansions;
		context.trialJournal = callerContext->trialJournal;
		context.trialInstantiationCache =
			callerContext->trialInstantiationCache
				? callerContext->trialInstantiationCache
				: (callerContext->trial ? callerContext->ensureTrialInstantiationCache() : nullptr);
		context.suppressDiagnostics = callerContext->suppressDiagnostics;
		context.suppressReinferPassDiagnostics = callerContext->suppressReinferPassDiagnostics;
	}
	inst.inferring = true;
	Instantiation *savedInst = context.currentInstantiation;
	for (const auto &[name, value] : inst.constantParameterValues) {
		Variable *var = findOwnSectionVariable(section, name);
		if (var)
			context.setKnownConstant(var->definition, value);
	}
	context.currentInstantiation = &inst;
	size_t bindingCount = std::min(parameterNames.size(), argTypes.size());
	BindingFrame nonFlexTypeBindings =
		rebuildInstantiationNonFlexParameterBindings(inst, bindingCount, [&](size_t index) -> const std::string & {
		return parameterNames[index];
	}, [&](size_t index) -> VariableReference * {
		return findPatternParameterDefinition(definition, parameterNames[index]);
	}, argTypes, section->globalVariables);
	bool inferenceSucceeded = inferSection(section, context, makeBindingFrameStack(nonFlexTypeBindings));
	for (VariableReference *reference : inst.writtenGlobalReferences) {
		auto knownIt = context.currentKnownConstants.find(reference);
		if (knownIt != context.currentKnownConstants.end() && isCompileTimeKnown(knownIt->second))
			inst.finalGlobalConstantValues[reference] = knownIt->second;
	}
	context.currentInstantiation = savedInst;
	inst.inferring = false;
	inst.valid = inferenceSucceeded;
	if (callerContext) {
		callerContext->observedInProgressUndeducedInstantiation =
			callerContext->observedInProgressUndeducedInstantiation || context.observedInProgressUndeducedInstantiation;
		callerContext->inheritTypeFailureFrom(context);
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

	if (inst.returnType.kind == DataType::Kind::Any)
		inst.returnType = {DataType::Kind::Void};

	return inst.returnType.isDeduced();
}
