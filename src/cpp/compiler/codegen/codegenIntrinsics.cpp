#include "arithmeticTypePromotion.h"
#include "atomicIntrinsics.h"
#include "classDefinition.h"
#include "codegenInternal.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "executionTrace.h"
#include "intrinsicInfo.h"
#include "nativeScalarABI.h"
#include "sectionFlexBody.h"
#include "spirv.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <algorithm>
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
	if (indexType.isInteger())
		return ensureType(context, indexVal, indexType, {DataType::Kind::UInt, 8});
	return indexVal;
}

#include "codegenConditional.inl"

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
	DataType elementType = resultType.numericElementType();
	if (elementType.kind == DataType::Kind::Float) {
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
		return elementType.isUnsignedInteger() ? builder.CreateUDiv(left, right, "div")
											   : builder.CreateSDiv(left, right, "div");
	case ArithmeticIntrinsicKind::Modulo:
		return elementType.isUnsignedInteger() ? builder.CreateURem(left, right, "mod")
											   : builder.CreateSRem(left, right, "mod");
	default:
		return nullptr;
	}
}

static llvm::Value *
generateScalarBitwise(ParseContext &context, IntrinsicKind kind, llvm::Value *left, llvm::Value *right, DataType resultType) {
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
		return resultType.numericElementType().isUnsignedInteger() ? builder.CreateLShr(left, right, "shr")
																   : builder.CreateAShr(left, right, "shr");
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

static DataType mathComputationType(DataType resultType, int maximumFloatBytes) {
	if (resultType.kind == DataType::Kind::Float) {
		resultType.numericSize = std::min(resultType.numericSize, maximumFloatBytes);
		return resultType;
	}
	if (resultType.isInteger())
		return {DataType::Kind::Float, maximumFloatBytes};
	if (resultType.kind == DataType::Kind::Vector && resultType.arrayElementType) {
		DataType computationType = resultType;
		computationType.arrayElementType =
			std::make_shared<DataType>(mathComputationType(*resultType.arrayElementType, maximumFloatBytes));
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

// Intrinsic operands recurse through the dispatcher. Handler calls keep
// unrelated operation temporaries outside those recursive dispatch frames.
static bool generateIntrinsicRuntimeValue(ParseContext &context, Expression *expression, llvm::Value *&value) {
	CodegenResult result = generateExpressionCode(context, expression);
	if (!result)
		return false;
	value = result.value;
	return true;
}

#include "codegenAtomicIntrinsics.inl"
#include "codegenIntrinsicAggregates.inl"
#include "codegenIntrinsicCalls.inl"
#include "codegenIntrinsicControlFlow.inl"
#include "codegenIntrinsicMemory.inl"
#include "codegenIntrinsicNumeric.inl"
#include "codegenIntrinsicTypes.inl"

CodegenResult generateIntrinsicCode(
	ParseContext &context, Expression *callExpr, const std::string &name, const std::vector<Expression *> &allArguments,
	DataType resultType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (allArguments.empty())
		crashCompilerBug("intrinsic call is missing the intrinsic-name argument");
	const std::vector<Expression *> &args = allArguments;
	IntrinsicKind kind = intrinsicKind(name);
	if (kind == IntrinsicKind::LifecycleValue) {
		if (!context.managedLifecycleValueBinding)
			crashCompilerBug("lifecycle value reached codegen without its managed value binding");
		return builder.CreateLoad(getLLVMType(context, resultType), context.managedLifecycleValueBinding, "lifecycle_value");
	}
	if (kind == IntrinsicKind::CommandLineArgumentCount || kind == IntrinsicKind::CommandLineArgumentValues)
		return generateCommandLineArgumentsIntrinsic(context, kind);
	if (kind == IntrinsicKind::SetSubject)
		return generateSetSubjectIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::Subject)
		return generateSubjectIntrinsic(context, callExpr, resultType);
	if (kind == IntrinsicKind::Discard)
		return generateDiscardIntrinsic(context, args);
	if (kind == IntrinsicKind::CheckDeallocation)
		return nullptr;
	if (kind == IntrinsicKind::Store)
		return generateStoreIntrinsic(context, args);
	ArithmeticIntrinsicKind arithmeticOp = arithmeticIntrinsicKind(name);
	if (isArithmeticIntrinsic(arithmeticOp))
		return generateArithmeticIntrinsic(context, args, resultType, arithmeticOp);
	if (kind == IntrinsicKind::BitwiseNot)
		return generateBitwiseNotIntrinsic(context, args, resultType);
	if (kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor ||
		kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight)
		return generateBinaryBitwiseIntrinsic(context, args, resultType, kind);
	if (isComparisonIntrinsicKind(kind))
		return generateComparisonIntrinsic(context, args, resultType, kind);
	if (kind == IntrinsicKind::Not)
		return generateNotIntrinsic(context, args);
	if (kind == IntrinsicKind::Negate)
		return generateNegateIntrinsic(context, args);
	if (kind == IntrinsicKind::Min || kind == IntrinsicKind::Max)
		return generateMinMaxIntrinsic(context, callExpr, args, kind);
	if (isMathFunction(name))
		return generateMathIntrinsic(context, name, args, resultType, kind);
	if (kind == IntrinsicKind::AddressOf)
		return generateAddressOfIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::Dereference)
		return generateDereferenceIntrinsic(context, args);
	if (kind == IntrinsicKind::StoreAt || kind == IntrinsicKind::InitializeAt)
		return generatePointerStoreIntrinsic(context, args, kind);
	if (kind == IntrinsicKind::DestroyAt)
		return generateDestroyAtIntrinsic(context, args);
	if (isAtomicIntrinsicKind(kind))
		return generateAtomicIntrinsic(context, args, kind);
	if (kind == IntrinsicKind::ExecuteBody)
		return generateExecuteBodyIntrinsic(context, callExpr);
	if (kind == IntrinsicKind::LoopWhile)
		return generateLoopWhileIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::If)
		return generateIfIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::Else || kind == IntrinsicKind::ElseIf)
		return generateAlternativeIntrinsic(context, callExpr, args, kind);
	if (kind == IntrinsicKind::Switch)
		return generateSwitchIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::Case)
		return generateCaseIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::DefaultCase)
		return generateDefaultCaseIntrinsic(context, callExpr);
	if (kind == IntrinsicKind::Return)
		return generateReturnIntrinsic(context, args);
	if (isExternalCallIntrinsicKind(kind))
		return generateExternalCallIntrinsic(context, args, kind);
	if (kind == IntrinsicKind::Function)
		return generateFunctionIntrinsic(context, callExpr);
	if (kind == IntrinsicKind::Cast)
		return generateCastIntrinsic(context, args);
	if (kind == IntrinsicKind::Type || kind == IntrinsicKind::Fix) {
		// Meta-values are compile-time only — no runtime code.
		return nullptr;
	}
	if (kind == IntrinsicKind::SizeOf)
		return generateSizeOfIntrinsic(context, args);
	if (kind == IntrinsicKind::BuildInfo || kind == IntrinsicKind::TargetIs || kind == IntrinsicKind::ShaderStageIs ||
		kind == IntrinsicKind::TypeExtent)
		return generateCompileTimeResultIntrinsic(context, callExpr, resultType);
	if (kind == IntrinsicKind::TypeOf || kind == IntrinsicKind::Array) {
		// Compile-time only — no runtime code
		return nullptr;
	}
	if (auto conditional = conditionalOperands(kind))
		return buildConditionalValue(context, args, resultType, *conditional);
	if (kind == IntrinsicKind::AddPointerDepth) {
		// Compile-time only — modifies TypeReference, no runtime code
		return nullptr;
	}
	if (kind == IntrinsicKind::Construct)
		return generateConstructIntrinsic(context, args, resultType);
	if (kind == IntrinsicKind::Property)
		return generatePropertyIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::ShaderInput || kind == IntrinsicKind::ShaderInterpolantInput)
		return generateShaderInputIntrinsic(context, args, kind);
	if (kind == IntrinsicKind::ShaderUniform)
		return generateShaderUniformIntrinsic(context, callExpr, args);
	if (kind == IntrinsicKind::ShaderOutput || kind == IntrinsicKind::ShaderInterpolantOutput)
		return generateShaderOutputIntrinsic(context, args, kind);
	if (kind == IntrinsicKind::ExtractElement)
		return generateExtractElementIntrinsic(context, args);
	if (kind == IntrinsicKind::InsertElement)
		return generateInsertElementIntrinsic(context, args);
	std::string uri =
		(callExpr && callExpr->range.line && callExpr->range.line->sourceFile) ? callExpr->range.line->sourceFile->uri : "";
	int line = (callExpr && callExpr->range.line) ? callExpr->range.line->sourceFileLineIndex + 1 : -1;
	crashUnimplementedIntrinsic("codegen", name, uri, line);
}
