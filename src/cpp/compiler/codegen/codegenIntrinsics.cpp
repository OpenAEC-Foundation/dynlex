#include "classDefinition.h"
#include "codegenInternal.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "intrinsicInfo.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Helper to extract string literal from an expression
std::string getStringLiteral(Expression *expr) {
	if (expr && expr->kind == Expression::Kind::Literal) {
		if (auto *str = std::get_if<std::string>(&expr->literalValue))
			return *str;
	}
	return "";
}

static bool isSectionDescendantOrSame(Section *section, Section *ancestor) {
	for (Section *current = section; current; current = current->parent) {
		if (current == ancestor)
			return true;
	}
	return false;
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

static llvm::Value *buildRuntimeSelect(ParseContext &context, const std::vector<Expression *> &args, DataType resultType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *function = builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
	if (!function)
		return nullptr;

	DataType conditionType = getEffectiveType(context, args[1]);
	if (conditionType.kind != DataType::Kind::Bool)
		crashCompilerBug("runtime select condition must be boolean after type inference");
	llvm::Value *conditionValue = generateExpressionCode(context, args[1]);
	if (!conditionValue)
		return nullptr;

	if (resultType.kind == DataType::Kind::Type) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "compile time type value used at runtime", args[1]->range)
		);
		return nullptr;
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
	llvm::Value *trueValue = generateExpressionCode(context, args[2]);
	llvm::BasicBlock *trueEndBlock = builder.GetInsertBlock();
	if (!trueEndBlock->getTerminator()) {
		if (resultType.kind != DataType::Kind::Void) {
			DataType trueType = getEffectiveType(context, args[2]);
			trueValue = ensureType(context, trueValue, trueType, resultType);
			incomingValues.push_back({trueValue, trueEndBlock});
		}
		builder.CreateBr(mergeBlock);
	}

	builder.SetInsertPoint(falseBlock);
	llvm::Value *falseValue = generateExpressionCode(context, args[3]);
	llvm::BasicBlock *falseEndBlock = builder.GetInsertBlock();
	if (!falseEndBlock->getTerminator()) {
		if (resultType.kind != DataType::Kind::Void) {
			DataType falseType = getEffectiveType(context, args[3]);
			falseValue = ensureType(context, falseValue, falseType, resultType);
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

static llvm::Value *
buildVectorValue(ParseContext &context, DataType vectorType, const std::vector<Expression *> &args, size_t startIndex) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *llvmVectorType = getLLVMType(context, vectorType);
	llvm::Value *vectorValue = llvm::Constant::getNullValue(llvmVectorType);
	DataType elementType = vectorType.vectorElementType();
	for (int i = 0; i < vectorType.vectorSize(); i++) {
		llvm::Value *elementValue = generateExpressionCode(context, args[startIndex + i]);
		DataType fromType = getEffectiveType(context, args[startIndex + i]);
		elementValue = ensureType(context, elementValue, fromType, elementType);
		vectorValue = builder.CreateInsertElement(vectorValue, elementValue, getVectorLaneIndexValue(context, i), "vec_ins");
	}
	return vectorValue;
}

static llvm::Value *buildMatrixFromFlatArray(ParseContext &context, DataType matrixType, Expression *sourceExpr) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType sourceType = getEffectiveType(context, sourceExpr);
	if (sourceType.kind != DataType::Kind::Array || !sourceType.arrayElementType)
		return nullptr;
	if (sourceType.arraySize != matrixType.matrixRows() * matrixType.matrixColumns())
		return nullptr;

	llvm::Value *flatArrayValue = generateExpressionCode(context, sourceExpr);
	llvm::Type *llvmFlatArrayType = getLLVMType(context, sourceType);
	llvm::AllocaInst *flatAlloca = createEntryAlloca(context, "matrix_flat", sourceType);
	builder.CreateAlignedStore(flatArrayValue, flatAlloca, llvm::Align(8));

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
			llvm::Value *elementValue =
				builder.CreateLoad(sourceType.arrayElementType->toLLVM(*context.llvmContext), elementPtr);
			elementValue = ensureType(context, elementValue, *sourceType.arrayElementType, elementType);
			rowValue =
				builder.CreateInsertElement(rowValue, elementValue, getVectorLaneIndexValue(context, column), "mat_row_ins");
		}
		matrixValue = builder.CreateInsertValue(matrixValue, rowValue, {static_cast<unsigned>(row)}, "mat_ins");
	}

	return matrixValue;
}

static llvm::Value *buildMatrixFromScalarArgs(
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
			llvm::Value *elementValue = generateExpressionCode(context, args[argIndex]);
			DataType fromType = getEffectiveType(context, args[argIndex]);
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

// Generate code for an intrinsic call.
// All type decisions use getEffectiveType to resolve through flex/pattern bindings.
llvm::Value *generateIntrinsicCode(
	ParseContext &context, Expression *callExpr, const std::string &name, const std::vector<Expression *> &allArguments,
	DataType resultType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (allArguments.empty())
		crashCompilerBug("intrinsic call is missing the intrinsic-name argument");
	const std::vector<Expression *> &args = allArguments;
	IntrinsicKind kind = intrinsicKind(name);

	if (kind == IntrinsicKind::Discard) {
		// Evaluate the argument for side effects and discard the result
		generateExpressionCode(context, args[1]);
		return nullptr;
	}

	if (kind == IntrinsicKind::Store) {
		// Generate the value in the current (original) flex scope first,
		// before resolving the destination which may cross scope boundaries.
		DataType valType = getEffectiveType(context, args[2]);
		llvm::Value *val = generateExpressionCode(context, args[2]);

		// Save scope state — resolveThroughFlexLayers freely crosses scope
		// boundaries, so we restore afterward.
		auto savedBindingFrames = context.flexBindingFrames;

		// Resolve the destination through all flex and scope layers to detect
		// property stores. E.g., `add value to the x of target` chains through
		// scalar add flex → set flex → @intrinsic("store", var, val), and the
		// dest var resolves through multiple scopes to @intrinsic("property", ...).
		Expression *destExpr = args[1];
		resolveThroughFlexLayers(context, destExpr);

		if (destExpr->kind == Expression::Kind::IntrinsicCall &&
			intrinsicKind(destExpr->intrinsicName) == IntrinsicKind::Property) {
			// Storing to a class field: generate GEP + store
			Expression *instExpr = resolveVariableBinding(context, destExpr->arguments[1]);
			DataType instType = getEffectiveType(context, instExpr);
			ClassDefinition *classDef = instType.classDefinition;
			Expression *propExpr = resolveVariableBinding(context, destExpr->arguments[2]);
			std::string fieldName = getStringLiteral(propExpr);

			int fieldIdx = -1;
			for (size_t i = 0; i < classDef->fields.size(); i++) {
				if (classDef->fields[i].name == fieldName) {
					fieldIdx = i;
					break;
				}
			}

			llvm::Value *instPtr = getVariablePointer(context, instExpr);
			llvm::Type *structType = getLLVMType(context, instType);
			unsigned llvmFieldIdx = static_cast<unsigned>(fieldIdx);
			if (instType.classInstIndex >= 0 && instType.classInstIndex < static_cast<int>(classDef->instantiations.size()) &&
				fieldIdx < static_cast<int>(classDef->instantiations[instType.classInstIndex].llvmFieldIndices.size())) {
				llvmFieldIdx = classDef->instantiations[instType.classInstIndex].llvmFieldIndices[fieldIdx];
			}
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, instPtr, llvmFieldIdx, "field_ptr");

			DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
			val = ensureType(context, val, valType, fieldType);
			builder.CreateStore(val, fieldPtr);
		} else {
			// Restore scope state — the else branch evaluates args[1] directly
			context.flexBindingFrames = savedBindingFrames;

			llvm::Value *ptr = getVariablePointer(context, args[1]);
			if (ptr && val) {
				DataType destType = getEffectiveType(context, args[1]);
				if (destType.kind == DataType::Kind::Class && !destType.isPointer() && destType.classDefinition &&
					destType.classInstIndex >= 0) {
					ClassDefinition *classDef = destType.classDefinition;
					auto &destFields = classDef->instantiations[destType.classInstIndex].fieldTypes;
					auto &srcFields = valType.classDefinition
										  ? valType.classDefinition->instantiations[valType.classInstIndex].fieldTypes
										  : destFields;
					llvm::Value *srcPtr = val;
					if (srcPtr && !srcPtr->getType()->isPointerTy()) {
						llvm::AllocaInst *tmpStruct = createEntryAlloca(context, "struct_src", valType);
						builder.CreateAlignedStore(srcPtr, tmpStruct, llvm::Align(8));
						srcPtr = tmpStruct;
					}
					bool sameLayout = (srcFields.size() == destFields.size());
					if (sameLayout) {
						for (size_t i = 0; i < srcFields.size(); i++) {
							if (srcFields[i] != destFields[i]) {
								sameLayout = false;
								break;
							}
						}
					}
					if (sameLayout) {
						// Same field types — direct struct copy
						llvm::Type *structType = getLLVMType(context, destType);
						llvm::Value *srcVal = builder.CreateAlignedLoad(structType, srcPtr, llvm::Align(8), "struct_load");
						builder.CreateAlignedStore(srcVal, ptr, llvm::Align(8));
					} else {
						// Different field types — element-wise copy with conversion
						llvm::Type *srcStructType = getLLVMType(context, valType);
						llvm::Type *destStructType = getLLVMType(context, destType);
						for (size_t i = 0; i < destFields.size(); i++) {
							llvm::Value *srcFieldPtr = builder.CreateStructGEP(srcStructType, srcPtr, i, "src_field");
							llvm::Value *fieldVal =
								builder.CreateLoad(srcFields[i].toLLVM(*context.llvmContext), srcFieldPtr, "field_val");
							fieldVal = ensureType(context, fieldVal, srcFields[i], destFields[i]);
							llvm::Value *destFieldPtr = builder.CreateStructGEP(destStructType, ptr, i, "dest_field");
							builder.CreateStore(fieldVal, destFieldPtr);
						}
					}
				} else {
					val = ensureType(context, val, valType, destType);
					builder.CreateAlignedStore(val, ptr, llvm::Align(8));
				}
			}
		}

		// Restore scope state
		context.flexBindingFrames = savedBindingFrames;
		return nullptr;
	}

	ArithmeticIntrinsicKind arithmeticOp = arithmeticIntrinsicKind(name);

	// Arithmetic intrinsics
	if (isArithmeticIntrinsic(arithmeticOp)) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
		DataType leftType = getEffectiveType(context, args[1]);
		DataType rightType = getEffectiveType(context, args[2]);

		// Pointer arithmetic: ptr +/- integer → GEP
		if (isPointerArithmeticIntrinsic(arithmeticOp) && (leftType.isPointer() || rightType.isPointer())) {
			llvm::Value *ptrVal = leftType.isPointer() ? left : right;
			llvm::Value *indexVal = leftType.isPointer() ? right : left;
			DataType indexType = leftType.isPointer() ? rightType : leftType;
			DataType ptrType = leftType.isPointer() ? leftType : rightType;
			llvm::Type *elemType = ptrType.dereferenced().toLLVM(*context.llvmContext);
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
		llvm::Value *value = generateExpressionCode(context, args[1]);
		DataType valueType = getEffectiveType(context, args[1]);
		value = ensureType(context, value, valueType, resultType);
		return builder.CreateNot(value, "bnot");
	}

	if (kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor ||
		kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
		DataType leftType = getEffectiveType(context, args[1]);
		DataType rightType = getEffectiveType(context, args[2]);

		left = ensureType(context, left, leftType, resultType);
		right = ensureType(context, right, rightType, resultType);
		return generateScalarBitwise(context, kind, left, right);
	}

	// Comparison intrinsics
	if (isComparisonIntrinsicKind(kind)) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
		DataType leftType = getEffectiveType(context, args[1]);
		DataType rightType = getEffectiveType(context, args[2]);
		if ((kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) && leftType.isPointer() &&
			rightType.isPointer() && leftType == rightType) {
			llvm::Value *cmp = kind == IntrinsicKind::Equal ? builder.CreateICmpEQ(left, right, "peq")
															: builder.CreateICmpNE(left, right, "pne");
			assert(resultType.isDeduced() && "Comparison result type must be deduced before codegen");
			if (resultType.kind == DataType::Kind::Bool)
				return cmp;
			return builder.CreateZExt(cmp, getLLVMType(context, resultType), "cmp_ext");
		}
		DataType promoted;
		DataType::promoteArithmetic(leftType, rightType, promoted);

		left = ensureType(context, left, leftType, promoted);
		right = ensureType(context, right, rightType, promoted);

		llvm::Value *cmp;
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

		assert(resultType.isDeduced() && "Comparison result type must be deduced before codegen");
		if (resultType.kind == DataType::Kind::Bool)
			return cmp; // already i1
		return builder.CreateZExt(cmp, getLLVMType(context, resultType), "cmp_ext");
	}

	// Logical operators
	if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
		DataType leftType = getEffectiveType(context, args[1]);
		DataType rightType = getEffectiveType(context, args[2]);
		if (leftType.kind != DataType::Kind::Bool || rightType.kind != DataType::Kind::Bool)
			crashCompilerBug("logical and/or operands must be boolean after type inference");

		if (kind == IntrinsicKind::And)
			return builder.CreateAnd(left, right, "and");
		else
			return builder.CreateOr(left, right, "or");
	}

	if (kind == IntrinsicKind::Not) {
		llvm::Value *val = generateExpressionCode(context, args[1]);
		DataType valType = getEffectiveType(context, args[1]);
		if (valType.kind != DataType::Kind::Bool)
			crashCompilerBug("logical not operand must be boolean after type inference");

		return builder.CreateXor(val, builder.getTrue(), "not");
	}

	// Negate
	if (kind == IntrinsicKind::Negate) {
		llvm::Value *val = generateExpressionCode(context, args[1]);
		DataType valType = getEffectiveType(context, args[1]);
		if (valType.kind == DataType::Kind::Float)
			return builder.CreateFNeg(val, "fneg");
		return builder.CreateNeg(val, "neg");
	}

	if (kind == IntrinsicKind::Min || kind == IntrinsicKind::Max) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
		DataType leftType = getEffectiveType(context, args[1]);
		DataType rightType = getEffectiveType(context, args[2]);
		DataType promoted;
		if (!DataType::promoteArithmetic(leftType, rightType, promoted)) {
			std::fputs("min/max operands must be arithmetic-compatible before codegen\n", stderr);
			std::abort();
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
				llvm::Value *val = generateExpressionCode(context, args[1]);
				DataType valType = getEffectiveType(context, args[1]);
				if (valType != computationType)
					val = ensureType(context, val, valType, computationType);
				llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, intrinsicId, {val->getType()});
				llvm::Value *computed = builder.CreateCall(fn, {val}, name);
				return ensureType(context, computed, computationType, resultType);
			}
			llvm::Value *left = generateExpressionCode(context, args[1]);
			llvm::Value *right = generateExpressionCode(context, args[2]);
			DataType leftType = getEffectiveType(context, args[1]);
			DataType rightType = getEffectiveType(context, args[2]);
			left = ensureType(context, left, leftType, computationType);
			right = ensureType(context, right, rightType, computationType);
			llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, intrinsicId, {left->getType()});
			llvm::Value *computed = builder.CreateCall(fn, {left, right}, name);
			return ensureType(context, computed, computationType, resultType);
		}

		// atan2: no LLVM intrinsic, call libm
		if (kind == IntrinsicKind::Atan2) {
			llvm::Value *y = generateExpressionCode(context, args[1]);
			llvm::Value *x = generateExpressionCode(context, args[2]);
			DataType yType = getEffectiveType(context, args[1]);
			DataType xType = getEffectiveType(context, args[2]);
			DataType promoted;
			DataType::promoteArithmetic(yType, xType, promoted);
			promoted = mathComputationType(promoted, defaultFloatByteSize(context.options.emitSPIRV));
			y = ensureType(context, y, yType, promoted);
			x = ensureType(context, x, xType, promoted);
			llvm::Type *floatType = promoted.toLLVM(*context.llvmContext);
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
		llvm::Value *ptr = getVariablePointer(context, args[1]);
		assert(ptr && "address of requires a variable");
		return ptr;
	}

	if (kind == IntrinsicKind::Dereference) {
		llvm::Value *ptrVal = generateExpressionCode(context, args[1]);
		DataType ptrType = getEffectiveType(context, args[1]);
		DataType elemType = ptrType.dereferenced();
		llvm::Type *elemLLVMType = getLLVMType(context, elemType);
		return builder.CreateAlignedLoad(elemLLVMType, ptrVal, llvm::Align(8), "deref");
	}

	// Array/pointer intrinsics
	if (kind == IntrinsicKind::StoreAt) {
		llvm::Value *ptr = generateExpressionCode(context, args[1]);
		llvm::Value *index = generateExpressionCode(context, args[2]);
		llvm::Value *value = generateExpressionCode(context, args[3]);
		DataType ptrType = getEffectiveType(context, args[1]);
		assert(ptrType.isPointer() && "store at requires a pointer argument");
		index = coerceIndexToSizeT(context, index, getEffectiveType(context, args[2]));
		DataType pointedType = ptrType.dereferenced();
		DataType elementType = pointedType;
		llvm::Value *elementPtr = nullptr;
		if (pointedType.kind == DataType::Kind::Array && pointedType.arrayElementType) {
			elementType = *pointedType.arrayElementType;
			llvm::Type *arrayType = getLLVMType(context, pointedType);
			elementPtr = builder.CreateGEP(arrayType, ptr, {builder.getInt64(0), index}, "array_elem_ptr");
		} else {
			llvm::Type *elemLLVMType = getLLVMType(context, elementType);
			elementPtr = builder.CreateGEP(elemLLVMType, ptr, index, "elem_ptr");
		}
		value = ensureType(context, value, getEffectiveType(context, args[3]), elementType);
		builder.CreateAlignedStore(value, elementPtr, llvm::Align(8));
		return nullptr;
	}

	if (kind == IntrinsicKind::LoadAt) {
		llvm::Value *ptr = generateExpressionCode(context, args[1]);
		llvm::Value *index = generateExpressionCode(context, args[2]);
		DataType ptrType = getEffectiveType(context, args[1]);
		assert(ptrType.isPointer() && "load at requires a pointer argument");
		index = coerceIndexToSizeT(context, index, getEffectiveType(context, args[2]));
		DataType pointedType = ptrType.dereferenced();
		DataType elementType = pointedType;
		llvm::Value *elementPtr = nullptr;
		if (pointedType.kind == DataType::Kind::Array && pointedType.arrayElementType) {
			elementType = *pointedType.arrayElementType;
			llvm::Type *arrayType = getLLVMType(context, pointedType);
			elementPtr = builder.CreateGEP(arrayType, ptr, {builder.getInt64(0), index}, "array_elem_ptr");
		} else {
			llvm::Type *elemLLVMType = getLLVMType(context, elementType);
			elementPtr = builder.CreateGEP(elemLLVMType, ptr, index, "elem_ptr");
		}
		return builder.CreateAlignedLoad(getLLVMType(context, elementType), elementPtr, llvm::Align(8));
	}

	if (kind == IntrinsicKind::ExecuteBody) {
		Section *callSection = callExpr && callExpr->range.line ? callExpr->range.line->section : nullptr;
		if (!callSection)
			crashCompilerBug("execute body call is missing source section context");
		if (context.sectionFlexBodyFrames.empty())
			crashCompilerBug("execute body used outside of section flex expansion");

		ParseContext::SectionFlexBodyFrame *targetFrame = nullptr;
		for (auto frameIt = context.sectionFlexBodyFrames.rbegin(); frameIt != context.sectionFlexBodyFrames.rend();
			 ++frameIt) {
			if (frameIt->definitionSection && isSectionDescendantOrSame(callSection, frameIt->definitionSection)) {
				targetFrame = &*frameIt;
				break;
			}
		}

		if (!targetFrame) {
			for (auto ownerIt = context.flexCallSiteSectionStack.rbegin(); ownerIt != context.flexCallSiteSectionStack.rend();
				 ++ownerIt) {
				Section *ownerSection = *ownerIt;
				if (!ownerSection)
					continue;
				for (auto frameIt = context.sectionFlexBodyFrames.rbegin(); frameIt != context.sectionFlexBodyFrames.rend();
					 ++frameIt) {
					if (frameIt->definitionSection && isSectionDescendantOrSame(ownerSection, frameIt->definitionSection)) {
						targetFrame = &*frameIt;
						break;
					}
				}
				if (targetFrame)
					break;
			}
		}

		if (!targetFrame) {
			for (auto flexIt = context.activeFlexDefinitionStack.rbegin(); flexIt != context.activeFlexDefinitionStack.rend();
				 ++flexIt) {
				Section *activeDefinition = *flexIt;
				if (!activeDefinition || activeDefinition->type != SectionType::Section)
					continue;
				for (auto frameIt = context.sectionFlexBodyFrames.rbegin(); frameIt != context.sectionFlexBodyFrames.rend();
					 ++frameIt) {
					if (frameIt->definitionSection == activeDefinition) {
						targetFrame = &*frameIt;
						break;
					}
				}
				if (targetFrame)
					break;
			}
		}

		if (!targetFrame || !targetFrame->bodySection) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "execute body has no matching section flex body", callExpr->range)
			);
			return nullptr;
		}

		if (targetFrame->bodyEmitted) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "execute body can only run once per section flex call", callExpr->range
			));
			return nullptr;
		}
		targetFrame->bodyEmitted = true;
		emitFlexBodySection(context, targetFrame->bodySection, false);
		return nullptr;
	}

	if (kind == IntrinsicKind::LoopWhile) {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "loop while requires a body section");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_cond", func);
		llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_body", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_exit", func);

		builder.CreateBr(condBlock);
		builder.SetInsertPoint(condBlock);

		llvm::Value *condValue = generateExpressionCode(context, args[1]);
		DataType condType = getEffectiveType(context, args[1]);
		if (condType.kind != DataType::Kind::Bool)
			crashCompilerBug("loop while condition must be boolean after type inference");
		builder.CreateCondBr(condValue, bodyBlock, exitBlock);

		builder.SetInsertPoint(bodyBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = condBlock;

		return nullptr;
	}

	if (kind == IntrinsicKind::If) {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "if requires a body section");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_then", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_exit", func);

		llvm::Value *condValue = generateExpressionCode(context, args[1]);
		DataType condType = getEffectiveType(context, args[1]);
		if (condType.kind != DataType::Kind::Bool)
			crashCompilerBug("if condition must be boolean after type inference");
		builder.CreateCondBr(condValue, thenBlock, exitBlock);

		builder.SetInsertPoint(thenBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (kind == IntrinsicKind::Else || kind == IntrinsicKind::ElseIf) {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "else/else if requires a body section");

		llvm::Function *func = builder.GetInsertBlock()->getParent();
		llvm::BasicBlock *currentBlock = builder.GetInsertBlock();

		// Create new exit block — if/elif bodies will jump here (skipping the else)
		llvm::BasicBlock *newExitBlock = llvm::BasicBlock::Create(*context.llvmContext, "else_exit", func);

		// Redirect all unconditional branch predecessors to the new exit block.
		// Unconditional branches come from if/elif bodies (they should skip the else).
		// Conditional false-path branches come from if/elif conditions (they should fall through here).
		llvm::SmallVector<llvm::BasicBlock *, 4> uncondPreds;
		for (llvm::BasicBlock *pred : llvm::predecessors(currentBlock)) {
			llvm::BranchInst *br = llvm::dyn_cast<llvm::BranchInst>(pred->getTerminator());
			if (br && br->isUnconditional()) {
				uncondPreds.push_back(pred);
			}
		}
		for (llvm::BasicBlock *pred : uncondPreds) {
			pred->getTerminator()->replaceUsesOfWith(currentBlock, newExitBlock);
		}

		if (kind == IntrinsicKind::ElseIf) {
			llvm::BasicBlock *elifThenBlock = llvm::BasicBlock::Create(*context.llvmContext, "elif_then", func);

			llvm::Value *condValue = generateExpressionCode(context, args[1]);
			DataType condType = getEffectiveType(context, args[1]);
			if (condType.kind != DataType::Kind::Bool)
				crashCompilerBug("else if condition must be boolean after type inference");
			builder.CreateCondBr(condValue, elifThenBlock, newExitBlock);

			builder.SetInsertPoint(elifThenBlock);
		}

		bodySection->exitBlock = newExitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (kind == IntrinsicKind::Switch) {
		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::Value *switchValue = generateExpressionCode(context, args[1]);

		// Ensure the value is an integer (LLVM switch requires integer operand)
		assert(
			(getEffectiveType(context, args[1]).kind == DataType::Kind::Int ||
			 getEffectiveType(context, args[1]).kind == DataType::Kind::Bool) &&
			"switch requires an integer value"
		);

		llvm::BasicBlock *defaultBlock = llvm::BasicBlock::Create(*context.llvmContext, "switch_default", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "switch_exit", func);

		llvm::SwitchInst *switchInst = builder.CreateSwitch(switchValue, defaultBlock);

		// Default case: just branch to exit
		builder.SetInsertPoint(defaultBlock);
		builder.CreateBr(exitBlock);

		// Store switch state for "case" intrinsics to use
		context.currentSwitchInst = switchInst;
		context.currentSwitchExitBlock = exitBlock;

		// Don't set bodySection->exitBlock — the insert point will naturally
		// end up at switchExitBlock after all cases are processed.
		builder.SetInsertPoint(exitBlock);

		return nullptr;
	}

	if (kind == IntrinsicKind::Case) {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "case requires a body section");

		assert(context.currentSwitchInst && "case outside of switch");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		// Evaluate the case value — must be a constant integer
		llvm::Value *caseValue = generateExpressionCode(context, args[1]);
		llvm::ConstantInt *caseConst = llvm::dyn_cast<llvm::ConstantInt>(caseValue);
		assert(caseConst && "case value must be a constant integer");
		llvm::Type *switchType = context.currentSwitchInst->getCondition()->getType();
		if (caseConst->getType() != switchType)
			caseConst = llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(switchType, caseConst->getSExtValue(), true));

		llvm::BasicBlock *caseBlock = llvm::BasicBlock::Create(*context.llvmContext, "case", func);
		context.currentSwitchInst->addCase(caseConst, caseBlock);

		builder.SetInsertPoint(caseBlock);
		bodySection->exitBlock = context.currentSwitchExitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (kind == IntrinsicKind::Return) {
		llvm::Value *returnValue = generateExpressionCode(context, args[1]);
		builder.CreateRet(returnValue);
		return nullptr;
	}

	if (kind == IntrinsicKind::Call) {
		// Format: args[1]="library", args[2]="function", args[3]="return type", args[4+]=actual args
		std::string library = getStringLiteral(args[1]);
		std::string funcName = getStringLiteral(args[2]);
		if (!library.empty() && library != "libc")
			context.requiredLibraries.insert(library);

		DataType retTypeRef = getEffectiveType(context, args[3]);
		DataType returnType = retTypeRef.toReferencedType();
		llvm::Type *returnLLVMType = returnType.toLLVM(*context.llvmContext);

		// Build call arguments — string literals become global constant pointers
		std::vector<llvm::Value *> callArgs;
		for (size_t i = 4; i < args.size(); ++i) {
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
					continue;
				}
			}
			llvm::Value *argVal = generateExpressionCode(context, args[i]);
			if (argVal)
				callArgs.push_back(argVal);
		}

		llvm::FunctionCallee callee;
		std::vector<llvm::Type *> argTypes;
		argTypes.reserve(callArgs.size());
		for (llvm::Value *arg : callArgs)
			argTypes.push_back(arg->getType());
		if (library == "libc" && funcName == "malloc" && callArgs.size() == 1) {
			callArgs[0] = coerceIndexToSizeT(context, callArgs[0], getEffectiveType(context, args[4]));
			llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getPtrTy(), {builder.getInt64Ty()}, false);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else if (library == "libc" && funcName == "memcpy" && callArgs.size() == 3) {
			callArgs[2] = coerceIndexToSizeT(context, callArgs[2], getEffectiveType(context, args[6]));
			llvm::FunctionType *funcType = llvm::FunctionType::get(
				builder.getPtrTy(), {builder.getPtrTy(), builder.getPtrTy(), builder.getInt64Ty()}, false
			);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else if (library == "libc" && funcName == "snprintf" && callArgs.size() >= 3) {
			callArgs[1] = coerceIndexToSizeT(context, callArgs[1], getEffectiveType(context, args[5]));
			llvm::FunctionType *funcType = llvm::FunctionType::get(
				builder.getInt32Ty(), {builder.getPtrTy(), builder.getInt64Ty(), builder.getPtrTy()}, true
			);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else if (library == "libc" && funcName == "printf" && callArgs.size() >= 1) {
			llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getInt32Ty(), {builder.getPtrTy()}, true);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else if (library == "libc" && funcName == "strlen" && callArgs.size() == 1) {
			llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getInt64Ty(), {builder.getPtrTy()}, false);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else if (library == "libc" && funcName == "free" && callArgs.size() == 1) {
			llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else {
			llvm::FunctionType *funcType = llvm::FunctionType::get(returnLLVMType, argTypes, false);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		}

		llvm::Value *callResult = builder.CreateCall(callee, callArgs);
		// If return type is void, return nullptr (no value to use)
		if (returnType.kind == DataType::Kind::Void)
			return nullptr;
		return callResult;
	}

	if (kind == IntrinsicKind::Function) {
		Expression *nameExpr = resolveVariableBinding(context, args[1]);
		std::string signature = getStringLiteral(nameExpr);
		if (signature.empty()) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "function intrinsic requires constant string literal", args[1]->range
			));
			return nullptr;
		}

		std::vector<PatternDefinition *> matches = findDefinitionsBySignature(context, SectionType::Function, signature);
		std::vector<PatternDefinition *> callableMatches;
		for (PatternDefinition *definition : matches) {
			if (definition && definition->section && definition->section->type == SectionType::Function &&
				!definition->section->isFlex)
				callableMatches.push_back(definition);
		}
		if (callableMatches.empty()) {
			Diagnostic diagnostic;
			diagnostic.level = Diagnostic::Level::Error;
			diagnostic.range = args[1]->range;
			diagnostic.message = "unknown function reference: " + signature;
			context.addDiagnostic(std::move(diagnostic));
			return nullptr;
		}
		if (callableMatches.size() > 1) {
			Diagnostic diagnostic;
			diagnostic.level = Diagnostic::Level::Error;
			diagnostic.range = args[1]->range;
			diagnostic.message = "ambiguous function reference: " + signature;
			context.addDiagnostic(std::move(diagnostic));
			return nullptr;
		}

		llvm::Function *callableFunction =
			ensureCallableFunctionGenerated(context, callableMatches.front(), callableMatches.front()->section->isExposed);
		if (!callableFunction)
			return nullptr;
		if (callableFunction->getType() == builder.getPtrTy())
			return callableFunction;
		return builder.CreateBitCast(callableFunction, builder.getPtrTy(), "function_ptr");
	}

	if (kind == IntrinsicKind::Cast) {
		// Format: args[1]=value, args[2]=type (TypeReference)
		llvm::Value *val = generateExpressionCode(context, args[1]);
		DataType fromType = getEffectiveType(context, args[1]);

		// Get target type from the TypeReference argument
		DataType typeArgType = getEffectiveType(context, args[2]);
		if (typeArgType.kind != DataType::Kind::Type)
			return nullptr; // unresolved type (e.g. spurious top-level instantiation)
		DataType toType = typeArgType.toReferencedType();

		return ensureType(context, val, fromType, toType);
	}

	if (kind == IntrinsicKind::Type) {
		// Types are compile-time only — no runtime code
		return nullptr;
	}

	if (kind == IntrinsicKind::SizeOf) {
		DataType typeArgType = getEffectiveType(context, args[1]);
		if (typeArgType.kind != DataType::Kind::Type)
			return nullptr;
		DataType valueType = typeArgType.toReferencedType();
		if (valueType.kind == DataType::Kind::Class && valueType.classDefinition && valueType.classInstIndex < 0 &&
			!valueType.classDefinition->instantiations.empty()) {
			valueType.classInstIndex = 0;
		}
		return builder.getInt64(valueType.getByteSize());
	}

	if (kind == IntrinsicKind::BuildInfo || kind == IntrinsicKind::TargetIs || kind == IntrinsicKind::ShaderStageIs) {
		CompileTimeValue value =
			resolveStoredCompileTimeValue(context, callExpr, context.flexBindingFrames, context.currentCodegenInstantiation);
		if (auto *number = std::get_if<double>(&value)) {
			llvm::Type *llvmType = getLLVMType(context, resultType);
			return llvm::ConstantInt::get(llvmType, static_cast<std::int64_t>(*number), true);
		}
		if (auto *boolean = std::get_if<bool>(&value)) {
			llvm::Type *llvmType = getLLVMType(context, resultType);
			return llvm::ConstantInt::get(llvmType, *boolean ? 1 : 0, false);
		}
		return nullptr;
	}

	if (kind == IntrinsicKind::TypeOf || kind == IntrinsicKind::Array) {
		// Compile-time only — no runtime code
		return nullptr;
	}

	if (kind == IntrinsicKind::Select) {
		if (!args[1])
			crashCompilerBug("select intrinsic missing condition argument during codegen");
		CompileTimeValue conditionValue =
			resolveStoredCompileTimeValue(context, args[1], context.flexBindingFrames, context.currentCodegenInstantiation);
		auto *condition = std::get_if<bool>(&conditionValue);
		if (condition)
			return generateExpressionCode(context, args[*condition ? 2 : 3]);
		return buildRuntimeSelect(context, args, resultType);
	}

	if (kind == IntrinsicKind::AddPointerDepth) {
		// Compile-time only — modifies TypeReference, no runtime code
		return nullptr;
	}

	if (kind == IntrinsicKind::Construct) {
		if (resultType.kind == DataType::Kind::Array) {
			llvm::Type *arrayType = getLLVMType(context, resultType);
			llvm::AllocaInst *alloca = createEntryAlloca(context, "array_tmp", resultType);
			DataType elementType = *resultType.arrayElementType;
			for (size_t i = 2; i < args.size(); i++) {
				llvm::Value *elementVal = generateExpressionCode(context, args[i]);
				DataType fromType = getEffectiveType(context, args[i]);
				elementVal = ensureType(context, elementVal, fromType, elementType);
				llvm::Value *elementPtr =
					builder.CreateGEP(arrayType, alloca, {builder.getInt64(0), builder.getInt64(i - 2)}, "array_elem_ptr");
				builder.CreateStore(elementVal, elementPtr);
			}
			return builder.CreateAlignedLoad(arrayType, alloca, llvm::Align(8), "array_load");
		}

		if (resultType.kind == DataType::Kind::Vector) {
			return buildVectorValue(context, resultType, args, 2);
		}

		if (resultType.kind == DataType::Kind::Matrix) {
			if (args.size() == 3)
				return buildMatrixFromFlatArray(context, resultType, args[2]);
			return buildMatrixFromScalarArgs(context, resultType, args, 2);
		}

		if (resultType.kind != DataType::Kind::Class) {
			llvm::Value *val = generateExpressionCode(context, args[2]);
			DataType fromType = getEffectiveType(context, args[2]);
			return ensureType(context, val, fromType, resultType);
		}

		ClassDefinition *classDef = resultType.classDefinition;
		DataType concreteType = resultType;
		if (concreteType.classInstIndex < 0) {
			std::vector<DataType> fieldTypes;
			fieldTypes.reserve(args.size() - 2);
			for (size_t i = 2; i < args.size(); i++)
				fieldTypes.push_back(getEffectiveType(context, args[i]));
			concreteType.classInstIndex = classDef->getOrCreateInstantiation(fieldTypes);
		}
		assert(concreteType.classInstIndex >= 0 && "class construct result type must have a concrete instantiation");
		ClassInstantiation &inst = classDef->instantiations[concreteType.classInstIndex];
		llvm::Type *structType = getLLVMType(context, concreteType);

		llvm::AllocaInst *alloca = createEntryAlloca(context, "class_tmp", concreteType);

		for (size_t i = 0; i < inst.fieldTypes.size(); i++) {
			llvm::Value *fieldVal = generateExpressionCode(context, args[i + 2]);
			DataType fieldFromType = getEffectiveType(context, args[i + 2]);
			fieldVal = ensureType(context, fieldVal, fieldFromType, inst.fieldTypes[i]);
			unsigned llvmFieldIndex = static_cast<unsigned>(i);
			if (i < inst.llvmFieldIndices.size())
				llvmFieldIndex = inst.llvmFieldIndices[i];
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, alloca, llvmFieldIndex, "field_ptr");
			builder.CreateStore(fieldVal, fieldPtr);
		}

		return builder.CreateAlignedLoad(structType, alloca, llvm::Align(8), "struct_load");
	}

	if (kind == IntrinsicKind::Property) {
		// Format: args[1]=instance, args[2]=fieldname (string literal from {word:} capture)
		Expression *ownerExpr = args[1];
		DataType ownerType = getEffectiveType(context, ownerExpr);
		bool ownerIsClassPointer = ownerType.isPointer() && ownerType.kind == DataType::Kind::Class;
		DataType instType = ownerIsClassPointer ? ownerType.dereferenced() : ownerType;
		if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex < 0 &&
			!instType.classDefinition->instantiations.empty()) {
			instType.classInstIndex = 0;
		}
		ClassDefinition *classDef = instType.classDefinition;

		// Get field name from string literal
		Expression *propExpr = resolveVariableBinding(context, args[2]);
		std::string fieldName = getStringLiteral(propExpr);

		// C strings expose a synthetic "data" property so string-library flexes can
		// operate on both heap strings and string literals without duplicating logic.
		if (fieldName == "data" && instType.isBytePointer())
			return generateExpressionCode(context, ownerExpr);

		if (!classDef) {
			context.diagnostics.push_back(Diagnostic(
				context, Diagnostic::Level::Error, "class has no properties", args[1]->range, "type", instType.toString()
			));
			return nullptr;
		}

		// Find field index
		int fieldIdx = -1;
		for (size_t i = 0; i < classDef->fields.size(); i++) {
			if (classDef->fields[i].name == fieldName) {
				fieldIdx = i;
				break;
			}
		}

		if (fieldIdx == -1) {
			context.diagnostics.push_back(Diagnostic(
				context, Diagnostic::Level::Error, "class missing property", args[1]->range, "type", instType.toString(),
				"property", fieldName
			));
			return nullptr;
		}

		DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
		if (ownerIsClassPointer) {
			llvm::Value *instPtrValue = generateExpressionCode(context, ownerExpr);
			if (!instPtrValue)
				return nullptr;
			llvm::Type *structType = getLLVMType(context, instType);
			unsigned llvmFieldIdx = static_cast<unsigned>(fieldIdx);
			if (instType.classInstIndex >= 0 && instType.classInstIndex < static_cast<int>(classDef->instantiations.size()) &&
				fieldIdx < static_cast<int>(classDef->instantiations[instType.classInstIndex].llvmFieldIndices.size())) {
				llvmFieldIdx = classDef->instantiations[instType.classInstIndex].llvmFieldIndices[fieldIdx];
			}
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, instPtrValue, llvmFieldIdx, "field_ptr");
			return builder.CreateAlignedLoad(getLLVMType(context, fieldType), fieldPtr, llvm::Align(8), fieldName + "_val");
		}

		if (llvm::Value *instPtr = getVariablePointer(context, ownerExpr)) {
			llvm::Type *structType = getLLVMType(context, instType);
			unsigned llvmFieldIdx = static_cast<unsigned>(fieldIdx);
			if (instType.classInstIndex >= 0 && instType.classInstIndex < static_cast<int>(classDef->instantiations.size()) &&
				fieldIdx < static_cast<int>(classDef->instantiations[instType.classInstIndex].llvmFieldIndices.size())) {
				llvmFieldIdx = classDef->instantiations[instType.classInstIndex].llvmFieldIndices[fieldIdx];
			}
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, instPtr, llvmFieldIdx, "field_ptr");
			return builder.CreateAlignedLoad(getLLVMType(context, fieldType), fieldPtr, llvm::Align(8), fieldName + "_val");
		}

		llvm::Value *instValue = generateExpressionCode(context, ownerExpr);
		if (!instValue)
			return nullptr;
		return builder.CreateExtractValue(instValue, {static_cast<unsigned>(fieldIdx)}, fieldName + "_val");
	}

	// Shader I/O intrinsics (only available in --emit-spirv mode)
	if (kind == IntrinsicKind::ShaderInput) {
		// @intrinsic("shader input", globalName) → load vec4 from named shader input global
		std::string inputName = getStringLiteral(args[1]);
		std::string globalName;
		if (inputName == "FragCoord")
			globalName = "gl_FragCoord";
		else if (inputName == "Position")
			globalName = "in_Position";
		else {
			context.diagnostics.push_back(
				Diagnostic(context, Diagnostic::Level::Error, "unknown shader input", Range(), "name", inputName)
			);
			return nullptr;
		}
		llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
		if (!global) {
			context.diagnostics.push_back(
				Diagnostic(context, Diagnostic::Level::Error, "shader input unavailable", args[1]->range, "name", inputName)
			);
			return nullptr;
		}
		llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
		return builder.CreateLoad(vec4Ty, global, inputName);
	}

	if (kind == IntrinsicKind::ShaderUniform) {
		// @intrinsic("shader uniform", uniformName) → load f32 from named uniform global
		// The SPIR-V patcher wraps this in a UBO struct with proper decorations
		std::string uniformName = getStringLiteral(args[1]);
		assert(!uniformName.empty() && "shader uniform requires a string literal name");
		std::string globalName = "ubo_" + uniformName;
		llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
		if (!global) {
			llvm::Type *f32Ty = builder.getFloatTy();
			global = new llvm::GlobalVariable(
				*context.llvmModule, f32Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, globalName, nullptr,
				llvm::GlobalValue::NotThreadLocal, 3
			);
			global->setInitializer(llvm::Constant::getNullValue(f32Ty));
			context.registerShaderUniformName(uniformName);
		}
		return builder.CreateLoad(builder.getFloatTy(), global, uniformName + "_val");
	}

	if (kind == IntrinsicKind::ShaderOutput) {
		// @intrinsic("shader output", r, g, b, a) → store vec4 to shader output global
		llvm::Value *r = generateExpressionCode(context, args[1]);
		llvm::Value *g = generateExpressionCode(context, args[2]);
		llvm::Value *b = generateExpressionCode(context, args[3]);
		llvm::Value *a = generateExpressionCode(context, args[4]);
		if (!r || !g || !b || !a)
			return nullptr;

		DataType rType = getEffectiveType(context, args[1]);
		DataType gType = getEffectiveType(context, args[2]);
		DataType bType = getEffectiveType(context, args[3]);
		DataType aType = getEffectiveType(context, args[4]);
		DataType f32 = {DataType::Kind::Float, 4};
		r = ensureType(context, r, rType, f32);
		g = ensureType(context, g, gType, f32);
		b = ensureType(context, b, bType, f32);
		a = ensureType(context, a, aType, f32);

		llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
		llvm::Value *color = llvm::UndefValue::get(vec4Ty);
		color = builder.CreateInsertElement(color, r, getVectorLaneIndexValue(context, 0), "color_r");
		color = builder.CreateInsertElement(color, g, getVectorLaneIndexValue(context, 1), "color_g");
		color = builder.CreateInsertElement(color, b, getVectorLaneIndexValue(context, 2), "color_b");
		color = builder.CreateInsertElement(color, a, getVectorLaneIndexValue(context, 3), "color_a");

		// Find the output global: gl_FragColor (fragment) or gl_Position (vertex)
		std::string outName =
			(context.options.shaderStage == ParseContext::ShaderStage::Vertex) ? "gl_Position" : "gl_FragColor";
		llvm::GlobalVariable *outGlobal = context.llvmModule->getGlobalVariable(outName);
		if (!outGlobal) {
			context.diagnostics.push_back(Diagnostic(
				context, Diagnostic::Level::Error, "shader output unavailable",
				Range(args[1]->range.line, args[1]->range.start(), args[4]->range.end())
			));
			return nullptr;
		}
		builder.CreateStore(color, outGlobal);
		return nullptr;
	}

	if (kind == IntrinsicKind::ExtractElement) {
		// @intrinsic("extract element", vector, index) → extract scalar from vector
		llvm::Value *vec = generateExpressionCode(context, args[1]);
		if (!vec)
			return nullptr;
		if (auto *idxLit = std::get_if<double>(&args[2]->literalValue)) {
			return builder.CreateExtractElement(vec, getVectorLaneIndexValue(context, static_cast<unsigned>(*idxLit)), "elem");
		}
		llvm::Value *idx = generateExpressionCode(context, args[2]);
		if (!idx)
			return nullptr;
		idx = ensureType(context, idx, getEffectiveType(context, args[2]), {DataType::Kind::Int, 4});
		return builder.CreateExtractElement(vec, idx, "elem");
	}

	std::string uri =
		(callExpr && callExpr->range.line && callExpr->range.line->sourceFile) ? callExpr->range.line->sourceFile->uri : "";
	int line = (callExpr && callExpr->range.line) ? callExpr->range.line->sourceFileLineIndex + 1 : -1;
	crashUnimplementedIntrinsic("codegen", name, uri, line);
}
