// These checks consume facts from the normal inference walk. Unknown alternatives
// prevent a diagnostic; they never trigger a separate memory-safety analysis.
static bool addressIsDefinitelyNull(const AddressProvenance &address) {
	return address.mayBeNull && !address.unknown && !address.mayBeStatic && address.mayTargets.empty();
}

static std::optional<std::string_view>
memoryIntrinsicViolation(Expression *expression, InferenceContext &context, const BindingFrameStack &bindings) {
	IntrinsicKind kind = intrinsicKind(expression->intrinsicName);
	bool deallocation = kind == IntrinsicKind::CheckDeallocation;
	bool reads = kind == IntrinsicKind::Dereference || kind == IntrinsicKind::CallPointer || isAtomicIntrinsicKind(kind);
	bool writes = kind == IntrinsicKind::Store || kind == IntrinsicKind::StoreAt || kind == IntrinsicKind::InitializeAt;
	bool destruction = kind == IntrinsicKind::DestroyAt;
	bool property = kind == IntrinsicKind::Property;
	if (!deallocation && !reads && !writes && !property && !destruction)
		return std::nullopt;
	Expression *pointer = expression->arguments[1];
	DataType pointerType = resolveKnownExpressionType(pointer, bindings);
	AddressProvenance address;
	if (kind == IntrinsicKind::Store) {
		std::optional<AddressProvenance> destination = inferLValueAddressProvenance(pointer, context, bindings, false);
		if (!destination)
			return std::nullopt;
		address = std::move(*destination);
	} else {
		address = inferAddressProvenance(pointer, context, bindings);
	}
	if ((kind != IntrinsicKind::Store && !pointerType.isPointer()) || (property && pointerType.kind != DataType::Kind::Class))
		return std::nullopt;
	if (kind == IntrinsicKind::DestroyAt && !typeHasManagedLifecycle(pointerType.dereferenced()))
		return std::nullopt;
	bool writesLiteral = writes || (isAtomicIntrinsicKind(kind) && kind != IntrinsicKind::AtomicLoad);
	if (deallocation) {
		// A null alternative is legal for deallocation. Parameters may alias
		// caller-owned storage, including heap storage, rather than local slots.
		if (address.unknown || address.mayBeNull || (address.mayTargets.empty() && !address.mayBeStatic))
			return std::nullopt;
		bool allLocal = !address.mayBeStatic;
		for (VariableReference *target : address.mayTargets) {
			if (variableReferenceIsCurrentInstantiationParameter(context, target))
				return std::nullopt;
			Variable *variable = variableForAddressTarget(target);
			requireCompilerInvariant(variable, "tracked deallocation target has no variable storage");
			allLocal = allLocal && !variable->isGlobal;
		}
		return allLocal ? "cannot deallocate stack storage" : "cannot deallocate static or variable storage";
	}
	int depth = property ? pointerType.pointerDepth : 1;
	for (int level = 0; level < depth; level++) {
		if (addressIsDefinitelyNull(address))
			return "cannot dereference a null pointer";
		if (writesLiteral && level == depth - 1 && address.mayBeStatic && !address.unknown && !address.mayBeNull &&
			address.mayTargets.empty())
			return "cannot write to static literal storage";
		address = dereferenceAddressProvenance(std::move(address), 1, context);
	}
	return std::nullopt;
}

static void
recordIntrinsicMemoryViolation(Expression *expression, InferenceContext &context, const BindingFrameStack &bindings) {
	std::optional<std::string_view> violation = memoryIntrinsicViolation(expression, context, bindings);
	if (!violation)
		return;
	Range range = context.activeFlexCallStack.size() > context.memoryFlexBase ? context.activeFlexCallStack.back()->range
																			  : expression->range;
	auto diagnostic = std::make_shared<Diagnostic>(context.parseContext, Diagnostic::Level::Error, *violation, range);
	expression->memoryFacts.value = diagnostic;
	if (intrinsicKind(expression->intrinsicName) != IntrinsicKind::Dereference)
		expression->memoryFacts.address = std::move(diagnostic);
}

static const MemoryFacts &expressionMemoryFacts(Expression *expression, const BindingFrameStack &bindings) {
	// A flex call already combines its replacement's effects and result.
	// Resolve parameter bindings without expanding away those call effects.
	ResolvedBindingLayers resolved = resolveExpressionBindingWithCallerScope(expression, bindings);
	requireCompilerInvariant(resolved.expression, "memory fact lookup lost its bound expression");
	return resolved.expression->inferredConversion ? resolved.expression->inferredConversion->memoryFacts
												   : resolved.expression->memoryFacts;
}

static void recordExpressionMemoryFacts(Expression *expression, InferenceContext &context, const BindingFrameStack &bindings) {
	MemoryFacts &facts = expression->memoryFacts;
	auto consume = [&](const std::shared_ptr<const Diagnostic> &violation) {
		if (!facts.value)
			facts.value = violation;
		if (!facts.address)
			facts.address = violation;
	};
	consume(facts.effects);
	if (expression->kind == Expression::Kind::Variable) {
		facts = expressionMemoryFacts(expression, bindings);
		return;
	}
	if (expression->kind == Expression::Kind::PatternCall) {
		if (expression->inferredFlexExpansion) {
			const MemoryFacts &result = expression->inferredFlexExpansion->memoryFacts;
			if (!facts.value)
				facts.value = result.value;
			if (!facts.address)
				facts.address = result.address;
			return;
		}
		if (expression->selectedInstantiation)
			consume(expression->selectedInstantiation->memoryViolation);
		for (Expression *argument : expression->arguments) {
			if (argument->type.isMetaType())
				continue;
			const MemoryFacts &argumentFacts = expressionMemoryFacts(argument, bindings);
			if (!argumentFacts.value)
				continue;
			bool byReference = inferLValueAddressProvenance(argument, context, bindings, false).has_value();
			consume(byReference ? argumentFacts.address : argumentFacts.value);
		}
		return;
	}
	if (expression->kind == Expression::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(expression->intrinsicName);
		if (kind == IntrinsicKind::Select) {
			consume(expressionMemoryFacts(expression->arguments[1], bindings).value);
			CompileTimeValue condition = resolveStoredCompileTimeValue(expression->arguments[1], bindings, &context);
			if (const auto *selected = std::get_if<bool>(&condition)) {
				consume(expressionMemoryFacts(expression->arguments[*selected ? 2 : 3], bindings).value);
			} else {
				consume(expressionMemoryFacts(expression->arguments[2], bindings).value);
				consume(expressionMemoryFacts(expression->arguments[3], bindings).value);
			}
			return;
		}
		for (size_t index = 1; index < expression->arguments.size(); index++) {
			bool typeOnly = intrinsicArgumentIsCompileTimeOnly(expression->intrinsicName, static_cast<int>(index)) ||
							(index == 1 && (kind == IntrinsicKind::TypeOf || kind == IntrinsicKind::ElementType ||
											kind == IntrinsicKind::TypeExtent));
			if (typeOnly)
				continue;
			const MemoryFacts &argument = expressionMemoryFacts(expression->arguments[index], bindings);
			bool addressOnly = index == 1 && (kind == IntrinsicKind::AddressOf || kind == IntrinsicKind::Store);
			consume(addressOnly ? argument.address : argument.value);
		}
		return;
	}
	for (Expression *argument : expression->arguments)
		consume(expressionMemoryFacts(argument, bindings).value);
}

static bool consumeStatementMemoryFacts(Expression *expression, InferenceContext &context) {
	const std::shared_ptr<const Diagnostic> &violation = expression->memoryFacts.value;
	if (!violation)
		return true;
	if (context.activeFlexCallStack.size() > context.memoryFlexBase) {
		MemoryFacts &call = context.activeFlexCallStack.back()->memoryFacts;
		if (!call.effects)
			call.effects = violation;
	} else if (context.currentInstantiation) {
		if (context.trial) {
			requireCompilerInvariant(context.trialJournal, "trial memory proof requires a rollback journal");
			context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
		}
		if (!context.currentInstantiation->memoryViolation)
			context.currentInstantiation->memoryViolation = violation;
	} else {
		context.fail(*violation, 1, false);
		return false;
	}
	return true;
}
