[[gnu::noinline]] static CodegenResult generateCastIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	// Format: args[1]=value, args[2]=type (TypeReference)
	llvm::Value *val = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], val))
		return CodegenResult::failure();
	DataType fromType = finalizedExpressionType(context, args[1]);

	// Get target type from the TypeReference argument
	DataType typeArgType = finalizedExpressionType(context, args[2]);
	if (typeArgType.kind != DataType::Kind::Type)
		return nullptr; // unresolved type (e.g. spurious top-level instantiation)
	DataType toType = typeArgType.toReferencedType();

	return ensureType(context, val, fromType, toType);
}

[[gnu::noinline]] static CodegenResult generateSizeOfIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType typeArgType = finalizedExpressionType(context, args[1]);
	if (typeArgType.kind != DataType::Kind::Type)
		return nullptr;
	DataType valueType = typeArgType.toReferencedType();
	requireCompilerInvariant(valueType.isConcrete(), "size of reached codegen with a non-concrete type");
	return builder.getInt64(valueType.getByteSize(context.llvmModule->getDataLayout(), *context.llvmContext));
}

[[gnu::noinline]] static CodegenResult
generateCompileTimeResultIntrinsic(ParseContext &context, Expression *callExpr, const DataType &resultType) {
	CompileTimeValue value = resolveStoredCompileTimeValue(callExpr, context.flexBindingFrames);
	if (auto *integer = std::get_if<std::int64_t>(&value)) {
		llvm::Type *llvmType = getLLVMType(context, resultType);
		return llvm::ConstantInt::get(llvmType, *integer, true);
	}
	if (auto *boolean = std::get_if<bool>(&value)) {
		llvm::Type *llvmType = getLLVMType(context, resultType);
		return llvm::ConstantInt::get(llvmType, *boolean ? 1 : 0, false);
	}
	return nullptr;
}
