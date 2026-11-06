[[gnu::noinline]] static CodegenResult generateCommandLineArgumentsIntrinsic(ParseContext &context, IntrinsicKind kind) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::GlobalVariable *global = kind == IntrinsicKind::CommandLineArgumentCount ? context.commandLineArgumentCountGlobal
																				   : context.commandLineArgumentValuesGlobal;
	requireCompilerInvariant(global != nullptr, "command-line argument intrinsic reached codegen without native ABI state");
	return builder.CreateLoad(global->getValueType(), global, global->getName() + ".value");
}

[[gnu::noinline]] static CodegenResult
generateSetSubjectIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType valueType = finalizedExpressionType(context, args[1]);
	if (!valueType.isDeduced() || valueType.kind == DataType::Kind::Void)
		crashCompilerBug("subject assignment reached codegen without a runtime value type");
	if (valueType.isMetaType())
		return nullptr;
	CodegenResult generatedValue = generateExpressionCode(context, args[1]);
	if (!generatedValue)
		return generatedValue;
	llvm::Value *value = generatedValue.value;
	requireCompilerInvariant(value != nullptr, "subject assignment reached codegen without a runtime value");
	llvm::AllocaInst *storage = createEntryAlloca(context, "subject", valueType);
	if (typeHasManagedLifecycle(valueType)) {
		Section *ownerSection = currentFunctionLifetimeSection(context);
		requireCompilerInvariant(ownerSection != nullptr, "managed subject assignment has no owning function section");
		registerManagedStorage(context, storage, valueType, ownerSection);
		if (!managedExpressionResultIsOwned(context, args[1]) && !retainManagedValue(context, valueType, value))
			return CodegenResult::failure();
		if (!storeManagedValue(context, storage, valueType, value))
			return CodegenResult::failure();
	} else {
		builder.CreateStore(value, storage);
	}
	context.subjectStorage[callExpr] = storage;
	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateSubjectIntrinsic(ParseContext &context, Expression *callExpr, const DataType &resultType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (!callExpr || !callExpr->subjectSetter)
		crashCompilerBug("subject read reached codegen without an inferred assignment");
	if (resultType.isMetaType())
		return nullptr;
	auto storage = context.subjectStorage.find(callExpr->subjectSetter);
	if (storage == context.subjectStorage.end())
		crashCompilerBug("subject read reached codegen before its inferred assignment");
	llvm::Value *value = builder.CreateLoad(getLLVMType(context, resultType), storage->second, "subject");
	if (typeHasManagedLifecycle(resultType) && !retainManagedValue(context, resultType, value))
		return CodegenResult::failure();
	return value;
}

[[gnu::noinline]] static CodegenResult generateDiscardIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	// Evaluate the argument for side effects and discard the result
	CodegenResult generatedValue = generateExpressionCode(context, args[1]);
	if (!generatedValue)
		return generatedValue;
	llvm::Value *value = generatedValue.value;
	DataType valueType = finalizedExpressionType(context, args[1]);
	if (managedExpressionResultIsOwned(context, args[1]) && !releaseManagedValue(context, valueType, value))
		return CodegenResult::failure();
	return nullptr;
}

[[gnu::noinline]] static CodegenResult generateStoreIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType valType = finalizedExpressionType(context, args[2]);
	if (valType.isMetaType())
		return nullptr;
	CodegenResult generatedValue = generateExpressionCode(context, args[2]);
	if (!generatedValue)
		return generatedValue;
	llvm::Value *val = generatedValue.value;
	requireCompilerInvariant(val != nullptr, "store value reached codegen without a generated value");

	LValueAddressResult destination = generateLValueAddress(context, args[1]);
	if (destination.status == LValueAddressStatus::Failed)
		return CodegenResult::failure();
	requireCompilerInvariant(
		destination.status == LValueAddressStatus::Addressable && destination.address != nullptr,
		"store destination reached codegen without addressable storage"
	);

	llvm::Value *ptr = destination.address;
	DataType destType = finalizedExpressionType(context, args[1]);
	if (typeHasManagedLifecycle(destType)) {
		val = ensureType(context, val, valType, destType);
		if (!managedExpressionResultIsOwned(context, args[2]) && !retainManagedValue(context, destType, val))
			return CodegenResult::failure();
		if (!storeManagedValue(context, ptr, destType, val))
			return CodegenResult::failure();
	} else {
		val = ensureType(context, val, valType, destType);
		builder.CreateAlignedStore(val, ptr, getLLVMABIAlignment(context, destType));
	}
	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateAddressOfIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	LValueAddressResult lvalue = generateLValueAddress(context, args[1]);
	if (lvalue.status == LValueAddressStatus::Failed)
		return CodegenResult::failure();
	if (lvalue.status == LValueAddressStatus::NotAddressable) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "address of requires addressable value",
			intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}
	requireCompilerInvariant(lvalue.address != nullptr, "addressable lvalue produced no address");
	return lvalue.address;
}

[[gnu::noinline]] static CodegenResult
generateDereferenceIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *ptrVal = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], ptrVal))
		return CodegenResult::failure();
	DataType ptrType = finalizedExpressionType(context, args[1]);
	DataType elemType = ptrType.dereferenced();
	llvm::Type *elemLLVMType = getLLVMType(context, elemType);
	return builder.CreateAlignedLoad(elemLLVMType, ptrVal, getLLVMABIAlignment(context, elemType), "deref");
}

[[gnu::noinline]] static CodegenResult
generatePointerStoreIntrinsic(ParseContext &context, const std::vector<Expression *> &args, IntrinsicKind kind) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *ptr = nullptr;
	llvm::Value *value = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], ptr) || !generateIntrinsicRuntimeValue(context, args[2], value))
		return CodegenResult::failure();
	DataType ptrType = finalizedExpressionType(context, args[1]);
	requireCompilerInvariant(ptrType.isPointer(), "pointer store reached codegen with a non-pointer destination");
	DataType elementType = ptrType.dereferenced();
	value = ensureType(context, value, finalizedExpressionType(context, args[2]), elementType);
	if (typeHasManagedLifecycle(elementType)) {
		if (!managedExpressionResultIsOwned(context, args[2]) && !retainManagedValue(context, elementType, value))
			return CodegenResult::failure();
		if (kind == IntrinsicKind::StoreAt) {
			if (!storeManagedValue(context, ptr, elementType, value))
				return CodegenResult::failure();
		} else {
			builder.CreateAlignedStore(value, ptr, getLLVMABIAlignment(context, elementType));
		}
	} else {
		builder.CreateAlignedStore(value, ptr, getLLVMABIAlignment(context, elementType));
	}
	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateDestroyAtIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *ptr = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], ptr))
		return CodegenResult::failure();
	DataType ptrType = finalizedExpressionType(context, args[1]);
	requireCompilerInvariant(ptrType.isPointer(), "destroy at reached codegen with a non-pointer destination");
	DataType elementType = ptrType.dereferenced();
	if (typeHasManagedLifecycle(elementType)) {
		llvm::Value *value = builder.CreateAlignedLoad(
			getLLVMType(context, elementType), ptr, getLLVMABIAlignment(context, elementType), "destroy_value"
		);
		if (!releaseManagedValue(context, elementType, value))
			return CodegenResult::failure();
	}
	return nullptr;
}
