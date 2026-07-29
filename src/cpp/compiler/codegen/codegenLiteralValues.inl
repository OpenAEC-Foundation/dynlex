static llvm::Value *generateStringConstant(ParseContext &context, const std::string &value) {
	// Strings are currently i8* pointers to constant data. Runtime string
	// operations remain the responsibility of the string library.
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	auto it = context.stringConstants.find(value);
	if (it != context.stringConstants.end()) {
		llvm::GlobalVariable *strGlobal = it->second;
		return builder.CreateInBoundsGEP(
			strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
		);
	}
	std::string globalName = ".str." + std::to_string(context.stringConstants.size());
	llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, value, true);
	llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
		*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
	);
	context.stringConstants[value] = strGlobal;
	return builder.CreateInBoundsGEP(
		strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
	);
}

static llvm::Value *
generateCompileTimeRuntimeValue(ParseContext &context, const CompileTimeValue &value, const DataType &type) {
	requireCompilerInvariant(type.isRuntimeValueType(), "compile-time-only type reached runtime value codegen");
	llvm::Type *llvmType = getLLVMType(context, type);
	if (const auto *integer = std::get_if<std::int64_t>(&value)) {
		if (type.kind == DataType::Kind::Int)
			return llvm::ConstantInt::get(llvmType, *integer, true);
		if (type.kind == DataType::Kind::Float)
			return llvm::ConstantFP::get(llvmType, static_cast<double>(*integer));
	}
	if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&value)) {
		requireCompilerInvariant(
			minimumMagnitude->identity != nullptr, "minimum integer magnitude reached runtime codegen without an identity"
		);
		requireCompilerInvariant(
			type.kind == DataType::Kind::Int && type.numericSize == 8, "minimum integer magnitude is not i64"
		);
		return llvm::ConstantInt::get(llvmType, std::numeric_limits<std::int64_t>::min(), true);
	}
	if (const auto *number = std::get_if<double>(&value)) {
		if (type.kind == DataType::Kind::Int)
			return llvm::ConstantInt::get(llvmType, static_cast<int64_t>(*number), true);
		if (type.kind == DataType::Kind::Float)
			return llvm::ConstantFP::get(llvmType, *number);
	}
	if (const auto *boolean = std::get_if<bool>(&value)) {
		requireCompilerInvariant(type.kind == DataType::Kind::Bool, "boolean compile-time value has non-boolean runtime type");
		return llvm::ConstantInt::get(llvmType, *boolean ? 1 : 0);
	}
	if (const auto *text = std::get_if<std::string>(&value)) {
		requireCompilerInvariant(type.isBytePointer(), "string compile-time value has non-string runtime type");
		return generateStringConstant(context, *text);
	}
	crashCompilerBug("compile-time parameter value cannot be represented at runtime");
}
