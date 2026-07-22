#include "classDefinition.h"
#include "codegenInternal.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "intrinsicInfo.h"
#include "sectionFlexBody.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/GlobalVariable.h"
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

static llvm::Value *
createClassFieldPointer(ParseContext &context, const DataType &classType, int fieldIndex, llvm::Value *classPointer) {
	requireCompilerInvariant(classPointer != nullptr, "class field access requires an instance pointer");
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	return builder.CreateStructGEP(
		getLLVMType(context, classType), classPointer, getClassFieldLLVMIndex(context, classType, fieldIndex), "field_ptr"
	);
}

static llvm::Value *buildRuntimeSelect(ParseContext &context, const std::vector<Expression *> &args, DataType resultType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *function = builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
	if (!function)
		return nullptr;

	DataType conditionType = finalizedExpressionType(context, args[1]);
	if (conditionType.kind != DataType::Kind::Bool)
		crashCompilerBug("runtime select condition must be boolean after type inference");
	llvm::Value *conditionValue = generateExpressionCode(context, args[1]);
	if (!conditionValue)
		return nullptr;

	if (resultType.isMetaType()) {
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
			DataType trueType = finalizedExpressionType(context, args[2]);
			trueValue = ensureType(context, trueValue, trueType, resultType);
			if (typeHasManagedLifecycle(resultType) && !managedExpressionResultIsOwned(context, args[2]))
				retainManagedValue(context, resultType, trueValue);
			incomingValues.push_back({trueValue, trueEndBlock});
		}
		builder.CreateBr(mergeBlock);
	}

	builder.SetInsertPoint(falseBlock);
	llvm::Value *falseValue = generateExpressionCode(context, args[3]);
	llvm::BasicBlock *falseEndBlock = builder.GetInsertBlock();
	if (!falseEndBlock->getTerminator()) {
		if (resultType.kind != DataType::Kind::Void) {
			DataType falseType = finalizedExpressionType(context, args[3]);
			falseValue = ensureType(context, falseValue, falseType, resultType);
			if (typeHasManagedLifecycle(resultType) && !managedExpressionResultIsOwned(context, args[3]))
				retainManagedValue(context, resultType, falseValue);
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
		DataType fromType = finalizedExpressionType(context, args[startIndex + i]);
		elementValue = ensureType(context, elementValue, fromType, elementType);
		vectorValue = builder.CreateInsertElement(vectorValue, elementValue, getVectorLaneIndexValue(context, i), "vec_ins");
	}
	return vectorValue;
}

static llvm::Value *buildMatrixFromFlatArray(ParseContext &context, DataType matrixType, Expression *sourceExpr) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType sourceType = finalizedExpressionType(context, sourceExpr);
	if (sourceType.kind != DataType::Kind::Array || !sourceType.arrayElementType)
		return nullptr;
	if (sourceType.arraySize != matrixType.matrixRows() * matrixType.matrixColumns())
		return nullptr;

	llvm::Value *flatArrayValue = generateExpressionCode(context, sourceExpr);
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
llvm::Value *generateIntrinsicCode(
	ParseContext &context, Expression *callExpr, const std::string &name, const std::vector<Expression *> &allArguments,
	DataType resultType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (allArguments.empty())
		crashCompilerBug("intrinsic call is missing the intrinsic-name argument");
	const std::vector<Expression *> &args = allArguments;
	IntrinsicKind kind = intrinsicKind(name);
	if (kind == IntrinsicKind::LifecycleValue) {
		auto value = context.patternBindings.find(std::string(managedLifecycleParameterName));
		if (value == context.patternBindings.end() || !value->second)
			crashCompilerBug("lifecycle value reached codegen without its managed value binding");
		return builder.CreateLoad(getLLVMType(context, resultType), value->second, "lifecycle_value");
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
		llvm::Value *value = generateExpressionCode(context, args[1]);
		if (!value)
			crashCompilerBug("subject assignment reached codegen without a runtime value");
		llvm::AllocaInst *storage = createEntryAlloca(context, "subject", valueType);
		if (typeHasManagedLifecycle(valueType)) {
			Section *ownerSection = currentFunctionLifetimeSection(context);
			requireCompilerInvariant(ownerSection != nullptr, "managed subject assignment has no owning function section");
			registerManagedStorage(context, storage, valueType, ownerSection);
			if (!managedExpressionResultIsOwned(context, args[1]))
				retainManagedValue(context, valueType, value);
			initializeManagedStorage(context, storage, valueType, value);
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
		if (typeHasManagedLifecycle(resultType))
			retainManagedValue(context, resultType, value);
		return value;
	}

	if (kind == IntrinsicKind::Discard) {
		// Evaluate the argument for side effects and discard the result
		llvm::Value *value = generateExpressionCode(context, args[1]);
		DataType valueType = finalizedExpressionType(context, args[1]);
		if (managedExpressionResultIsOwned(context, args[1]))
			releaseManagedValue(context, valueType, value);
		return nullptr;
	}

	if (kind == IntrinsicKind::Store) {
		// Generate the value in the current (original) flex scope first,
		// before resolving the destination which may cross scope boundaries.
		DataType valType = finalizedExpressionType(context, args[2]);
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
			DataType ownerType = finalizedExpressionType(context, instExpr);
			bool ownerIsClassPointer = ownerType.kind == DataType::Kind::Class && ownerType.isPointer();
			DataType instType = ownerIsClassPointer ? ownerType.dereferenced() : ownerType;
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

			llvm::Value *instPtr =
				ownerIsClassPointer ? generateExpressionCode(context, instExpr) : getVariablePointer(context, instExpr);
			llvm::Value *fieldPtr = createClassFieldPointer(context, instType, fieldIdx, instPtr);

			DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
			val = ensureType(context, val, valType, fieldType);
			if (typeHasManagedLifecycle(fieldType)) {
				if (!managedExpressionResultIsOwned(context, args[2]))
					retainManagedValue(context, fieldType, val);
				storeManagedValue(context, fieldPtr, fieldType, val);
			} else {
				builder.CreateStore(val, fieldPtr);
			}
			context.flexBindingFrames = savedBindingFrames;
		} else {
			// Restore scope state — the else branch evaluates args[1] directly
			context.flexBindingFrames = savedBindingFrames;

			llvm::Value *ptr = getVariablePointer(context, destExpr);
			// A silently skipped store would corrupt program behavior far from
			// the cause; storage for every reachable destination must exist.
			if (!ptr)
				crashCompilerBug("store destination reached codegen without storage");
			if (!val)
				crashCompilerBug("store value reached codegen without a generated value");
			{
				DataType destType = finalizedExpressionType(context, destExpr);
				if (typeHasManagedLifecycle(destType)) {
					val = ensureType(context, val, valType, destType);
					if (!managedExpressionResultIsOwned(context, args[2]))
						retainManagedValue(context, destType, val);
					storeManagedValue(context, ptr, destType, val);
				} else if (destType.kind == DataType::Kind::Class && !destType.isPointer() && destType.classDefinition &&
						   destType.classInstIndex >= 0) {
					ClassDefinition *classDef = destType.classDefinition;
					auto &destFields = classDef->instantiations[destType.classInstIndex].fieldTypes;
					auto &srcFields = valType.classDefinition
										  ? valType.classDefinition->instantiations[valType.classInstIndex].fieldTypes
										  : destFields;
					llvm::Value *srcPtr = val;
					if (srcPtr && !srcPtr->getType()->isPointerTy()) {
						llvm::AllocaInst *tmpStruct = createEntryAlloca(context, "struct_src", valType);
						builder.CreateAlignedStore(srcPtr, tmpStruct, getLLVMABIAlignment(context, valType));
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
						llvm::Value *srcVal =
							builder.CreateAlignedLoad(structType, srcPtr, getLLVMABIAlignment(context, valType), "struct_load");
						builder.CreateAlignedStore(srcVal, ptr, getLLVMABIAlignment(context, destType));
					} else {
						// Different field types — element-wise copy with conversion
						llvm::Type *srcStructType = getLLVMType(context, valType);
						llvm::Type *destStructType = getLLVMType(context, destType);
						for (size_t i = 0; i < destFields.size(); i++) {
							llvm::Value *srcFieldPtr = builder.CreateStructGEP(
								srcStructType, srcPtr, getClassFieldLLVMIndex(context, valType, static_cast<int>(i)),
								"src_field"
							);
							llvm::Value *fieldVal =
								builder.CreateLoad(getLLVMType(context, srcFields[i]), srcFieldPtr, "field_val");
							fieldVal = ensureType(context, fieldVal, srcFields[i], destFields[i]);
							llvm::Value *destFieldPtr = builder.CreateStructGEP(
								destStructType, ptr, getClassFieldLLVMIndex(context, destType, static_cast<int>(i)),
								"dest_field"
							);
							builder.CreateStore(fieldVal, destFieldPtr);
						}
					}
				} else {
					val = ensureType(context, val, valType, destType);
					builder.CreateAlignedStore(val, ptr, getLLVMABIAlignment(context, destType));
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
		DataType leftType = finalizedExpressionType(context, args[1]);
		DataType rightType = finalizedExpressionType(context, args[2]);

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
		llvm::Value *value = generateExpressionCode(context, args[1]);
		DataType valueType = finalizedExpressionType(context, args[1]);
		value = ensureType(context, value, valueType, resultType);
		return builder.CreateNot(value, "bnot");
	}

	if (kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor ||
		kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
		DataType leftType = finalizedExpressionType(context, args[1]);
		DataType rightType = finalizedExpressionType(context, args[2]);

		left = ensureType(context, left, leftType, resultType);
		right = ensureType(context, right, rightType, resultType);
		return generateScalarBitwise(context, kind, left, right);
	}

	// Comparison intrinsics
	if (isComparisonIntrinsicKind(kind)) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
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
			requireCompilerInvariant(
				DataType::promoteArithmetic(leftType, rightType, promoted),
				"comparison operands accepted by inference have no common codegen type"
			);
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
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
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
		llvm::Value *val = generateExpressionCode(context, args[1]);
		DataType valType = finalizedExpressionType(context, args[1]);
		if (valType.kind != DataType::Kind::Bool)
			crashCompilerBug("logical not operand must be boolean after type inference");

		return builder.CreateXor(val, builder.getTrue(), "not");
	}

	// Negate
	if (kind == IntrinsicKind::Negate) {
		llvm::Value *val = generateExpressionCode(context, args[1]);
		DataType valType = finalizedExpressionType(context, args[1]);
		if (valType.kind == DataType::Kind::Float)
			return builder.CreateFNeg(val, "fneg");
		return builder.CreateNeg(val, "neg");
	}

	if (kind == IntrinsicKind::Min || kind == IntrinsicKind::Max) {
		llvm::Value *left = generateExpressionCode(context, args[1]);
		llvm::Value *right = generateExpressionCode(context, args[2]);
		DataType leftType = finalizedExpressionType(context, args[1]);
		DataType rightType = finalizedExpressionType(context, args[2]);
		DataType promoted;
		if (!DataType::promoteArithmetic(leftType, rightType, promoted)) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "min max requires arithmetic operands",
				intrinsicDiagnosticRange(context, callExpr)
			));
			return nullptr;
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
				DataType valType = finalizedExpressionType(context, args[1]);
				if (valType != computationType)
					val = ensureType(context, val, valType, computationType);
				llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, intrinsicId, {val->getType()});
				llvm::Value *computed = builder.CreateCall(fn, {val}, name);
				return ensureType(context, computed, computationType, resultType);
			}
			llvm::Value *left = generateExpressionCode(context, args[1]);
			llvm::Value *right = generateExpressionCode(context, args[2]);
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
			llvm::Value *y = generateExpressionCode(context, args[1]);
			llvm::Value *x = generateExpressionCode(context, args[2]);
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
		llvm::Value *ptr = getVariablePointer(context, args[1]);
		if (!ptr) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "address of requires variable", args[1] ? args[1]->range : Range()
			));
			return nullptr;
		}
		return ptr;
	}

	if (kind == IntrinsicKind::Dereference) {
		llvm::Value *ptrVal = generateExpressionCode(context, args[1]);
		DataType ptrType = finalizedExpressionType(context, args[1]);
		DataType elemType = ptrType.dereferenced();
		llvm::Type *elemLLVMType = getLLVMType(context, elemType);
		return builder.CreateAlignedLoad(elemLLVMType, ptrVal, getLLVMABIAlignment(context, elemType), "deref");
	}

	// Pointer storage intrinsics
	if (kind == IntrinsicKind::StoreAt || kind == IntrinsicKind::InitializeAt) {
		llvm::Value *ptr = generateExpressionCode(context, args[1]);
		llvm::Value *value = generateExpressionCode(context, args[2]);
		DataType ptrType = finalizedExpressionType(context, args[1]);
		requireCompilerInvariant(ptrType.isPointer(), "pointer store reached codegen with a non-pointer destination");
		DataType elementType = ptrType.dereferenced();
		value = ensureType(context, value, finalizedExpressionType(context, args[2]), elementType);
		if (typeHasManagedLifecycle(elementType)) {
			if (!managedExpressionResultIsOwned(context, args[2]))
				retainManagedValue(context, elementType, value);
			if (kind == IntrinsicKind::StoreAt)
				storeManagedValue(context, ptr, elementType, value);
			else
				builder.CreateAlignedStore(value, ptr, getLLVMABIAlignment(context, elementType));
		} else {
			builder.CreateAlignedStore(value, ptr, getLLVMABIAlignment(context, elementType));
		}
		return nullptr;
	}

	if (kind == IntrinsicKind::DestroyAt) {
		llvm::Value *ptr = generateExpressionCode(context, args[1]);
		DataType ptrType = finalizedExpressionType(context, args[1]);
		requireCompilerInvariant(ptrType.isPointer(), "destroy at reached codegen with a non-pointer destination");
		DataType elementType = ptrType.dereferenced();
		if (typeHasManagedLifecycle(elementType)) {
			llvm::Value *value = builder.CreateAlignedLoad(
				getLLVMType(context, elementType), ptr, getLLVMABIAlignment(context, elementType), "destroy_value"
			);
			releaseManagedValue(context, elementType, value);
		}
		return nullptr;
	}

#include "codegenIntrinsicEffects.inl"
