#include "arithmeticTypePromotion.h"
#include "classDefinition.h"
#include "codegenInternal.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "intrinsicInfo.h"
#include "sectionFlexBody.h"
#include "spirv.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

std::string getCompileTimeString(ParseContext &context, Expression *expr) {
	requireCompilerInvariant(expr != nullptr, "compile-time string codegen received a null expression");
	CompileTimeValue value = resolveStoredCompileTimeValue(expr, context.flexBindingFrames);
	const auto *text = std::get_if<std::string>(&value);
	requireCompilerInvariant(text != nullptr, "compile-time string argument reached codegen without its inferred value");
	return *text;
}

// Diagnostics for intrinsics inside flex replacement bodies should point at
// the caller's line, not the library definition of the flex.
static Range intrinsicDiagnosticRange(ParseContext &context, Expression *callExpr) {
	if (!context.flexCallSiteRangeStack.empty())
		return context.flexCallSiteRangeStack.back();
	return callExpr ? callExpr->range : Range();
}

static llvm::Value *coerceIndexToSizeT(ParseContext &context, llvm::Value *indexVal, DataType indexType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *sizeTy = builder.getInt64Ty();

	if (!indexVal)
		return nullptr;
	if (indexVal->getType() == sizeTy)
		return indexVal;
	if (indexType.kind == DataType::Kind::Float)
		return builder.CreateFPToSI(indexVal, sizeTy, "idx_size");
	if (indexType.kind == DataType::Kind::Bool)
		return builder.CreateZExt(indexVal, sizeTy, "idx_size");
	if (indexType.kind == DataType::Kind::Int)
		return ensureType(context, indexVal, indexType, {DataType::Kind::Int, 8});
	return indexVal;
}

static CodegenResult buildRuntimeSelect(ParseContext &context, const std::vector<Expression *> &args, DataType resultType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *function = builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
	if (!function)
		return nullptr;

	DataType conditionType = finalizedExpressionType(context, args[1]);
	if (conditionType.kind != DataType::Kind::Bool)
		crashCompilerBug("runtime select condition must be boolean after type inference");
	CodegenResult condition = generateExpressionCode(context, args[1]);
	if (!condition)
		return condition;
	llvm::Value *conditionValue = condition.value;
	requireCompilerInvariant(conditionValue != nullptr, "runtime select condition produced no value");

	if (resultType.isMetaType()) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "compile time type value used at runtime", args[1]->range)
		);
		return CodegenResult::failure();
	}

	llvm::BasicBlock *trueBlock = llvm::BasicBlock::Create(*context.llvmContext, "select.true", function);
	llvm::BasicBlock *falseBlock = llvm::BasicBlock::Create(*context.llvmContext, "select.false", function);
	llvm::BasicBlock *mergeBlock = llvm::BasicBlock::Create(*context.llvmContext, "select.merge", function);
	builder.CreateCondBr(conditionValue, trueBlock, falseBlock);

	struct PhiIncoming {
		llvm::Value *value;
		llvm::BasicBlock *block;
	};
	std::vector<PhiIncoming> incomingValues;
	incomingValues.reserve(2);

	builder.SetInsertPoint(trueBlock);
	CodegenResult generatedTrue = generateExpressionCode(context, args[2]);
	if (!generatedTrue)
		return generatedTrue;
	llvm::Value *trueValue = generatedTrue.value;
	llvm::BasicBlock *trueEndBlock = builder.GetInsertBlock();
	if (!trueEndBlock->hasTerminator()) {
		if (resultType.kind != DataType::Kind::Void) {
			DataType trueType = finalizedExpressionType(context, args[2]);
			trueValue = ensureType(context, trueValue, trueType, resultType);
			if (typeHasManagedLifecycle(resultType) && !managedExpressionResultIsOwned(context, args[2]) &&
				!retainManagedValue(context, resultType, trueValue))
				return CodegenResult::failure();
			incomingValues.push_back({trueValue, trueEndBlock});
		}
		builder.CreateBr(mergeBlock);
	}

	builder.SetInsertPoint(falseBlock);
	CodegenResult generatedFalse = generateExpressionCode(context, args[3]);
	if (!generatedFalse)
		return generatedFalse;
	llvm::Value *falseValue = generatedFalse.value;
	llvm::BasicBlock *falseEndBlock = builder.GetInsertBlock();
	if (!falseEndBlock->hasTerminator()) {
		if (resultType.kind != DataType::Kind::Void) {
			DataType falseType = finalizedExpressionType(context, args[3]);
			falseValue = ensureType(context, falseValue, falseType, resultType);
			if (typeHasManagedLifecycle(resultType) && !managedExpressionResultIsOwned(context, args[3]) &&
				!retainManagedValue(context, resultType, falseValue))
				return CodegenResult::failure();
			incomingValues.push_back({falseValue, falseEndBlock});
		}
		builder.CreateBr(mergeBlock);
	}

	builder.SetInsertPoint(mergeBlock);
	if (resultType.kind == DataType::Kind::Void || incomingValues.empty())
		return nullptr;

	auto *phi = builder.CreatePHI(getLLVMType(context, resultType), static_cast<unsigned>(incomingValues.size()), "select");
	for (const PhiIncoming &incoming : incomingValues)
		phi->addIncoming(incoming.value, incoming.block);
	return phi;
}

static CodegenResult
buildVectorValue(ParseContext &context, DataType vectorType, const std::vector<Expression *> &args, size_t startIndex) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *llvmVectorType = getLLVMType(context, vectorType);
	llvm::Value *vectorValue = llvm::Constant::getNullValue(llvmVectorType);
	DataType elementType = vectorType.vectorElementType();
	for (int lane = 0; lane < vectorType.vectorSize(); lane++) {
		CodegenResult element = generateExpressionCode(context, args[startIndex + lane]);
		if (!element)
			return element;
		llvm::Value *elementValue = element.value;
		DataType fromType = finalizedExpressionType(context, args[startIndex + lane]);
		elementValue = ensureType(context, elementValue, fromType, elementType);
		vectorValue = builder.CreateInsertElement(vectorValue, elementValue, getVectorLaneIndexValue(context, lane), "vec_ins");
	}
	return vectorValue;
}

static CodegenResult buildMatrixFromFlatArray(ParseContext &context, DataType matrixType, Expression *sourceExpr) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType sourceType = finalizedExpressionType(context, sourceExpr);
	if (sourceType.kind != DataType::Kind::Array || !sourceType.arrayElementType)
		return nullptr;
	if (sourceType.arraySize != matrixType.matrixRows() * matrixType.matrixColumns())
		return nullptr;

	CodegenResult flatArray = generateExpressionCode(context, sourceExpr);
	if (!flatArray)
		return flatArray;
	llvm::Value *flatArrayValue = flatArray.value;
	llvm::Type *llvmFlatArrayType = getLLVMType(context, sourceType);
	llvm::AllocaInst *flatAlloca = createEntryAlloca(context, "matrix_flat", sourceType);
	builder.CreateAlignedStore(flatArrayValue, flatAlloca, getLLVMABIAlignment(context, sourceType));

	llvm::Type *llvmMatrixType = getLLVMType(context, matrixType);
	llvm::Value *matrixValue = llvm::Constant::getNullValue(llvmMatrixType);
	DataType elementType = matrixType.matrixElementType();
	DataType rowVectorType{DataType::Kind::Vector};
	rowVectorType.arraySize = matrixType.matrixColumns();
	rowVectorType.arrayElementType = std::make_shared<DataType>(elementType);

	for (int row = 0; row < matrixType.matrixRows(); row++) {
		llvm::Value *rowValue = llvm::Constant::getNullValue(getLLVMType(context, rowVectorType));
		for (int column = 0; column < matrixType.matrixColumns(); column++) {
			int flatIndex = row * matrixType.matrixColumns() + column;
			llvm::Value *elementPtr = builder.CreateGEP(
				llvmFlatArrayType, flatAlloca, {builder.getInt64(0), builder.getInt64(flatIndex)}, "mat_elem_ptr"
			);
			llvm::Value *elementValue = builder.CreateLoad(getLLVMType(context, *sourceType.arrayElementType), elementPtr);
			elementValue = ensureType(context, elementValue, *sourceType.arrayElementType, elementType);
			rowValue =
				builder.CreateInsertElement(rowValue, elementValue, getVectorLaneIndexValue(context, column), "mat_row_ins");
		}
		matrixValue = builder.CreateInsertValue(matrixValue, rowValue, {static_cast<unsigned>(row)}, "mat_ins");
	}

	return matrixValue;
}

static CodegenResult buildMatrixFromScalarArgs(
	ParseContext &context, DataType matrixType, const std::vector<Expression *> &args, size_t startIndex
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *matrixValue = llvm::Constant::getNullValue(getLLVMType(context, matrixType));
	DataType elementType = matrixType.matrixElementType();
	DataType rowVectorType{DataType::Kind::Vector};
	rowVectorType.arraySize = matrixType.matrixColumns();
	rowVectorType.arrayElementType = std::make_shared<DataType>(elementType);
	size_t argIndex = startIndex;
	for (int row = 0; row < matrixType.matrixRows(); row++) {
		llvm::Value *rowValue = llvm::Constant::getNullValue(getLLVMType(context, rowVectorType));
		for (int column = 0; column < matrixType.matrixColumns(); column++) {
			CodegenResult element = generateExpressionCode(context, args[argIndex]);
			if (!element)
				return element;
			llvm::Value *elementValue = element.value;
			DataType fromType = finalizedExpressionType(context, args[argIndex]);
			elementValue = ensureType(context, elementValue, fromType, elementType);
			rowValue =
				builder.CreateInsertElement(rowValue, elementValue, getVectorLaneIndexValue(context, column), "mat_row_ins");
			argIndex++;
		}
		matrixValue = builder.CreateInsertValue(matrixValue, rowValue, {static_cast<unsigned>(row)}, "mat_ins");
	}
	return matrixValue;
}

static llvm::Value *generateMatrixVectorMultiply(
	ParseContext &context, llvm::Value *matrixValue, DataType matrixType, llvm::Value *vectorValue, DataType /*vectorType*/
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType resultType{DataType::Kind::Vector};
	resultType.arraySize = matrixType.matrixRows();
	resultType.arrayElementType = std::make_shared<DataType>(matrixType.matrixElementType());
	llvm::Value *resultValue = llvm::Constant::getNullValue(getLLVMType(context, resultType));
	DataType elementType = matrixType.matrixElementType();
	for (int row = 0; row < matrixType.matrixRows(); row++) {
		llvm::Value *rowVector = builder.CreateExtractValue(matrixValue, {static_cast<unsigned>(row)}, "mat_row");
		llvm::Value *product = builder.CreateFMul(rowVector, vectorValue, "mat_vec_mul");
		llvm::Value *sum = llvm::ConstantFP::get(getLLVMType(context, elementType), 0.0);
		for (int column = 0; column < matrixType.matrixColumns(); column++) {
			llvm::Value *lane = builder.CreateExtractElement(product, getVectorLaneIndexValue(context, column), "mat_vec_lane");
			sum = builder.CreateFAdd(sum, lane, "mat_vec_sum");
		}
		resultValue = builder.CreateInsertElement(resultValue, sum, getVectorLaneIndexValue(context, row), "mat_vec_res");
	}
	return resultValue;
}

static llvm::Value *generateMatrixMatrixMultiply(
	ParseContext &context, llvm::Value *leftValue, DataType leftType, llvm::Value *rightValue, DataType rightType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType resultType{DataType::Kind::Matrix};
	resultType.matrixRowCount = leftType.matrixRows();
	resultType.arraySize = rightType.matrixColumns();
	resultType.arrayElementType = std::make_shared<DataType>(leftType.matrixElementType());
	llvm::Value *resultValue = llvm::Constant::getNullValue(getLLVMType(context, resultType));
	DataType elementType = resultType.matrixElementType();
	DataType rowVectorType{DataType::Kind::Vector};
	rowVectorType.arraySize = resultType.matrixColumns();
	rowVectorType.arrayElementType = std::make_shared<DataType>(elementType);
	for (int row = 0; row < resultType.matrixRows(); row++) {
		llvm::Value *resultRow = llvm::Constant::getNullValue(getLLVMType(context, rowVectorType));
		llvm::Value *leftRow = builder.CreateExtractValue(leftValue, {static_cast<unsigned>(row)}, "mat_left_row");
		for (int column = 0; column < resultType.matrixColumns(); column++) {
			llvm::Value *sum = llvm::ConstantFP::get(getLLVMType(context, elementType), 0.0);
			for (int inner = 0; inner < leftType.matrixColumns(); inner++) {
				llvm::Value *leftLane =
					builder.CreateExtractElement(leftRow, getVectorLaneIndexValue(context, inner), "mat_mul_left");
				llvm::Value *rightRow =
					builder.CreateExtractValue(rightValue, {static_cast<unsigned>(inner)}, "mat_mul_right_row");
				llvm::Value *rightLane =
					builder.CreateExtractElement(rightRow, getVectorLaneIndexValue(context, column), "mat_mul_right");
				sum = builder.CreateFAdd(sum, builder.CreateFMul(leftLane, rightLane), "mat_mul_sum");
			}
			resultRow =
				builder.CreateInsertElement(resultRow, sum, getVectorLaneIndexValue(context, column), "mat_mul_row_ins");
		}
		resultValue = builder.CreateInsertValue(resultValue, resultRow, {static_cast<unsigned>(row)}, "mat_mul_ins");
	}
	return resultValue;
}

static llvm::Value *generateScalarOrVectorArithmetic(
	ParseContext &context, ArithmeticIntrinsicKind op, llvm::Value *left, llvm::Value *right, DataType resultType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (resultType.kind == DataType::Kind::Vector) {
		switch (op) {
		case ArithmeticIntrinsicKind::Add:
			return builder.CreateFAdd(left, right, "vadd");
		case ArithmeticIntrinsicKind::Subtract:
			return builder.CreateFSub(left, right, "vsub");
		case ArithmeticIntrinsicKind::Multiply:
			return builder.CreateFMul(left, right, "vmul");
		case ArithmeticIntrinsicKind::Divide:
			return builder.CreateFDiv(left, right, "vdiv");
		default:
			return nullptr;
		}
	}
	if (resultType.kind == DataType::Kind::Float) {
		switch (op) {
		case ArithmeticIntrinsicKind::Add:
			return builder.CreateFAdd(left, right, "fadd");
		case ArithmeticIntrinsicKind::Subtract:
			return builder.CreateFSub(left, right, "fsub");
		case ArithmeticIntrinsicKind::Multiply:
			return builder.CreateFMul(left, right, "fmul");
		case ArithmeticIntrinsicKind::Divide:
			return builder.CreateFDiv(left, right, "fdiv");
		case ArithmeticIntrinsicKind::Modulo:
			return builder.CreateFRem(left, right, "fmod");
		default:
			return nullptr;
		}
	}
	switch (op) {
	case ArithmeticIntrinsicKind::Add:
		return builder.CreateAdd(left, right, "add");
	case ArithmeticIntrinsicKind::Subtract:
		return builder.CreateSub(left, right, "sub");
	case ArithmeticIntrinsicKind::Multiply:
		return builder.CreateMul(left, right, "mul");
	case ArithmeticIntrinsicKind::Divide:
		return builder.CreateSDiv(left, right, "div");
	case ArithmeticIntrinsicKind::Modulo:
		return builder.CreateSRem(left, right, "mod");
	default:
		return nullptr;
	}
}

static llvm::Value *generateScalarBitwise(ParseContext &context, IntrinsicKind kind, llvm::Value *left, llvm::Value *right) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	switch (kind) {
	case IntrinsicKind::BitwiseAnd:
		return builder.CreateAnd(left, right, "band");
	case IntrinsicKind::BitwiseOr:
		return builder.CreateOr(left, right, "bor");
	case IntrinsicKind::BitwiseXor:
		return builder.CreateXor(left, right, "bxor");
	case IntrinsicKind::ShiftLeft:
		return builder.CreateShl(left, right, "shl");
	case IntrinsicKind::ShiftRight:
		return builder.CreateAShr(left, right, "shr");
	default:
		return nullptr;
	}
}

static llvm::Intrinsic::ID mathIntrinsicId(IntrinsicKind kind) {
	switch (kind) {
	case IntrinsicKind::Sin:
		return llvm::Intrinsic::sin;
	case IntrinsicKind::Cos:
		return llvm::Intrinsic::cos;
	case IntrinsicKind::Sqrt:
		return llvm::Intrinsic::sqrt;
	case IntrinsicKind::Abs:
		return llvm::Intrinsic::fabs;
	case IntrinsicKind::Floor:
		return llvm::Intrinsic::floor;
	case IntrinsicKind::Ceil:
		return llvm::Intrinsic::ceil;
	case IntrinsicKind::Round:
		return llvm::Intrinsic::round;
	case IntrinsicKind::Exp:
		return llvm::Intrinsic::exp;
	case IntrinsicKind::Log:
		return llvm::Intrinsic::log;
	case IntrinsicKind::Pow:
		return llvm::Intrinsic::pow;
	case IntrinsicKind::Min:
		return llvm::Intrinsic::minnum;
	case IntrinsicKind::Max:
		return llvm::Intrinsic::maxnum;
	default:
		return llvm::Intrinsic::not_intrinsic;
	}
}

static DataType mathComputationType(DataType resultType, int mathFloatBytes) {
	if (resultType.kind == DataType::Kind::Float) {
		resultType.numericSize = mathFloatBytes;
		return resultType;
	}
	if (resultType.kind == DataType::Kind::Int)
		return {DataType::Kind::Float, mathFloatBytes};
	if (resultType.kind == DataType::Kind::Vector && resultType.arrayElementType) {
		DataType computationType = resultType;
		computationType.arrayElementType =
			std::make_shared<DataType>(mathComputationType(*resultType.arrayElementType, mathFloatBytes));
		return computationType;
	}
	return resultType;
}

static Section *currentFunctionLifetimeSection(ParseContext &context) {
	if (!context.currentCodegenInstantiation)
		return context.mainSection;
	requireCompilerInvariant(
		context.currentCodegenInstantiation->body && context.currentCodegenInstantiation->body->sourceSection,
		"generated function has no inferred source section"
	);
	return context.currentCodegenInstantiation->body->sourceSection;
}

// Generate code for an intrinsic call.
// All type decisions use finalizedExpressionType to resolve through flex/pattern bindings.
CodegenResult generateIntrinsicCode(
	ParseContext &context, Expression *callExpr, const std::string &name, const std::vector<Expression *> &allArguments,
	DataType resultType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (allArguments.empty())
		crashCompilerBug("intrinsic call is missing the intrinsic-name argument");
	const std::vector<Expression *> &args = allArguments;
	IntrinsicKind kind = intrinsicKind(name);
	auto generateRuntimeValue = [&](Expression *expression, llvm::Value *&value) {
		CodegenResult result = generateExpressionCode(context, expression);
		if (!result)
			return false;
		value = result.value;
		return true;
	};
	if (kind == IntrinsicKind::LifecycleValue) {
		if (!context.managedLifecycleValueBinding)
			crashCompilerBug("lifecycle value reached codegen without its managed value binding");
		return builder.CreateLoad(getLLVMType(context, resultType), context.managedLifecycleValueBinding, "lifecycle_value");
	}
	if (kind == IntrinsicKind::CommandLineArgumentCount || kind == IntrinsicKind::CommandLineArgumentValues) {
		llvm::GlobalVariable *global = kind == IntrinsicKind::CommandLineArgumentCount
										   ? context.commandLineArgumentCountGlobal
										   : context.commandLineArgumentValuesGlobal;
		requireCompilerInvariant(global != nullptr, "command-line argument intrinsic reached codegen without native ABI state");
		return builder.CreateLoad(global->getValueType(), global, global->getName() + ".value");
	}

	if (kind == IntrinsicKind::SetSubject) {
		DataType valueType = finalizedExpressionType(context, args[1]);
		if (!valueType.isDeduced() || valueType.kind == DataType::Kind::Void)
			crashCompilerBug("subject assignment reached codegen without a runtime value type");
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

	if (kind == IntrinsicKind::Subject) {
		if (!callExpr || !callExpr->subjectSetter)
			crashCompilerBug("subject read reached codegen without an inferred assignment");
		auto storage = context.subjectStorage.find(callExpr->subjectSetter);
		if (storage == context.subjectStorage.end())
			crashCompilerBug("subject read reached codegen before its inferred assignment");
		llvm::Value *value = builder.CreateLoad(getLLVMType(context, resultType), storage->second, "subject");
		if (typeHasManagedLifecycle(resultType) && !retainManagedValue(context, resultType, value))
			return CodegenResult::failure();
		return value;
	}

	if (kind == IntrinsicKind::Discard) {
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

	if (kind == IntrinsicKind::Store) {
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

	ArithmeticIntrinsicKind arithmeticOp = arithmeticIntrinsicKind(name);

	// Arithmetic intrinsics
	if (isArithmeticIntrinsic(arithmeticOp)) {
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
			if (!generateRuntimeValue(args[indexOperandIndex], indexValue))
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
		if (!generateRuntimeValue(args[1], left) || !generateRuntimeValue(args[2], right))
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

	if (kind == IntrinsicKind::BitwiseNot) {
		llvm::Value *value = nullptr;
		if (!generateRuntimeValue(args[1], value))
			return CodegenResult::failure();
		DataType valueType = finalizedExpressionType(context, args[1]);
		value = ensureType(context, value, valueType, resultType);
		return builder.CreateNot(value, "bnot");
	}

	if (kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor ||
		kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight) {
		llvm::Value *left = nullptr;
		llvm::Value *right = nullptr;
		if (!generateRuntimeValue(args[1], left) || !generateRuntimeValue(args[2], right))
			return CodegenResult::failure();
		DataType leftType = finalizedExpressionType(context, args[1]);
		DataType rightType = finalizedExpressionType(context, args[2]);

		left = ensureType(context, left, leftType, resultType);
		right = ensureType(context, right, rightType, resultType);
		return generateScalarBitwise(context, kind, left, right);
	}

	// Comparison intrinsics
	if (isComparisonIntrinsicKind(kind)) {
		llvm::Value *left = nullptr;
		llvm::Value *right = nullptr;
		if (!generateRuntimeValue(args[1], left) || !generateRuntimeValue(args[2], right))
			return CodegenResult::failure();
		DataType leftType = finalizedExpressionType(context, args[1]);
		DataType rightType = finalizedExpressionType(context, args[2]);

		llvm::Value *cmp;
		if ((kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) && leftType.isPointer() &&
			rightType.isPointer()) {
			DataType comparisonType = leftType;
			if (ClassDefinition::typeStructurallyRefines(rightType, leftType))
				comparisonType = rightType;
			left = ensureType(context, left, leftType, comparisonType);
			right = ensureType(context, right, rightType, comparisonType);
			cmp = kind == IntrinsicKind::Equal ? builder.CreateICmpEQ(left, right, "peq")
											   : builder.CreateICmpNE(left, right, "pne");
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
				if (kind == IntrinsicKind::LessThan)
					cmp = builder.CreateICmpSLT(left, right, "lt");
				else if (kind == IntrinsicKind::LessThanOrEqual)
					cmp = builder.CreateICmpSLE(left, right, "le");
				else if (kind == IntrinsicKind::GreaterThan)
					cmp = builder.CreateICmpSGT(left, right, "gt");
				else if (kind == IntrinsicKind::GreaterThanOrEqual)
					cmp = builder.CreateICmpSGE(left, right, "ge");
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

	// Logical operators
	if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
		llvm::Value *left = nullptr;
		llvm::Value *right = nullptr;
		if (!generateRuntimeValue(args[1], left) || !generateRuntimeValue(args[2], right))
			return CodegenResult::failure();
		DataType leftType = finalizedExpressionType(context, args[1]);
		DataType rightType = finalizedExpressionType(context, args[2]);
		if (leftType.kind != DataType::Kind::Bool || rightType.kind != DataType::Kind::Bool)
			crashCompilerBug("logical and/or operands must be boolean after type inference");

		if (kind == IntrinsicKind::And)
			return builder.CreateAnd(left, right, "and");
		else
			return builder.CreateOr(left, right, "or");
	}

	if (kind == IntrinsicKind::Not) {
		llvm::Value *val = nullptr;
		if (!generateRuntimeValue(args[1], val))
			return CodegenResult::failure();
		DataType valType = finalizedExpressionType(context, args[1]);
		if (valType.kind != DataType::Kind::Bool)
			crashCompilerBug("logical not operand must be boolean after type inference");

		return builder.CreateXor(val, builder.getTrue(), "not");
	}

	// Negate
	if (kind == IntrinsicKind::Negate) {
		llvm::Value *val = nullptr;
		if (!generateRuntimeValue(args[1], val))
			return CodegenResult::failure();
		DataType valType = finalizedExpressionType(context, args[1]);
		if (valType.kind == DataType::Kind::Float)
			return builder.CreateFNeg(val, "fneg");
		return builder.CreateNeg(val, "neg");
	}

	if (kind == IntrinsicKind::Min || kind == IntrinsicKind::Max) {
		llvm::Value *left = nullptr;
		llvm::Value *right = nullptr;
		if (!generateRuntimeValue(args[1], left) || !generateRuntimeValue(args[2], right))
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

		llvm::Value *cmp = kind == IntrinsicKind::Min ? builder.CreateICmpSLT(left, right, "min_cmp")
													  : builder.CreateICmpSGT(left, right, "max_cmp");
		return builder.CreateSelect(cmp, left, right, kind == IntrinsicKind::Min ? "min" : "max");
	}

	// Math functions (sin, cos, sqrt, abs, floor, ceil, round, exp, log, pow, atan2, min, max)
	if (isMathFunction(name)) {
		context.requiredLibraries.insert("m");
		llvm::Intrinsic::ID intrinsicId = mathIntrinsicId(kind);
		if (intrinsicId != llvm::Intrinsic::not_intrinsic) {
			// GLSL.std.450 extended instructions (used by SPIR-V) only support 16/32-bit floats.
			// Compute in float and convert back to the inferred result type.
			int mathFloatBytes = defaultFloatByteSize(context.options.emitSPIRV);
			DataType computationType = mathComputationType(resultType, mathFloatBytes);
			if (args.size() == 2) {
				llvm::Value *val = nullptr;
				if (!generateRuntimeValue(args[1], val))
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
			if (!generateRuntimeValue(args[1], left) || !generateRuntimeValue(args[2], right))
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
			if (!generateRuntimeValue(args[1], y) || !generateRuntimeValue(args[2], x))
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

	// Pointer intrinsics
	if (kind == IntrinsicKind::AddressOf) {
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

	if (kind == IntrinsicKind::Dereference) {
		llvm::Value *ptrVal = nullptr;
		if (!generateRuntimeValue(args[1], ptrVal))
			return CodegenResult::failure();
		DataType ptrType = finalizedExpressionType(context, args[1]);
		DataType elemType = ptrType.dereferenced();
		llvm::Type *elemLLVMType = getLLVMType(context, elemType);
		return builder.CreateAlignedLoad(elemLLVMType, ptrVal, getLLVMABIAlignment(context, elemType), "deref");
	}

	// Pointer storage intrinsics
	if (kind == IntrinsicKind::StoreAt || kind == IntrinsicKind::InitializeAt) {
		llvm::Value *ptr = nullptr;
		llvm::Value *value = nullptr;
		if (!generateRuntimeValue(args[1], ptr) || !generateRuntimeValue(args[2], value))
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

	if (kind == IntrinsicKind::DestroyAt) {
		llvm::Value *ptr = nullptr;
		if (!generateRuntimeValue(args[1], ptr))
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

#include "codegenIntrinsicEffects.inl"
