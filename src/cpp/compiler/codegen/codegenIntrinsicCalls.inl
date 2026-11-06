[[gnu::noinline]] static CodegenResult generateExternalCallIntrinsic(
	ParseContext &context, const std::vector<Expression *> &args, IntrinsicKind kind, const DataType &returnType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	// call: args[1]="library", args[2]="function", args[3]="return type", args[4+]=actual args.
	// variadic call: the fixed argument count is args[4], and actual args begin at args[5].
	// call pointer: args[1]=runtime callee, args[2]="return type", args[3+]=actual args.
	const bool isPointerCall = kind == IntrinsicKind::CallPointer || kind == IntrinsicKind::Invoke;
	std::string funcName;
	if (!isPointerCall) {
		std::string library = getCompileTimeString(context, args[1]);
		funcName = getCompileTimeString(context, args[2]);
		if (!library.empty() && library != "libc")
			context.requiredLibraries.insert(library);
	}

	llvm::Type *returnLLVMType = getLLVMType(context, returnType);
	llvm::Value *pointerCallee = nullptr;
	if (isPointerCall) {
		if (!generateIntrinsicRuntimeValue(context, args[1], pointerCallee))
			return CodegenResult::failure();
		requireCompilerInvariant(pointerCallee != nullptr, "call pointer callee produced no runtime value after inference");
	}

	// Build call arguments — string literals become global constant pointers
	std::vector<llvm::Value *> callArgs;
	std::vector<DataType> callArgumentTypes;
	std::vector<std::pair<DataType, llvm::Value *>> ownedManagedArguments;
	size_t runtimeArgumentStart = externalCallRuntimeArgumentStart(kind);
	for (size_t i = runtimeArgumentStart; i < args.size(); ++i) {
		DataType argumentType = finalizedExpressionType(context, args[i]);
		if (args[i]->kind == Expression::Kind::Literal) {
			if (auto *str = std::get_if<std::string>(&args[i]->literalValue)) {
				std::string globalName = ".str." + std::to_string(context.stringConstants.size());
				llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, *str, true);
				llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
					*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
				);
				context.stringConstants[*str] = strGlobal;
				callArgs.push_back(builder.CreateInBoundsGEP(
					strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
				));
				callArgumentTypes.push_back(argumentType);
				continue;
			}
		}
		llvm::Value *argVal = nullptr;
		if (!generateIntrinsicRuntimeValue(context, args[i], argVal))
			return CodegenResult::failure();
		requireCompilerInvariant(argVal != nullptr, "external call argument produced no runtime value after inference");
		callArgs.push_back(argVal);
		callArgumentTypes.push_back(argumentType);
		if (typeHasManagedLifecycle(argumentType) && managedExpressionResultIsOwned(context, args[i]))
			ownedManagedArguments.push_back({argumentType, argVal});
	}

	size_t fixedArgumentCount = callArgs.size();
	bool isVariadic = kind == IntrinsicKind::VariadicCall;
	if (isVariadic) {
		int inferredFixedArgumentCount = 0;
		requireCompilerInvariant(
			resolveStoredCompileTimeInteger(args[4], {}, inferredFixedArgumentCount),
			"variadic call reached codegen without its inferred fixed argument count"
		);
		requireCompilerInvariant(
			inferredFixedArgumentCount >= 0 && static_cast<size_t>(inferredFixedArgumentCount) <= callArgs.size(),
			"variadic call reached codegen with an invalid fixed argument count"
		);
		fixedArgumentCount = static_cast<size_t>(inferredFixedArgumentCount);
		for (size_t argumentIndex = fixedArgumentCount; argumentIndex < callArgs.size(); argumentIndex++) {
			DataType &argumentType = callArgumentTypes[argumentIndex];
			DataType promotedType = argumentType;
			if (!argumentType.isPointer()) {
				if (argumentType.kind == DataType::Kind::Float && argumentType.numericSize < 8)
					promotedType = {DataType::Kind::Float, 8};
				else if (argumentType.kind == DataType::Kind::Bool ||
						 (argumentType.kind == DataType::Kind::Int && argumentType.numericSize < 4))
					promotedType = {DataType::Kind::Int, 4};
			}
			if (promotedType != argumentType) {
				callArgs[argumentIndex] = ensureType(context, callArgs[argumentIndex], argumentType, promotedType);
				argumentType = promotedType;
			}
		}
	}
	std::vector<llvm::Type *> fixedArgumentTypes;
	fixedArgumentTypes.reserve(fixedArgumentCount);
	for (size_t argumentIndex = 0; argumentIndex < fixedArgumentCount; argumentIndex++)
		fixedArgumentTypes.push_back(callArgs[argumentIndex]->getType());
	llvm::FunctionType *funcType = llvm::FunctionType::get(returnLLVMType, fixedArgumentTypes, isVariadic);
	auto fixedTypes = std::span<const DataType>(callArgumentTypes).first(fixedArgumentCount);
	llvm::CallInst *callResult = nullptr;
	if (isPointerCall) {
		callResult = builder.CreateCall(funcType, pointerCallee, callArgs);
	} else {
		llvm::FunctionCallee callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		applyNativeScalarABI(
			*llvm::cast<llvm::Function>(callee.getCallee()), context.llvmModule->getTargetTriple(), returnType, fixedTypes
		);
		callResult = builder.CreateCall(callee, callArgs);
	}
	applyNativeScalarABI(*callResult, context.llvmModule->getTargetTriple(), returnType, fixedTypes);
	for (auto iterator = ownedManagedArguments.rbegin(); iterator != ownedManagedArguments.rend(); iterator++)
		if (!releaseManagedValue(context, iterator->first, iterator->second))
			return CodegenResult::failure();
	// If return type is void, return nullptr (no value to use)
	if (returnType.kind == DataType::Kind::Void)
		return nullptr;
	return callResult;
}

[[gnu::noinline]] static CodegenResult generateFunctionIntrinsic(ParseContext &context, Expression *callExpr) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	requireCompilerInvariant(callExpr, "function intrinsic reached codegen without its call expression");
	PatternDefinition *definition = callExpr->selectedCallableDefinition;
	requireCompilerInvariant(definition, "function intrinsic reached codegen without its inferred callable definition");
	requireCompilerInvariant(
		callExpr->selectedCallablePathIndex.has_value(), "function intrinsic reached codegen without its inferred callable path"
	);
	requireCompilerInvariant(callExpr->selectedInstantiation, "function intrinsic reached codegen without its instantiation");
	llvm::Function *callableFunction = nullptr;
	if (!ensureCallableFunctionGenerated(
			context, {definition, *callExpr->selectedCallablePathIndex}, *callExpr->selectedInstantiation,
			definition->section->isExposed, callableFunction
		))
		return CodegenResult::failure();
	if (callableFunction->getType() == builder.getPtrTy())
		return callableFunction;
	return builder.CreateBitCast(callableFunction, builder.getPtrTy(), "function_ptr");
}
