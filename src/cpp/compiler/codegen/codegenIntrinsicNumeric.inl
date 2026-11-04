[[gnu::noinline]] static CodegenResult generateArithmeticIntrinsic(
	ParseContext &context, const std::vector<Expression *> &args, const DataType &resultType,
	ArithmeticIntrinsicKind arithmeticOp
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType leftType = finalizedExpressionType(context, args[1]);
	DataType rightType = finalizedExpressionType(context, args[2]);
	int arrayOperandIndex = decayingArrayOperandIndex(arithmeticOp, leftType, rightType);
	if (arrayOperandIndex != 0) {
		int indexOperandIndex = arrayOperandIndex == 1 ? 2 : 1;
		LValueAddressResult arrayStorage = generateLValueAddress(context, args[arrayOperandIndex]);
		if (arrayStorage.status == LValueAddressStatus::Failed)
			return CodegenResult::failure();
		requireCompilerInvariant(
			arrayStorage.status == LValueAddressStatus::Addressable && arrayStorage.address != nullptr,
			"fixed-array decay reached codegen without addressable storage"
		);

		llvm::Value *indexValue = nullptr;
		if (!generateIntrinsicRuntimeValue(context, args[indexOperandIndex], indexValue))
			return CodegenResult::failure();
		DataType indexType = finalizedExpressionType(context, args[indexOperandIndex]);
		indexValue = coerceIndexToSizeT(context, indexValue, indexType);
		if (arithmeticOp == ArithmeticIntrinsicKind::Subtract)
			indexValue = builder.CreateNeg(indexValue, "neg_idx");

		DataType arrayType = finalizedExpressionType(context, args[arrayOperandIndex]);
		requireCompilerInvariant(
			isFixedArrayValue(arrayType) && arrayType.arrayElementType,
			"fixed-array decay reached codegen without an element type"
		);
		return builder.CreateGEP(
			getLLVMType(context, arrayType), arrayStorage.address, {builder.getInt64(0), indexValue}, "array_decay"
		);
	}

	llvm::Value *left = nullptr;
	llvm::Value *right = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], left) || !generateIntrinsicRuntimeValue(context, args[2], right))
		return CodegenResult::failure();

	// Pointer arithmetic: ptr +/- integer → GEP
	if (isPointerArithmeticIntrinsic(arithmeticOp) && (leftType.isPointer() || rightType.isPointer())) {
		llvm::Value *ptrVal = leftType.isPointer() ? left : right;
		llvm::Value *indexVal = leftType.isPointer() ? right : left;
		DataType indexType = leftType.isPointer() ? rightType : leftType;
		DataType ptrType = leftType.isPointer() ? leftType : rightType;
		llvm::Type *elemType = getLLVMType(context, ptrType.dereferenced());
		indexVal = coerceIndexToSizeT(context, indexVal, indexType);
		if (arithmeticOp == ArithmeticIntrinsicKind::Subtract && leftType.isPointer())
			indexVal = builder.CreateNeg(indexVal, "neg_idx");
		return builder.CreateGEP(elemType, ptrVal, indexVal, "ptr_arith");
	}

	if (leftType.kind == DataType::Kind::Matrix && rightType.kind == DataType::Kind::Vector &&
		arithmeticOp == ArithmeticIntrinsicKind::Multiply)
		return generateMatrixVectorMultiply(context, left, leftType, right, rightType);
	if (leftType.kind == DataType::Kind::Vector && rightType.kind == DataType::Kind::Matrix &&
		arithmeticOp == ArithmeticIntrinsicKind::Multiply) {
		// Treat row-vector * matrix as transpose-compatible multiply by transposing the operand order.
		return generateMatrixVectorMultiply(context, right, rightType, left, leftType);
	}
	if (leftType.kind == DataType::Kind::Matrix && rightType.kind == DataType::Kind::Matrix &&
		arithmeticOp == ArithmeticIntrinsicKind::Multiply)
		return generateMatrixMatrixMultiply(context, left, leftType, right, rightType);

	left = ensureType(context, left, leftType, resultType);
	right = ensureType(context, right, rightType, resultType);
	return generateScalarOrVectorArithmetic(context, arithmeticOp, left, right, resultType);
}

[[gnu::noinline]] static CodegenResult
generateBitwiseNotIntrinsic(ParseContext &context, const std::vector<Expression *> &args, const DataType &resultType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *value = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], value))
		return CodegenResult::failure();
	DataType valueType = finalizedExpressionType(context, args[1]);
	value = ensureType(context, value, valueType, resultType);
	return builder.CreateNot(value, "bnot");
}

[[gnu::noinline]] static CodegenResult generateBinaryBitwiseIntrinsic(
	ParseContext &context, const std::vector<Expression *> &args, const DataType &resultType, IntrinsicKind kind
) {
	llvm::Value *left = nullptr;
	llvm::Value *right = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], left) || !generateIntrinsicRuntimeValue(context, args[2], right))
		return CodegenResult::failure();
	DataType leftType = finalizedExpressionType(context, args[1]);
	DataType rightType = finalizedExpressionType(context, args[2]);

	left = ensureType(context, left, leftType, resultType);
	right = ensureType(context, right, rightType, resultType);
	return generateScalarBitwise(context, kind, left, right, resultType);
}

[[gnu::noinline]] static CodegenResult generateComparisonIntrinsic(
	ParseContext &context, const std::vector<Expression *> &args, const DataType &resultType, IntrinsicKind kind
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *left = nullptr;
	llvm::Value *right = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], left) || !generateIntrinsicRuntimeValue(context, args[2], right))
		return CodegenResult::failure();
	DataType leftType = finalizedExpressionType(context, args[1]);
	DataType rightType = finalizedExpressionType(context, args[2]);

	llvm::Value *cmp;
	if ((kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) && leftType.isAddress() && rightType.isAddress()) {
		DataType comparisonType = leftType;
		if (ClassDefinition::typeStructurallyRefines(rightType, leftType))
			comparisonType = rightType;
		left = ensureType(context, left, leftType, comparisonType);
		right = ensureType(context, right, rightType, comparisonType);
		cmp =
			kind == IntrinsicKind::Equal ? builder.CreateICmpEQ(left, right, "peq") : builder.CreateICmpNE(left, right, "pne");
	} else {
		DataType promoted;
		bool equality = kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual;
		bool promotable = equality ? DataType::promoteEquality(leftType, rightType, promoted)
								   : DataType::promoteArithmetic(leftType, rightType, promoted);
		requireCompilerInvariant(promotable, "comparison operands accepted by inference have no common codegen type");
		left = ensureType(context, left, leftType, promoted);
		right = ensureType(context, right, rightType, promoted);
		if (promoted.kind == DataType::Kind::Float) {
			if (kind == IntrinsicKind::LessThan)
				cmp = builder.CreateFCmpOLT(left, right, "flt");
			else if (kind == IntrinsicKind::LessThanOrEqual)
				cmp = builder.CreateFCmpOLE(left, right, "fle");
			else if (kind == IntrinsicKind::GreaterThan)
				cmp = builder.CreateFCmpOGT(left, right, "fgt");
			else if (kind == IntrinsicKind::GreaterThanOrEqual)
				cmp = builder.CreateFCmpOGE(left, right, "fge");
			else if (kind == IntrinsicKind::Equal)
				cmp = builder.CreateFCmpOEQ(left, right, "feq");
			else
				cmp = builder.CreateFCmpONE(left, right, "fne");
		} else {
			DataType comparisonElementType = promoted.numericElementType();
			if (kind == IntrinsicKind::LessThan)
				cmp = comparisonElementType.isUnsignedInteger() ? builder.CreateICmpULT(left, right, "lt")
																: builder.CreateICmpSLT(left, right, "lt");
			else if (kind == IntrinsicKind::LessThanOrEqual)
				cmp = comparisonElementType.isUnsignedInteger() ? builder.CreateICmpULE(left, right, "le")
																: builder.CreateICmpSLE(left, right, "le");
			else if (kind == IntrinsicKind::GreaterThan)
				cmp = comparisonElementType.isUnsignedInteger() ? builder.CreateICmpUGT(left, right, "gt")
																: builder.CreateICmpSGT(left, right, "gt");
			else if (kind == IntrinsicKind::GreaterThanOrEqual)
				cmp = comparisonElementType.isUnsignedInteger() ? builder.CreateICmpUGE(left, right, "ge")
																: builder.CreateICmpSGE(left, right, "ge");
			else if (kind == IntrinsicKind::Equal)
				cmp = builder.CreateICmpEQ(left, right, "eq");
			else
				cmp = builder.CreateICmpNE(left, right, "ne");
		}
	}

	requireCompilerInvariant(resultType.isDeduced(), "Comparison result type must be deduced before codegen");
	if (resultType.kind == DataType::Kind::Bool)
		return cmp; // already i1
	return builder.CreateZExt(cmp, getLLVMType(context, resultType), "cmp_ext");
}

[[gnu::noinline]] static CodegenResult generateNotIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *val = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], val))
		return CodegenResult::failure();
	DataType valType = finalizedExpressionType(context, args[1]);
	if (valType.kind != DataType::Kind::Bool)
		crashCompilerBug("logical not operand must be boolean after type inference");

	return builder.CreateXor(val, builder.getTrue(), "not");
}

[[gnu::noinline]] static CodegenResult generateNegateIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *val = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], val))
		return CodegenResult::failure();
	DataType valType = finalizedExpressionType(context, args[1]);
	DataType elementType = valType.numericElementType();
	if (!valType.isNumeric() && !valType.isVector())
		crashCompilerBug("negate reached code generation with a non-numeric operand");
	if (!elementType.isNumeric())
		crashCompilerBug("negate reached code generation with a non-numeric element type");
	if (elementType.kind == DataType::Kind::Float)
		return builder.CreateFNeg(val, "fneg");
	return builder.CreateNeg(val, "neg");
}

[[gnu::noinline]] static CodegenResult generateMinMaxIntrinsic(
	ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args, IntrinsicKind kind
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *left = nullptr;
	llvm::Value *right = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], left) || !generateIntrinsicRuntimeValue(context, args[2], right))
		return CodegenResult::failure();
	DataType leftType = finalizedExpressionType(context, args[1]);
	DataType rightType = finalizedExpressionType(context, args[2]);
	DataType promoted;
	if (!DataType::promoteArithmetic(leftType, rightType, promoted)) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "min max requires arithmetic operands",
			intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}
	left = ensureType(context, left, leftType, promoted);
	right = ensureType(context, right, rightType, promoted);

	if (promoted.kind == DataType::Kind::Float) {
		llvm::Intrinsic::ID intrinsicId = kind == IntrinsicKind::Min ? llvm::Intrinsic::minnum : llvm::Intrinsic::maxnum;
		llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, intrinsicId, {left->getType()});
		return builder.CreateCall(fn, {left, right}, kind == IntrinsicKind::Min ? "fmin" : "fmax");
	}

	bool unsignedElements = promoted.numericElementType().isUnsignedInteger();
	llvm::Value *cmp = kind == IntrinsicKind::Min ? (unsignedElements ? builder.CreateICmpULT(left, right, "min_cmp")
																	  : builder.CreateICmpSLT(left, right, "min_cmp"))
												  : (unsignedElements ? builder.CreateICmpUGT(left, right, "max_cmp")
																	  : builder.CreateICmpSGT(left, right, "max_cmp"));
	return builder.CreateSelect(cmp, left, right, kind == IntrinsicKind::Min ? "min" : "max");
}

[[gnu::noinline]] static CodegenResult generateMathIntrinsic(
	ParseContext &context, const std::string &name, const std::vector<Expression *> &args, const DataType &resultType,
	IntrinsicKind kind
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	context.requiredLibraries.insert("m");
	llvm::Intrinsic::ID intrinsicId = mathIntrinsicId(kind);
	if (intrinsicId != llvm::Intrinsic::not_intrinsic) {
		DataType operandType = args.size() == 2 ? finalizedExpressionType(context, args[1]) : DataType{};
		DataType elementType = operandType.numericElementType();
		if ((isRoundingIntrinsicKind(kind) && elementType.isInteger()) ||
			(kind == IntrinsicKind::Abs && elementType.isUnsignedInteger())) {
			llvm::Value *value = nullptr;
			if (!generateIntrinsicRuntimeValue(context, args[1], value))
				return CodegenResult::failure();
			return value;
		}
		// GLSL.std.450 extended instructions (used by SPIR-V) only support 16/32-bit floats.
		// Native LLVM math intrinsics retain the inferred 32/64-bit floating-point width.
		int maximumFloatBytes = defaultFloatByteSize(context.options.emitSPIRV);
		DataType computationType = mathComputationType(resultType, maximumFloatBytes);
		if (args.size() == 2) {
			llvm::Value *val = nullptr;
			if (!generateIntrinsicRuntimeValue(context, args[1], val))
				return CodegenResult::failure();
			DataType valType = finalizedExpressionType(context, args[1]);
			if (valType != computationType)
				val = ensureType(context, val, valType, computationType);
			llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, intrinsicId, {val->getType()});
			llvm::Value *computed = builder.CreateCall(fn, {val}, name);
			return ensureType(context, computed, computationType, resultType);
		}
		llvm::Value *left = nullptr;
		llvm::Value *right = nullptr;
		if (!generateIntrinsicRuntimeValue(context, args[1], left) || !generateIntrinsicRuntimeValue(context, args[2], right))
			return CodegenResult::failure();
		DataType leftType = finalizedExpressionType(context, args[1]);
		DataType rightType = finalizedExpressionType(context, args[2]);
		left = ensureType(context, left, leftType, computationType);
		right = ensureType(context, right, rightType, computationType);
		llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, intrinsicId, {left->getType()});
		llvm::Value *computed = builder.CreateCall(fn, {left, right}, name);
		return ensureType(context, computed, computationType, resultType);
	}

	// atan2: no LLVM intrinsic, call libm
	if (kind == IntrinsicKind::Atan2) {
		llvm::Value *y = nullptr;
		llvm::Value *x = nullptr;
		if (!generateIntrinsicRuntimeValue(context, args[1], y) || !generateIntrinsicRuntimeValue(context, args[2], x))
			return CodegenResult::failure();
		DataType yType = finalizedExpressionType(context, args[1]);
		DataType xType = finalizedExpressionType(context, args[2]);
		DataType promoted;
		DataType::promoteArithmetic(yType, xType, promoted);
		promoted = mathComputationType(promoted, defaultFloatByteSize(context.options.emitSPIRV));
		y = ensureType(context, y, yType, promoted);
		x = ensureType(context, x, xType, promoted);
		llvm::Type *floatType = getLLVMType(context, promoted);
		llvm::FunctionType *ft = llvm::FunctionType::get(floatType, {floatType, floatType}, false);
		const char *fnName = promoted.numericSize == 4 ? "atan2f" : "atan2";
		llvm::FunctionCallee callee = context.llvmModule->getOrInsertFunction(fnName, ft);
		llvm::Value *computed = builder.CreateCall(callee, {y, x}, "atan2");
		return ensureType(context, computed, promoted, resultType);
	}

	return nullptr;
}
