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
#include <unordered_map>

// Helper to extract string literal from function
std::string getStringLiteral(Function *expr) {
	if (expr && expr->kind == Function::Kind::Literal) {
		if (auto *str = std::get_if<std::string>(&expr->literalValue))
			return *str;
	}
	return "";
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
buildVectorValue(ParseContext &context, DataType vectorType, const std::vector<Function *> &args, size_t startIndex) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *llvmVectorType = getLLVMType(context, vectorType);
	llvm::Value *vectorValue = llvm::Constant::getNullValue(llvmVectorType);
	DataType elementType = vectorType.vectorElementType();
	for (int i = 0; i < vectorType.vectorSize(); i++) {
		llvm::Value *elementValue = generateFunctionCode(context, args[startIndex + i]);
		DataType fromType = getEffectiveType(context, args[startIndex + i]);
		elementValue = ensureType(context, elementValue, fromType, elementType);
		vectorValue = builder.CreateInsertElement(vectorValue, elementValue, static_cast<uint64_t>(i), "vec_ins");
	}
	return vectorValue;
}

static llvm::Value *buildMatrixFromFlatArray(ParseContext &context, DataType matrixType, Function *sourceExpr) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType sourceType = getEffectiveType(context, sourceExpr);
	if (sourceType.kind != DataType::Kind::Array || !sourceType.arrayElementType)
		return nullptr;
	if (sourceType.arraySize != matrixType.matrixRows() * matrixType.matrixColumns())
		return nullptr;

	llvm::Value *flatArrayValue = generateFunctionCode(context, sourceExpr);
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
			rowValue = builder.CreateInsertElement(rowValue, elementValue, static_cast<uint64_t>(column), "mat_row_ins");
		}
		matrixValue = builder.CreateInsertValue(matrixValue, rowValue, {static_cast<unsigned>(row)}, "mat_ins");
	}

	return matrixValue;
}

static llvm::Value *
buildMatrixFromScalarArgs(ParseContext &context, DataType matrixType, const std::vector<Function *> &args, size_t startIndex) {
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
			llvm::Value *elementValue = generateFunctionCode(context, args[argIndex]);
			DataType fromType = getEffectiveType(context, args[argIndex]);
			elementValue = ensureType(context, elementValue, fromType, elementType);
			rowValue = builder.CreateInsertElement(rowValue, elementValue, static_cast<uint64_t>(column), "mat_row_ins");
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
			llvm::Value *lane = builder.CreateExtractElement(product, static_cast<uint64_t>(column), "mat_vec_lane");
			sum = builder.CreateFAdd(sum, lane, "mat_vec_sum");
		}
		resultValue = builder.CreateInsertElement(resultValue, sum, static_cast<uint64_t>(row), "mat_vec_res");
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
				llvm::Value *leftLane = builder.CreateExtractElement(leftRow, static_cast<uint64_t>(inner), "mat_mul_left");
				llvm::Value *rightRow =
					builder.CreateExtractValue(rightValue, {static_cast<unsigned>(inner)}, "mat_mul_right_row");
				llvm::Value *rightLane = builder.CreateExtractElement(rightRow, static_cast<uint64_t>(column), "mat_mul_right");
				sum = builder.CreateFAdd(sum, builder.CreateFMul(leftLane, rightLane), "mat_mul_sum");
			}
			resultRow = builder.CreateInsertElement(resultRow, sum, static_cast<uint64_t>(column), "mat_mul_row_ins");
		}
		resultValue = builder.CreateInsertValue(resultValue, resultRow, {static_cast<unsigned>(row)}, "mat_mul_ins");
	}
	return resultValue;
}

// Generate code for an intrinsic call.
// All type decisions use getEffectiveType to resolve through macro/pattern bindings.
llvm::Value *generateIntrinsicCode(
	ParseContext &context, const std::string &name, const std::vector<Function *> &args, DataType resultType
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	if (name == "discard") {
		// Evaluate the argument for side effects and discard the result
		generateFunctionCode(context, args[0]);
		return nullptr;
	}

	if (name == "store") {
		// Generate the value in the current (original) macro scope first,
		// before resolving the destination which may cross scope boundaries.
		DataType valType = getEffectiveType(context, args[1]);
		llvm::Value *val = generateFunctionCode(context, args[1]);

		// Save scope state — resolveThroughMacroLayers freely crosses scope
		// boundaries, so we restore afterward.
		auto savedBindings = context.macroFunctionBindings;
		auto savedStack = context.macroBindingStack;

		// Resolve the destination through all macro and scope layers to detect
		// property stores. E.g., `add value to the x of target` chains through
		// scalar add macro → set macro → @intrinsic("store", var, val), and the
		// dest var resolves through multiple scopes to @intrinsic("property", ...).
		Function *destExpr = args[0];
		resolveThroughMacroLayers(context, destExpr);

		if (destExpr->kind == Function::Kind::IntrinsicCall && destExpr->intrinsicName == "property") {
			// Storing to a class field: generate GEP + store
			Function *instExpr = resolveVariableBinding(context, destExpr->arguments[1]);
			DataType instType = getEffectiveType(context, instExpr);
			ClassDefinition *classDef = instType.classDefinition;
			Function *propExpr = resolveVariableBinding(context, destExpr->arguments[2]);
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
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, instPtr, fieldIdx, "field_ptr");

			DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
			val = ensureType(context, val, valType, fieldType);
			builder.CreateStore(val, fieldPtr);
		} else {
			// Restore scope state — the else branch evaluates args[0] directly
			context.macroFunctionBindings = savedBindings;
			context.macroBindingStack = savedStack;

			llvm::Value *ptr = getVariablePointer(context, args[0]);
			if (ptr && val) {
				DataType destType = getEffectiveType(context, args[0]);
				if (destType.kind == DataType::Kind::Class && destType.classDefinition && destType.classInstIndex >= 0) {
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
		context.macroFunctionBindings = savedBindings;
		context.macroBindingStack = savedStack;
		return nullptr;
	}

	// Arithmetic intrinsics
	if (isArithmeticOperator(name)) {
		llvm::Value *left = generateFunctionCode(context, args[0]);
		llvm::Value *right = generateFunctionCode(context, args[1]);
		DataType leftType = getEffectiveType(context, args[0]);
		DataType rightType = getEffectiveType(context, args[1]);

		// Pointer arithmetic: ptr +/- integer → GEP
		if (isPointerArithmeticOperator(name) && (leftType.isPointer() || rightType.isPointer())) {
			llvm::Value *ptrVal = leftType.isPointer() ? left : right;
			llvm::Value *indexVal = leftType.isPointer() ? right : left;
			DataType indexType = leftType.isPointer() ? rightType : leftType;
			DataType ptrType = leftType.isPointer() ? leftType : rightType;
			llvm::Type *elemType = ptrType.dereferenced().toLLVM(*context.llvmContext);
			indexVal = coerceIndexToSizeT(context, indexVal, indexType);
			if (name == "subtract" && leftType.isPointer())
				indexVal = builder.CreateNeg(indexVal, "neg_idx");
			return builder.CreateGEP(elemType, ptrVal, indexVal, "ptr_arith");
		}

		if (leftType.kind == DataType::Kind::Matrix && rightType.kind == DataType::Kind::Vector && name == "multiply")
			return generateMatrixVectorMultiply(context, left, leftType, right, rightType);
		if (leftType.kind == DataType::Kind::Vector && rightType.kind == DataType::Kind::Matrix && name == "multiply") {
			// Treat row-vector * matrix as transpose-compatible multiply by transposing the operand order.
			return generateMatrixVectorMultiply(context, right, rightType, left, leftType);
		}
		if (leftType.kind == DataType::Kind::Matrix && rightType.kind == DataType::Kind::Matrix && name == "multiply")
			return generateMatrixMatrixMultiply(context, left, leftType, right, rightType);

		left = ensureType(context, left, leftType, resultType);
		right = ensureType(context, right, rightType, resultType);

		if (resultType.kind == DataType::Kind::Vector) {
			if (name == "add")
				return builder.CreateFAdd(left, right, "vadd");
			if (name == "subtract")
				return builder.CreateFSub(left, right, "vsub");
			if (name == "multiply")
				return builder.CreateFMul(left, right, "vmul");
			if (name == "divide")
				return builder.CreateFDiv(left, right, "vdiv");
		}
		if (resultType.kind == DataType::Kind::Float) {
			if (name == "add")
				return builder.CreateFAdd(left, right, "fadd");
			if (name == "subtract")
				return builder.CreateFSub(left, right, "fsub");
			if (name == "multiply")
				return builder.CreateFMul(left, right, "fmul");
			if (name == "divide")
				return builder.CreateFDiv(left, right, "fdiv");
			if (name == "modulo")
				return builder.CreateFRem(left, right, "fmod");
		} else {
			if (name == "add")
				return builder.CreateAdd(left, right, "add");
			if (name == "subtract")
				return builder.CreateSub(left, right, "sub");
			if (name == "multiply")
				return builder.CreateMul(left, right, "mul");
			if (name == "divide")
				return builder.CreateSDiv(left, right, "div");
			if (name == "modulo")
				return builder.CreateSRem(left, right, "mod");
		}
		return nullptr;
	}

	// Comparison intrinsics
	if (isComparisonOperator(name)) {
		llvm::Value *left = generateFunctionCode(context, args[0]);
		llvm::Value *right = generateFunctionCode(context, args[1]);
		DataType leftType = getEffectiveType(context, args[0]);
		DataType rightType = getEffectiveType(context, args[1]);
		if ((name == "equal" || name == "not equal") && leftType.isPointer() && rightType.isPointer() &&
			leftType == rightType) {
			llvm::Value *cmp =
				name == "equal" ? builder.CreateICmpEQ(left, right, "peq") : builder.CreateICmpNE(left, right, "pne");
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
			if (name == "less than")
				cmp = builder.CreateFCmpOLT(left, right, "flt");
			else if (name == "less than or equal")
				cmp = builder.CreateFCmpOLE(left, right, "fle");
			else if (name == "greater than")
				cmp = builder.CreateFCmpOGT(left, right, "fgt");
			else if (name == "greater than or equal")
				cmp = builder.CreateFCmpOGE(left, right, "fge");
			else if (name == "equal")
				cmp = builder.CreateFCmpOEQ(left, right, "feq");
			else
				cmp = builder.CreateFCmpONE(left, right, "fne");
		} else {
			if (name == "less than")
				cmp = builder.CreateICmpSLT(left, right, "lt");
			else if (name == "less than or equal")
				cmp = builder.CreateICmpSLE(left, right, "le");
			else if (name == "greater than")
				cmp = builder.CreateICmpSGT(left, right, "gt");
			else if (name == "greater than or equal")
				cmp = builder.CreateICmpSGE(left, right, "ge");
			else if (name == "equal")
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
	if (name == "and" || name == "or") {
		llvm::Value *left = generateFunctionCode(context, args[0]);
		llvm::Value *right = generateFunctionCode(context, args[1]);
		DataType leftType = getEffectiveType(context, args[0]);
		DataType rightType = getEffectiveType(context, args[1]);

		left = convertConditionToBool(context, left, leftType, "tobool");
		right = convertConditionToBool(context, right, rightType, "tobool");

		if (name == "and")
			return builder.CreateAnd(left, right, "and");
		else
			return builder.CreateOr(left, right, "or");
	}

	if (name == "not") {
		llvm::Value *val = generateFunctionCode(context, args[0]);
		DataType valType = getEffectiveType(context, args[0]);

		val = convertConditionToBool(context, val, valType, "tobool");

		return builder.CreateXor(val, builder.getTrue(), "not");
	}

	// Negate
	if (name == "negate") {
		llvm::Value *val = generateFunctionCode(context, args[0]);
		DataType valType = getEffectiveType(context, args[0]);
		if (valType.kind == DataType::Kind::Float)
			return builder.CreateFNeg(val, "fneg");
		return builder.CreateNeg(val, "neg");
	}

	// Math functions (sin, cos, sqrt, abs, floor, ceil, round, exp, log, pow, atan2, min, max)
	if (isMathFunction(name)) {
		context.requiredLibraries.insert("m");
		// Map intrinsic names to LLVM intrinsic IDs
		static const std::unordered_map<std::string, llvm::Intrinsic::ID> floatIntrinsics = {
			{"sin", llvm::Intrinsic::sin},	   {"cos", llvm::Intrinsic::cos},	  {"sqrt", llvm::Intrinsic::sqrt},
			{"abs", llvm::Intrinsic::fabs},	   {"floor", llvm::Intrinsic::floor}, {"ceil", llvm::Intrinsic::ceil},
			{"round", llvm::Intrinsic::round}, {"exp", llvm::Intrinsic::exp},	  {"log", llvm::Intrinsic::log},
			{"pow", llvm::Intrinsic::pow},	   {"min", llvm::Intrinsic::minnum},  {"max", llvm::Intrinsic::maxnum},
		};

		auto it = floatIntrinsics.find(name);
		if (it != floatIntrinsics.end()) {
			// GLSL.std.450 extended instructions (used by SPIR-V) only support 16/32-bit floats.
			// Use f32 for SPIR-V targets, f64 for native targets.
			int mathFloatBytes = context.options.emitSPIRV ? 4 : 8;
			DataType mathFloat = {DataType::Kind::Float, mathFloatBytes};
			if (args.size() == 1) {
				llvm::Value *val = generateFunctionCode(context, args[0]);
				DataType valType = getEffectiveType(context, args[0]);
				if (valType.kind != DataType::Kind::Float || valType.numericSize != mathFloatBytes)
					val = ensureType(context, val, valType, mathFloat);
				llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, it->second, {val->getType()});
				llvm::Value *result = builder.CreateCall(fn, {val}, name);
				// Convert back to f64 for SPIR-V so the rest of the computation stays consistent
				if (context.options.emitSPIRV)
					result = builder.CreateFPExt(result, llvm::Type::getDoubleTy(*context.llvmContext));
				return result;
			}
			llvm::Value *left = generateFunctionCode(context, args[0]);
			llvm::Value *right = generateFunctionCode(context, args[1]);
			DataType leftType = getEffectiveType(context, args[0]);
			DataType rightType = getEffectiveType(context, args[1]);
			left = ensureType(context, left, leftType, mathFloat);
			right = ensureType(context, right, rightType, mathFloat);
			llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, it->second, {left->getType()});
			llvm::Value *result = builder.CreateCall(fn, {left, right}, name);
			if (context.options.emitSPIRV)
				result = builder.CreateFPExt(result, llvm::Type::getDoubleTy(*context.llvmContext));
			return result;
		}

		// atan2: no LLVM intrinsic, call libm
		if (name == "atan2") {
			llvm::Value *y = generateFunctionCode(context, args[0]);
			llvm::Value *x = generateFunctionCode(context, args[1]);
			DataType yType = getEffectiveType(context, args[0]);
			DataType xType = getEffectiveType(context, args[1]);
			DataType promoted;
			DataType::promoteArithmetic(yType, xType, promoted);
			if (promoted.kind != DataType::Kind::Float)
				promoted = {DataType::Kind::Float, 8};
			y = ensureType(context, y, yType, promoted);
			x = ensureType(context, x, xType, promoted);
			llvm::Type *floatType = promoted.toLLVM(*context.llvmContext);
			llvm::FunctionType *ft = llvm::FunctionType::get(floatType, {floatType, floatType}, false);
			const char *fnName = promoted.numericSize == 4 ? "atan2f" : "atan2";
			llvm::FunctionCallee callee = context.llvmModule->getOrInsertFunction(fnName, ft);
			return builder.CreateCall(callee, {y, x}, "atan2");
		}

		return nullptr;
	}

	// Pointer intrinsics
	if (name == "address of") {
		llvm::Value *ptr = getVariablePointer(context, args[0]);
		assert(ptr && "address of requires a variable");
		return ptr;
	}

	if (name == "dereference") {
		llvm::Value *ptrVal = generateFunctionCode(context, args[0]);
		DataType ptrType = getEffectiveType(context, args[0]);
		DataType elemType = ptrType.dereferenced();
		llvm::Type *elemLLVMType = getLLVMType(context, elemType);
		return builder.CreateAlignedLoad(elemLLVMType, ptrVal, llvm::Align(8), "deref");
	}

	// Array/pointer intrinsics
	if (name == "store at") {
		llvm::Value *ptr = generateFunctionCode(context, args[0]);
		llvm::Value *index = generateFunctionCode(context, args[1]);
		llvm::Value *value = generateFunctionCode(context, args[2]);
		DataType ptrType = getEffectiveType(context, args[0]);
		assert(ptrType.isPointer() && "store at requires a pointer argument");
		index = coerceIndexToSizeT(context, index, getEffectiveType(context, args[1]));
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
		value = ensureType(context, value, getEffectiveType(context, args[2]), elementType);
		builder.CreateAlignedStore(value, elementPtr, llvm::Align(8));
		return nullptr;
	}

	if (name == "load at") {
		llvm::Value *ptr = generateFunctionCode(context, args[0]);
		llvm::Value *index = generateFunctionCode(context, args[1]);
		DataType ptrType = getEffectiveType(context, args[0]);
		assert(ptrType.isPointer() && "load at requires a pointer argument");
		index = coerceIndexToSizeT(context, index, getEffectiveType(context, args[1]));
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

	if (name == "loop while") {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "loop while requires a body section");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_cond", func);
		llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_body", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_exit", func);

		builder.CreateBr(condBlock);
		builder.SetInsertPoint(condBlock);

		llvm::Value *condValue = generateFunctionCode(context, args[0]);
		DataType condType = getEffectiveType(context, args[0]);
		llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "while_cond_bool");
		builder.CreateCondBr(condBool, bodyBlock, exitBlock);

		builder.SetInsertPoint(bodyBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = condBlock;

		return nullptr;
	}

	if (name == "if") {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "if requires a body section");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_then", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_exit", func);

		llvm::Value *condValue = generateFunctionCode(context, args[0]);
		DataType condType = getEffectiveType(context, args[0]);
		llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "if_cond");
		builder.CreateCondBr(condBool, thenBlock, exitBlock);

		builder.SetInsertPoint(thenBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (name == "else" || name == "else if") {
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

		if (name == "else if") {
			llvm::BasicBlock *elifThenBlock = llvm::BasicBlock::Create(*context.llvmContext, "elif_then", func);

			llvm::Value *condValue = generateFunctionCode(context, args[0]);
			DataType condType = getEffectiveType(context, args[0]);
			llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "elif_cond");
			builder.CreateCondBr(condBool, elifThenBlock, newExitBlock);

			builder.SetInsertPoint(elifThenBlock);
		}

		bodySection->exitBlock = newExitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (name == "switch") {
		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::Value *switchValue = generateFunctionCode(context, args[0]);

		// Ensure the value is an integer (LLVM switch requires integer operand)
		assert(
			(getEffectiveType(context, args[0]).kind == DataType::Kind::Int ||
			 getEffectiveType(context, args[0]).kind == DataType::Kind::Bool) &&
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

	if (name == "case") {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "case requires a body section");

		assert(context.currentSwitchInst && "case outside of switch");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		// Evaluate the case value — must be a constant integer
		llvm::Value *caseValue = generateFunctionCode(context, args[0]);
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

	if (name == "return") {
		if (args.size() >= 1) {
			llvm::Value *returnValue = generateFunctionCode(context, args[0]);
			builder.CreateRet(returnValue);
		}
		return nullptr;
	}

	if (name == "call") {
		// Format: args[0]="library", args[1]="function", args[2]="return type", args[3+]=actual args
		std::string library = getStringLiteral(args[0]);
		std::string funcName = getStringLiteral(args[1]);
		if (!library.empty() && library != "libc")
			context.requiredLibraries.insert(library);

		DataType retTypeRef = getEffectiveType(context, args[2]);
		DataType returnType = retTypeRef.toReferencedType();
		llvm::Type *returnLLVMType = returnType.toLLVM(*context.llvmContext);

		// Build call arguments — string literals become global constant pointers
		std::vector<llvm::Value *> callArgs;
		for (size_t i = 3; i < args.size(); ++i) {
			if (args[i]->kind == Function::Kind::Literal) {
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
			llvm::Value *argVal = generateFunctionCode(context, args[i]);
			if (argVal)
				callArgs.push_back(argVal);
		}

		llvm::FunctionCallee callee;
		if (library == "libc" && funcName == "malloc" && callArgs.size() == 1) {
			callArgs[0] = coerceIndexToSizeT(context, callArgs[0], getEffectiveType(context, args[3]));
			llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getPtrTy(), {builder.getInt64Ty()}, false);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else if (library == "libc" && funcName == "memcpy" && callArgs.size() == 3) {
			callArgs[2] = coerceIndexToSizeT(context, callArgs[2], getEffectiveType(context, args[5]));
			llvm::FunctionType *funcType = llvm::FunctionType::get(
				builder.getPtrTy(), {builder.getPtrTy(), builder.getPtrTy(), builder.getInt64Ty()}, false
			);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else if (library == "libc" && funcName == "snprintf" && callArgs.size() >= 3) {
			callArgs[1] = coerceIndexToSizeT(context, callArgs[1], getEffectiveType(context, args[4]));
			llvm::FunctionType *funcType = llvm::FunctionType::get(
				builder.getInt32Ty(), {builder.getPtrTy(), builder.getInt64Ty(), builder.getPtrTy()}, true
			);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		} else {
			llvm::FunctionType *funcType = llvm::FunctionType::get(returnLLVMType, {}, true);
			callee = context.llvmModule->getOrInsertFunction(funcName, funcType);
		}

		llvm::Value *callResult = builder.CreateCall(callee, callArgs);
		// If return type is void, return nullptr (no value to use)
		if (returnType.kind == DataType::Kind::Void)
			return nullptr;
		return callResult;
	}

	if (name == "cast") {
		// Format: args[0]=value, args[1]=type (TypeReference)
		// Class cast: reinterpret as pointer to the class struct
		if (resultType.kind == DataType::Kind::Class) {
			llvm::Value *val = generateFunctionCode(context, args[0]);
			DataType fromType = getEffectiveType(context, args[0]);
			if (val && !fromType.isPointer() && fromType.kind == DataType::Kind::Int)
				val = builder.CreateIntToPtr(val, llvm::PointerType::getUnqual(*context.llvmContext), "itop_class");
			return val;
		}

		llvm::Value *val = generateFunctionCode(context, args[0]);
		DataType fromType = getEffectiveType(context, args[0]);

		// Get target type from the TypeReference argument
		DataType typeArgType = getEffectiveType(context, args[1]);
		if (typeArgType.kind != DataType::Kind::Type)
			return nullptr; // unresolved type (e.g. spurious top-level instantiation)
		DataType toType = typeArgType.toReferencedType();

		return ensureType(context, val, fromType, toType);
	}

	if (name == "type") {
		// Types are compile-time only — no runtime code
		return nullptr;
	}

	if (name == "type of" || name == "array" || name == "build info") {
		// Compile-time only — no runtime code
		return nullptr;
	}

	if (name == "select") {
		CompileTimeValue conditionValue = evaluateCompileTimeValue(args[0], context, context.macroFunctionBindings);
		std::optional<bool> condition = compileTimeTruthiness(conditionValue);
		if (!condition.has_value()) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "select condition must be compile-time known", args[0]->range)
			);
			return nullptr;
		}
		return generateFunctionCode(context, args[*condition ? 1 : 2]);
	}

	if (name == "add pointer depth") {
		// Compile-time only — modifies TypeReference, no runtime code
		return nullptr;
	}

	if (name == "construct") {
		if (resultType.kind == DataType::Kind::Array) {
			llvm::Type *arrayType = getLLVMType(context, resultType);
			llvm::AllocaInst *alloca = createEntryAlloca(context, "array_tmp", resultType);
			DataType elementType = *resultType.arrayElementType;
			for (size_t i = 1; i < args.size(); i++) {
				llvm::Value *elementVal = generateFunctionCode(context, args[i]);
				DataType fromType = getEffectiveType(context, args[i]);
				elementVal = ensureType(context, elementVal, fromType, elementType);
				llvm::Value *elementPtr =
					builder.CreateGEP(arrayType, alloca, {builder.getInt64(0), builder.getInt64(i - 1)}, "array_elem_ptr");
				builder.CreateStore(elementVal, elementPtr);
			}
			return builder.CreateAlignedLoad(arrayType, alloca, llvm::Align(8), "array_load");
		}

		if (resultType.kind == DataType::Kind::Vector) {
			assert(static_cast<int>(args.size()) - 1 == resultType.vectorSize() && "vector construct arity mismatch");
			return buildVectorValue(context, resultType, args, 1);
		}

		if (resultType.kind == DataType::Kind::Matrix) {
			if (args.size() == 2)
				return buildMatrixFromFlatArray(context, resultType, args[1]);
			assert(
				static_cast<int>(args.size()) - 1 == resultType.matrixRows() * resultType.matrixColumns() &&
				"matrix construct arity mismatch"
			);
			return buildMatrixFromScalarArgs(context, resultType, args, 1);
		}

		if (resultType.kind != DataType::Kind::Class) {
			assert(args.size() == 2 && "construct for non-aggregate types expects exactly one value");
			llvm::Value *val = generateFunctionCode(context, args[1]);
			DataType fromType = getEffectiveType(context, args[1]);
			return ensureType(context, val, fromType, resultType);
		}

		ClassDefinition *classDef = resultType.classDefinition;
		std::vector<DataType> fieldTypes;
		fieldTypes.reserve(args.size() - 1);
		for (size_t i = 1; i < args.size(); i++)
			fieldTypes.push_back(getEffectiveType(context, args[i]));
		int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
		DataType concreteType = resultType;
		concreteType.classInstIndex = instIndex;
		ClassInstantiation &inst = classDef->instantiations[instIndex];
		llvm::Type *structType = getLLVMType(context, concreteType);

		llvm::AllocaInst *alloca = createEntryAlloca(context, "class_tmp", concreteType);

		for (size_t i = 0; i < inst.fieldTypes.size(); i++) {
			llvm::Value *fieldVal = generateFunctionCode(context, args[i + 1]);
			DataType fieldFromType = getEffectiveType(context, args[i + 1]);
			fieldVal = ensureType(context, fieldVal, fieldFromType, inst.fieldTypes[i]);
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, alloca, i, "field_ptr");
			builder.CreateStore(fieldVal, fieldPtr);
		}

		return builder.CreateAlignedLoad(structType, alloca, llvm::Align(8), "struct_load");
	}

	if (name == "property") {
		// Format: args[0]=instance, args[1]=fieldname (string literal from {word:} capture)
		Function *ownerExpr = args[0];
		DataType instType = getEffectiveType(context, ownerExpr);
		if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex < 0 &&
			!instType.classDefinition->instantiations.empty()) {
			instType.classInstIndex = 0;
		}
		ClassDefinition *classDef = instType.classDefinition;

		// Get field name from string literal
		Function *propExpr = resolveVariableBinding(context, args[1]);
		std::string fieldName = getStringLiteral(propExpr);

		// C strings expose a synthetic "data" property so string-library macros can
		// operate on both heap strings and string literals without duplicating logic.
		if (fieldName == "data" && instType.isBytePointer())
			return generateFunctionCode(context, ownerExpr);

		if (!classDef) {
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error, instType.toString() + " is not a class and has no properties", args[0]->range
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
				Diagnostic::Level::Error, instType.toString() + " doesn't have property \"" + fieldName + "\"", args[0]->range
			));
			return nullptr;
		}

		DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
		if (llvm::Value *instPtr = getVariablePointer(context, ownerExpr)) {
			llvm::Type *structType = getLLVMType(context, instType);
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, instPtr, fieldIdx, "field_ptr");
			return builder.CreateAlignedLoad(getLLVMType(context, fieldType), fieldPtr, llvm::Align(8), fieldName + "_val");
		}

		llvm::Value *instValue = generateFunctionCode(context, ownerExpr);
		if (!instValue)
			return nullptr;
		return builder.CreateExtractValue(instValue, {static_cast<unsigned>(fieldIdx)}, fieldName + "_val");
	}

	// Shader I/O intrinsics (only available in --emit-spirv mode)
	if (name == "shader input") {
		// @intrinsic("shader input", globalName) → load vec4 from named shader input global
		std::string inputName = getStringLiteral(args[0]);
		std::string globalName;
		if (inputName == "FragCoord")
			globalName = "gl_FragCoord";
		else if (inputName == "Position")
			globalName = "in_Position";
		else {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unknown shader input: " + inputName, Range()));
			return nullptr;
		}
		llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
		if (!global) {
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error, "Shader input '" + inputName + "' is unavailable in this compilation mode",
				args[0]->range
			));
			return nullptr;
		}
		llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
		return builder.CreateLoad(vec4Ty, global, inputName);
	}

	if (name == "shader uniform") {
		// @intrinsic("shader uniform", uniformName) → load f32 from named uniform global
		// The SPIR-V patcher wraps this in a UBO struct with proper decorations
		std::string uniformName = getStringLiteral(args[0]);
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
			context.shaderUniformNames.push_back(uniformName);
		}
		return builder.CreateLoad(builder.getFloatTy(), global, uniformName + "_val");
	}

	if (name == "shader output") {
		// @intrinsic("shader output", r, g, b, a) → store vec4 to shader output global
		llvm::Value *r = generateFunctionCode(context, args[0]);
		llvm::Value *g = generateFunctionCode(context, args[1]);
		llvm::Value *b = generateFunctionCode(context, args[2]);
		llvm::Value *a = generateFunctionCode(context, args[3]);
		if (!r || !g || !b || !a)
			return nullptr;

		DataType rType = getEffectiveType(context, args[0]);
		DataType gType = getEffectiveType(context, args[1]);
		DataType bType = getEffectiveType(context, args[2]);
		DataType aType = getEffectiveType(context, args[3]);
		DataType f32 = {DataType::Kind::Float, 4};
		r = ensureType(context, r, rType, f32);
		g = ensureType(context, g, gType, f32);
		b = ensureType(context, b, bType, f32);
		a = ensureType(context, a, aType, f32);

		llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
		llvm::Value *color = llvm::UndefValue::get(vec4Ty);
		color = builder.CreateInsertElement(color, r, (uint64_t)0, "color_r");
		color = builder.CreateInsertElement(color, g, (uint64_t)1, "color_g");
		color = builder.CreateInsertElement(color, b, (uint64_t)2, "color_b");
		color = builder.CreateInsertElement(color, a, (uint64_t)3, "color_a");

		// Find the output global: gl_FragColor (fragment) or gl_Position (vertex)
		std::string outName =
			(context.options.shaderStage == ParseContext::ShaderStage::Vertex) ? "gl_Position" : "gl_FragColor";
		llvm::GlobalVariable *outGlobal = context.llvmModule->getGlobalVariable(outName);
		if (!outGlobal) {
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error, "Shader output is unavailable in this compilation mode",
				Range(args[0]->range.line, args[0]->range.start(), args[3]->range.end())
			));
			return nullptr;
		}
		builder.CreateStore(color, outGlobal);
		return nullptr;
	}

	if (name == "extract element") {
		// @intrinsic("extract element", vector, index) → extract scalar from vector
		llvm::Value *vec = generateFunctionCode(context, args[0]);
		if (!vec)
			return nullptr;
		if (auto *idxLit = std::get_if<double>(&args[1]->literalValue)) {
			return builder.CreateExtractElement(vec, (uint64_t)*idxLit, "elem");
		}
		llvm::Value *idx = generateFunctionCode(context, args[1]);
		if (!idx)
			return nullptr;
		return builder.CreateExtractElement(vec, idx, "elem");
	}

	ASSERT_UNREACHABLE("Unknown intrinsic: validated at parse time");
}
