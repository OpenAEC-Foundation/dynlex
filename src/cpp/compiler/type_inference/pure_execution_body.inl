// A value summary does not contain the typed executable body. A new nested
// argument can need that body even when ordinary calls already reuse its IR.
static bool ensurePureExecutionBody(
	PureExecutionState &state, Section *section, Instantiation &instantiation,
	const std::vector<std::pair<std::string, CompileTimeValue>> &values
) {
	if (instantiation.bodyInferred || !instantiation.inferenceReusedFromCache)
		return true;
	if (state.parseContext.restoreInferredBody &&
		state.parseContext.restoreInferredBody(section, instantiation, &state.inferenceContext))
		return true;
	requireCompilerInvariant(
		values.size() == instantiation.argumentTypes.size(), "pure execution argument count differs from its instantiation"
	);
	InferenceContext &context = state.inferenceContext;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial pure execution has no journal");
		context.trialJournal->recordInstantiationWrite(&instantiation);
	}
	instantiation.needsReinfer = true;
	std::vector<Expression> arguments(values.size());
	std::vector<std::pair<std::string, Expression *>> bindings;
	for (size_t index = 0; index < values.size(); index++) {
		Expression &argument = arguments[index];
		argument.kind = Expression::Kind::TypedPlaceholder;
		argument.type = instantiation.argumentTypes[index];
		argument.compileTimeValue = values[index].second;
		argument.range = Range(section->openingLine, section->openingLine->patternText);
		bindings.emplace_back(values[index].first, &argument);
	}
	const auto *savedOwner = context.dependencyReads.owner;
	InferenceContext::DependencyOwner owner{section, savedOwner, instantiation.body->storageScope};
	if (!state.valueReadOwners.empty()) {
		owner.section = state.valueReadOwners.back().section;
		owner.scope = state.valueReadOwners.back().scope;
		context.dependencyReads.owner = &owner;
	}
	struct RestoreOwner {
		InferenceContext &context;
		const InferenceContext::DependencyOwner *owner;
		~RestoreOwner() { context.dependencyReads.owner = owner; }
	} restore{context, savedOwner};
	auto required = instantiation.requiredCompileTimeParameters;
	return ensureSectionInstantiationInferred(
		state.parseContext, section, nullptr, bindings, instantiation.argumentTypes, required, context.currentInstantiation,
		&context
	);
}
