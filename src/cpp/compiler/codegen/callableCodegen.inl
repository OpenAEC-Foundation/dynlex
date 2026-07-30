static std::string buildCallableFunctionName(PatternDefinition *definition, const std::vector<DataType> &argumentTypes) {
	std::string name = getPatternFunctionName(definition->section) + "_callable";
	for (const DataType &type : argumentTypes)
		name += "_" + type.toString();
	return name;
}

bool ensureCallableFunctionGenerated(
	ParseContext &context, const CallableFunctionMatch &match, Instantiation &instantiation, bool requireExternalLinkage,
	llvm::Function *&generatedFunction
) {
	generatedFunction = nullptr;
	PatternDefinition *definition = match.definition;
	requireCompilerInvariant(
		definition && definition->section && definition->section->type == SectionType::Function && !definition->section->isFlex,
		"non-callable definition reached callable codegen"
	);
	requireCompilerInvariant(!context.options.emitSPIRV, "function reference reached SPIR-V codegen");

	std::vector<CallableFunctionParameter> parameters;
	collectCallableFunctionParameters(match, parameters);
	Instantiation *instantiationPointer = &instantiation;
	const std::vector<DataType> &argumentTypes = instantiationPointer->argumentTypes;
	requireCompilerInvariant(parameters.size() == argumentTypes.size(), "callable parameter count changed after inference");

	std::vector<std::pair<std::string, Expression *>> parameterBindings;
	parameterBindings.reserve(parameters.size());
	for (size_t parameterIndex = 0; parameterIndex < parameters.size(); parameterIndex++) {
		requireCompilerInvariant(
			parameters[parameterIndex].type == argumentTypes[parameterIndex], "callable parameter type changed after inference"
		);
		requireCompilerInvariant(
			!parameters[parameterIndex].requiresCompileTimeValue, "fixed callable parameter reached code generation"
		);
		parameterBindings.push_back({parameters[parameterIndex].name, nullptr});
	}

	Section *section = definition->section;
	if (!instantiationPointer->llvmFunction &&
		!generateSpecializedFunction(context, section, parameterBindings, *instantiationPointer))
		return false;
	requireCompilerInvariant(instantiationPointer->valid, "invalid callable instantiation reached codegen");
	requireCompilerInvariant(!instantiationPointer->needsReinfer, "unfinished callable instantiation reached codegen");
	requireCompilerInvariant(instantiationPointer->returnType.isDeduced(), "callable without a return type reached codegen");
	if (instantiationPointer->llvmCallableFunction) {
		if (requireExternalLinkage)
			instantiationPointer->llvmCallableFunction->setLinkage(llvm::GlobalValue::ExternalLinkage);
		generatedFunction = instantiationPointer->llvmCallableFunction;
		return true;
	}

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	std::vector<llvm::Type *> parameterTypes;
	parameterTypes.reserve(argumentTypes.size());
	for (const DataType &parameterType : argumentTypes)
		parameterTypes.push_back(getLLVMType(context, parameterType));
	llvm::Type *returnType = getLLVMType(context, instantiationPointer->returnType);
	llvm::FunctionType *callableType = llvm::FunctionType::get(returnType, parameterTypes, false);
	std::string callableName = buildCallableFunctionName(definition, argumentTypes);
	llvm::GlobalValue::LinkageTypes linkage = (requireExternalLinkage || section->isExposed)
												  ? llvm::GlobalValue::ExternalLinkage
												  : llvm::GlobalValue::InternalLinkage;
	llvm::Function *callableFunction = llvm::Function::Create(callableType, linkage, callableName, context.llvmModule);
	instantiationPointer->llvmCallableFunction = callableFunction;

	size_t argumentIndex = 0;
	for (llvm::Argument &argument : callableFunction->args())
		argument.setName(parameters[argumentIndex++].name);

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", callableFunction);
	llvm::BasicBlock *savedBlock = builder.GetInsertBlock();
	llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();
	llvm::DebugLoc savedDebugLocation = builder.getCurrentDebugLocation();
	std::vector<ParseContext::ManagedStorageState> savedManagedStorage = std::move(context.managedLocalStorage);
	context.managedLocalStorage.clear();
	builder.SetInsertPoint(entry);

	std::vector<llvm::Value *> callArguments;
	std::vector<llvm::Value *> managedParameterStorage;
	callArguments.reserve(argumentTypes.size());
	argumentIndex = 0;
	bool succeeded = true;
	for (llvm::Argument &argument : callableFunction->args()) {
		const DataType &argumentType = argumentTypes[argumentIndex];
		llvm::AllocaInst *parameterStorage = createEntryAlloca(context, parameters[argumentIndex].name, argumentType);
		if (typeHasManagedLifecycle(argumentType)) {
			if (!retainManagedValue(context, argumentType, &argument)) {
				succeeded = false;
				break;
			}
			registerManagedStorage(context, parameterStorage, argumentType, section);
			initializeManagedStorage(context, parameterStorage, argumentType, &argument);
			managedParameterStorage.push_back(parameterStorage);
		} else {
			builder.CreateAlignedStore(&argument, parameterStorage, getLLVMABIAlignment(context, argumentType));
		}
		callArguments.push_back(parameterStorage);
		argumentIndex++;
	}

	if (succeeded) {
		llvm::CallInst *call = builder.CreateCall(instantiationPointer->llvmFunction, callArguments);
		for (auto storage = managedParameterStorage.rbegin(); storage != managedParameterStorage.rend(); storage++) {
			if (!releaseManagedTemporaryStorage(context, *storage)) {
				succeeded = false;
				break;
			}
		}
		if (succeeded) {
			if (instantiationPointer->returnType.kind == DataType::Kind::Void)
				builder.CreateRetVoid();
			else
				builder.CreateRet(call);
		}
	}
	if (succeeded)
		requireCompilerInvariant(context.managedLocalStorage.empty(), "callable wrapper left managed storage unclosed");
	context.managedLocalStorage = std::move(savedManagedStorage);

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
		builder.SetCurrentDebugLocation(savedDebugLocation);
	}

	if (!succeeded)
		return false;
	generatedFunction = callableFunction;
	return true;
}
