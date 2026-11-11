static bool inferIncrementalFunction(ParseContext &parseContext, const ParseContext::IncrementalFunctionRequest &request) {
	Section *section = request.section;
	requireCompilerInvariant(
		section->type == SectionType::Function && !section->patternDefinitions.empty(),
		"incremental preinference requires a function definition"
	);
	requireCompilerInvariant(
		request.parameterNames.size() == request.key.argumentTypes.size(), "incremental preinference lost a parameter type"
	);
	std::vector<Expression> arguments(request.parameterNames.size());
	std::vector<std::pair<std::string, Expression *>> bindings;
	for (size_t index = 0; index < arguments.size(); index++) {
		Expression &argument = arguments[index];
		argument.kind = Expression::Kind::TypedPlaceholder;
		argument.type = request.key.argumentTypes[index];
		argument.range = section->patternDefinitions.front()->range;
		const std::string &name = request.parameterNames[index];
		auto constant = std::ranges::find_if(request.key.compileTimeParameters, [&](const auto &entry) {
			return entry.first == name;
		});
		if (constant != request.key.compileTimeParameters.end())
			argument.compileTimeValue = constant->second;
		bindings.emplace_back(name, &argument);
	}
	size_t diagnosticCount = parseContext.diagnostics.size();
	bool inferred = ensureSectionInstantiationInferred(
		parseContext, section, section->patternDefinitions.front(), bindings, request.key.argumentTypes,
		request.requiredCompileTimeParameters, nullptr, nullptr
	);
	if (inferred) {
		auto found = section->instantiations.find(request.key);
		// A body can start requiring another compile-time parameter. The caller
		// must supply its value before that new instantiation can be compared.
		if (found == section->instantiations.end())
			return false;
		Instantiation &instantiation = found->second;
		if (instantiation.purity == InstantiationPurity::Pure && instantiation.returnType.kind != DataType::Kind::Void) {
			InferenceContext context(parseContext);
			context.currentInstantiation = &instantiation;
			InferenceContext::DependencyOwner owner{section, nullptr, instantiation.body->storageScope};
			context.dependencyReads.owner = &owner;
			context.currentInstantiatedSectionBody = instantiation.body.get();
			PureExecutionState execution(parseContext, context);
			try {
				for (const auto &values : request.observedArguments) {
					requireCompilerInvariant(
						values.size() == request.parameterNames.size(), "cached pure observation lost a parameter value"
					);
					std::vector<std::pair<std::string, CompileTimeValue>> namedValues;
					for (size_t index = 0; index < values.size(); index++)
						namedValues.emplace_back(request.parameterNames[index], values[index]);
					parseContext.preinferredPureValuesChecked++;
					executePureInstantiationReturnValue(execution, section, instantiation, namedValues);
				}
			} catch (const PureExecutionStopped &) {
				inferred = false;
			}
		}
	}
	if (!inferred)
		parseContext.diagnostics.resize(diagnosticCount);
	return inferred;
}
