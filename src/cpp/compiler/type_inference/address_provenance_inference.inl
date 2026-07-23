#pragma once

static void joinAddressProvenance(AddressProvenance &destination, const AddressProvenance &source) {
	destination.mayTargets.insert(source.mayTargets.begin(), source.mayTargets.end());
	destination.unknown = destination.unknown || source.unknown;
}

static AddressInferenceState mergeAddressInferenceStates(const std::vector<AddressInferenceState> &states) {
	requireCompilerInvariant(!states.empty(), "address inference state merge requires a reachable path");
	AddressInferenceState merged;
	std::unordered_set<VariableReference *> references;
	for (const AddressInferenceState &state : states) {
		for (const auto &[reference, provenance] : state->variables) {
			(void)provenance;
			references.insert(reference);
		}
		merged->addressTakenVariables.insert(state->addressTakenVariables.begin(), state->addressTakenVariables.end());
		joinAddressProvenance(merged->externallyEscaped, state->externallyEscaped);
	}
	for (VariableReference *reference : references) {
		AddressProvenance provenance;
		for (const AddressInferenceState &state : states) {
			auto value = state->variables.find(reference);
			if (value == state->variables.end())
				provenance.unknown = true;
			else
				joinAddressProvenance(provenance, value->second);
		}
		merged->variables.emplace(reference, std::move(provenance));
	}
	return merged;
}

static AddressProvenance
inferAddressProvenance(Expression *expression, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	if (!expression)
		crashCompilerBug("address provenance inference received a null expression");
	BindingFrameStack resolvedBindingFrameStack;
	Expression *resolvedExpression =
		resolveThroughBindingsDeep(expression, bindingFrameStack, resolvedBindingFrameStack, &context);
	if (!resolvedExpression)
		crashCompilerBug("address provenance inference lost its expression while resolving bindings");
	if (resolvedExpression->kind == Expression::Kind::Variable && resolvedExpression->variable)
		return context.lookupAddressProvenance(resolvedExpression->variable);
	if (resolvedExpression->kind == Expression::Kind::Literal)
		return {};
	if (resolvedExpression->kind == Expression::Kind::ArrayLiteral) {
		AddressProvenance provenance;
		for (Expression *element : resolvedExpression->arguments)
			joinAddressProvenance(provenance, inferAddressProvenance(element, context, resolvedBindingFrameStack));
		return provenance;
	}
	if (resolvedExpression->kind == Expression::Kind::PatternCall) {
		AddressProvenance provenance;
		if (resolvedExpression->selectedInstantiation &&
			resolvedExpression->selectedInstantiation->hasReturnAddressProvenance) {
			provenance = resolvedExpression->selectedInstantiation->returnAddressProvenance;
		} else {
			provenance.unknown = true;
		}
		for (Expression *argument : resolvedExpression->arguments)
			if (argument)
				joinAddressProvenance(provenance, inferAddressProvenance(argument, context, resolvedBindingFrameStack));
		return provenance;
	}
	if (resolvedExpression->kind != Expression::Kind::IntrinsicCall)
		return resolvedExpression->type.isPointer() ? AddressProvenance{.mayTargets = {}, .unknown = true}
													: AddressProvenance{};

	IntrinsicKind kind = intrinsicKind(resolvedExpression->intrinsicName);
	if (kind == IntrinsicKind::AddressOf) {
		BindingFrameStack targetBindingFrameStack;
		Expression *targetExpression = resolveThroughBindingsDeep(
			resolvedExpression->arguments[1], resolvedBindingFrameStack, targetBindingFrameStack, &context
		);
		if (!targetExpression || targetExpression->kind != Expression::Kind::Variable || !targetExpression->variable)
			return {.mayTargets = {}, .unknown = true};
		VariableReference *target = context.normalizeReference(targetExpression->variable);
		if (!target)
			return {.mayTargets = {}, .unknown = true};
		context.currentAddressState->addressTakenVariables.insert(target);
		return {.mayTargets = {target}};
	}
	if (kind == IntrinsicKind::Cast && resolvedExpression->arguments.size() > 2) {
		AddressProvenance source = inferAddressProvenance(resolvedExpression->arguments[1], context, resolvedBindingFrameStack);
		if (resolvedExpression->arguments[1]->type.isPointer() || !source.mayTargets.empty() || source.unknown)
			return source;
		CompileTimeValue sourceValue =
			resolveStoredCompileTimeValue(resolvedExpression->arguments[1], resolvedBindingFrameStack, &context);
		if (std::optional<std::int64_t> integer = getCompileTimeIntegerValue(sourceValue); integer && *integer == 0)
			return {};
		return resolvedExpression->type.isPointer() ? AddressProvenance{.mayTargets = {}, .unknown = true}
													: AddressProvenance{};
	}
	if (kind == IntrinsicKind::Select && resolvedExpression->arguments.size() > 3) {
		CompileTimeValue condition =
			resolveStoredCompileTimeValue(resolvedExpression->arguments[1], resolvedBindingFrameStack, &context);
		if (const auto *selected = std::get_if<bool>(&condition)) {
			return inferAddressProvenance(resolvedExpression->arguments[*selected ? 2 : 3], context, resolvedBindingFrameStack);
		}
		AddressProvenance provenance =
			inferAddressProvenance(resolvedExpression->arguments[2], context, resolvedBindingFrameStack);
		joinAddressProvenance(
			provenance, inferAddressProvenance(resolvedExpression->arguments[3], context, resolvedBindingFrameStack)
		);
		return provenance;
	}
	if (kind == IntrinsicKind::Property && resolvedExpression->arguments.size() > 2) {
		AddressProvenance owner = inferAddressProvenance(resolvedExpression->arguments[1], context, resolvedBindingFrameStack);
		const DataType &ownerType = resolvedExpression->arguments[1]->type;
		if (!ownerType.isPointer() || ownerType.kind != DataType::Kind::Class)
			return owner;
		AddressProvenance provenance{.mayTargets = {}, .unknown = owner.unknown};
		for (VariableReference *ownerTarget : owner.mayTargets)
			joinAddressProvenance(provenance, context.lookupAddressProvenance(ownerTarget));
		return provenance;
	}
	if (kind == IntrinsicKind::Dereference) {
		AddressProvenance pointerStorage =
			inferAddressProvenance(resolvedExpression->arguments[1], context, resolvedBindingFrameStack);
		AddressProvenance provenance{.mayTargets = {}, .unknown = pointerStorage.unknown};
		for (VariableReference *pointerVariable : pointerStorage.mayTargets)
			joinAddressProvenance(provenance, context.lookupAddressProvenance(pointerVariable));
		return provenance;
	}
	if ((kind == IntrinsicKind::Add || kind == IntrinsicKind::Subtract) && resolvedExpression->arguments.size() > 2) {
		AddressProvenance provenance;
		for (size_t argumentIndex = 1; argumentIndex <= 2; argumentIndex++) {
			AddressProvenance argument =
				inferAddressProvenance(resolvedExpression->arguments[argumentIndex], context, resolvedBindingFrameStack);
			if (resolvedExpression->arguments[argumentIndex]->type.isPointer() || !argument.mayTargets.empty() ||
				argument.unknown)
				joinAddressProvenance(provenance, argument);
		}
		if (!provenance.mayTargets.empty() || provenance.unknown)
			return provenance;
	}
	AddressProvenance provenance;
	for (size_t argumentIndex = 1; argumentIndex < resolvedExpression->arguments.size(); argumentIndex++) {
		Expression *argument = resolvedExpression->arguments[argumentIndex];
		if (argument)
			joinAddressProvenance(provenance, inferAddressProvenance(argument, context, resolvedBindingFrameStack));
	}
	if (isExternalCallIntrinsicKind(kind))
		provenance.unknown = true;
	else if (resolvedExpression->type.isPointer() && provenance.mayTargets.empty())
		provenance.unknown = true;
	return provenance;
}

static Variable *variableForAddressTarget(VariableReference *reference) {
	if (!reference || !reference->range.line || !reference->range.line->section)
		return nullptr;
	return reference->range.line->section->findVariable(reference->name);
}

static bool typeMayContainAddresses(const DataType &type) {
	if (type.isPointer())
		return true;
	if (type.kind == DataType::Kind::Array && type.arrayElementType)
		return typeMayContainAddresses(*type.arrayElementType);
	if (type.kind != DataType::Kind::Class || !type.classDefinition || type.classInstIndex < 0)
		return false;
	const auto &fieldTypes = type.classDefinition->instantiations[type.classInstIndex].fieldTypes;
	return std::ranges::any_of(fieldTypes, [](const DataType &fieldType) {
		return typeMayContainAddresses(fieldType);
	});
}

static void noteUnknownAddressWrite(InferenceContext &context) {
	if (!context.currentInstantiation || context.currentInstantiation->writesThroughUnknownAddress)
		return;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial unknown-address write requires a rollback journal");
		context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
	}
	context.currentInstantiation->writesThroughUnknownAddress = true;
}

static void noteUnknownExternalEscape(InferenceContext &context) {
	if (!context.currentInstantiation || context.currentInstantiation->externallyEscapesUnknownAddress)
		return;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial unknown external escape requires a rollback journal");
		context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
	}
	context.currentInstantiation->externallyEscapesUnknownAddress = true;
}

static void noteAddressTargetWrite(InferenceContext &context, VariableReference *target) {
	Variable *variable = variableForAddressTarget(target);
	if (variable && variable->isGlobal)
		context.noteWrittenGlobalReference(target);
}

static void addUnknownAddressUniverse(InferenceContext &context, std::unordered_set<VariableReference *> &targets) {
	targets.insert(
		context.currentAddressState->addressTakenVariables.begin(), context.currentAddressState->addressTakenVariables.end()
	);
}

static std::unordered_set<VariableReference *>
possibleAddressTargets(InferenceContext &context, const AddressProvenance &provenance) {
	std::unordered_set<VariableReference *> targets = provenance.mayTargets;
	if (provenance.unknown)
		addUnknownAddressUniverse(context, targets);
	return targets;
}

static void collectTransitiveAddressTargets(
	InferenceContext &context, const AddressProvenance &provenance, std::unordered_set<VariableReference *> &targets,
	bool &unknown
) {
	unknown = unknown || provenance.unknown;
	for (VariableReference *target : provenance.mayTargets) {
		if (!targets.insert(target).second)
			continue;
		Variable *variable = variableForAddressTarget(target);
		auto stored = context.currentAddressState->variables.find(context.normalizeReference(target));
		if (stored == context.currentAddressState->variables.end()) {
			if (variable && typeMayContainAddresses(variable->type))
				unknown = true;
			continue;
		}
		if (!stored->second.mayTargets.empty() || stored->second.unknown)
			collectTransitiveAddressTargets(context, stored->second, targets, unknown);
	}
}

static void invalidateAddressTargets(InferenceContext &context, const AddressProvenance &provenance, bool transitive) {
	std::unordered_set<VariableReference *> targets;
	bool unknown = provenance.unknown;
	if (transitive)
		collectTransitiveAddressTargets(context, provenance, targets, unknown);
	else
		targets = provenance.mayTargets;
	if (unknown) {
		noteUnknownAddressWrite(context);
		addUnknownAddressUniverse(context, targets);
		if (transitive) {
			std::vector<VariableReference *> addressTaken(targets.begin(), targets.end());
			for (VariableReference *target : addressTaken) {
				auto stored = context.currentAddressState->variables.find(context.normalizeReference(target));
				if (stored == context.currentAddressState->variables.end())
					continue;
				bool nestedUnknown = false;
				collectTransitiveAddressTargets(context, stored->second, targets, nestedUnknown);
			}
		}
	}
	for (VariableReference *target : targets) {
		noteAddressTargetWrite(context, target);
		context.setKnownConstant(target, {});
		Variable *variable = variableForAddressTarget(target);
		if (context.currentAddressState->variables.contains(context.normalizeReference(target)) ||
			(variable && typeMayContainAddresses(variable->type)))
			context.setAddressProvenance(target, {.mayTargets = {}, .unknown = true});
	}
}

static void applyStoreThroughAddress(
	InferenceContext &context, const AddressProvenance &destination, Expression *assignedExpression,
	const BindingFrameStack &bindingFrameStack
) {
	std::unordered_set<VariableReference *> targets = possibleAddressTargets(context, destination);
	if (destination.unknown)
		noteUnknownAddressWrite(context);
	if (targets.empty())
		return;
	bool definiteTarget = !destination.unknown && targets.size() == 1;
	CompileTimeValue assignedValue =
		assignedExpression ? context.lookupExpressionValue(assignedExpression) : CompileTimeValue{};
	AddressProvenance assignedProvenance = assignedExpression
											   ? inferAddressProvenance(assignedExpression, context, bindingFrameStack)
											   : AddressProvenance{.mayTargets = {}, .unknown = true};
	for (VariableReference *target : targets) {
		noteAddressTargetWrite(context, target);
		CompileTimeValue previousValue = context.lookupKnownConstant(target);
		context.setKnownConstant(target, definiteTarget || previousValue == assignedValue ? assignedValue : CompileTimeValue{});
		if (definiteTarget) {
			context.setAddressProvenance(target, assignedProvenance);
		} else {
			AddressProvenance possibleProvenance = context.lookupAddressProvenance(target);
			joinAddressProvenance(possibleProvenance, assignedProvenance);
			context.setAddressProvenance(target, std::move(possibleProvenance));
		}
	}
}

static void retainExternalAddress(InferenceContext &context, const AddressProvenance &provenance) {
	joinAddressProvenance(context.currentAddressState->externallyEscaped, provenance);
	if (provenance.unknown)
		noteUnknownExternalEscape(context);
}

static void invalidateExternalCallWrites(InferenceContext &context) {
	invalidateAddressTargets(context, context.currentAddressState->externallyEscaped, true);
}
