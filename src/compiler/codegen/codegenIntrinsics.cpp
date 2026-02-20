#include "classDefinition.h"
#include "codegenInternal.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "intrinsicInfo.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <unordered_map>

// Helper to extract string literal from expression
std::string getStringLiteral(Expression *expr) {
	if (expr && expr->kind == Expression::Kind::Literal) {
		if (auto *str = std::get_if<std::string>(&expr->literalValue))
			return *str;
	}
	return "";
}

// Helper: generate expression code, returning false on failure.
#define TRY_EXPR(var, expr)                                                                                                    \
	llvm::Value *var;                                                                                                          \
	if (!generateExpressionCode(context, expr, var))                                                                           \
	return false

// Generate code for an intrinsic call.
// All type decisions use getEffectiveType to resolve through macro/pattern bindings.
bool generateIntrinsicCode(
	ParseContext &context, const std::string &name, const std::vector<Expression *> &args, DataType resultType,
	llvm::Value *&result
) {
	result = nullptr;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	if (name == "store") {
		// Generate the value in the current (original) macro scope first,
		// before resolving the destination which may cross scope boundaries.
		DataType valType = getEffectiveType(context, args[1]);
		TRY_EXPR(val, args[1]);

		// Save scope state — resolveThroughMacroLayers freely crosses scope
		// boundaries, so we restore afterward.
		auto savedBindings = context.macroExpressionBindings;
		auto savedStack = context.macroBindingStack;

		// Resolve the destination through all macro and scope layers to detect
		// property stores. E.g., `add value to the x of target` chains through
		// scalar add macro → set macro → @intrinsic("store", var, val), and the
		// dest var resolves through multiple scopes to @intrinsic("property", ...).
		Expression *destExpr = args[0];
		resolveThroughMacroLayers(context, destExpr);

		if (destExpr->kind == Expression::Kind::IntrinsicCall && destExpr->intrinsicName == "property") {
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
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, instPtr, fieldIdx, "field_ptr");

			DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
			val = ensureType(context, val, valType, fieldType);
			builder.CreateStore(val, fieldPtr);
		} else {
			// Restore scope state — the else branch evaluates args[0] directly
			context.macroExpressionBindings = savedBindings;
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
						llvm::Value *srcVal = builder.CreateAlignedLoad(structType, val, llvm::Align(8), "struct_load");
						builder.CreateAlignedStore(srcVal, ptr, llvm::Align(8));
					} else {
						// Different field types — element-wise copy with conversion
						llvm::Type *srcStructType = getLLVMType(context, valType);
						llvm::Type *destStructType = getLLVMType(context, destType);
						for (size_t i = 0; i < destFields.size(); i++) {
							llvm::Value *srcFieldPtr = builder.CreateStructGEP(srcStructType, val, i, "src_field");
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
		context.macroExpressionBindings = savedBindings;
		context.macroBindingStack = savedStack;
		return true;
	}

	// Arithmetic intrinsics
	if (isArithmeticOperator(name)) {
		TRY_EXPR(left, args[0]);
		TRY_EXPR(right, args[1]);
		DataType leftType = getEffectiveType(context, args[0]);
		DataType rightType = getEffectiveType(context, args[1]);

		// Pointer arithmetic: ptr +/- integer → GEP
		if (isPointerArithmeticOperator(name) && (leftType.isPointer() || rightType.isPointer())) {
			llvm::Value *ptrVal = leftType.isPointer() ? left : right;
			llvm::Value *indexVal = leftType.isPointer() ? right : left;
			DataType ptrType = leftType.isPointer() ? leftType : rightType;
			llvm::Type *elemType = ptrType.dereferenced().toLLVM(*context.llvmContext);
			if (name == "subtract" && leftType.isPointer())
				indexVal = builder.CreateNeg(indexVal, "neg_idx");
			result = builder.CreateGEP(elemType, ptrVal, indexVal, "ptr_arith");
			return true;
		}

		DataType promoted = DataType::promote(leftType, rightType);

		left = ensureType(context, left, leftType, promoted);
		right = ensureType(context, right, rightType, promoted);

		if (promoted.kind == DataType::Kind::Float) {
			if (name == "add")
				result = builder.CreateFAdd(left, right, "fadd");
			else if (name == "subtract")
				result = builder.CreateFSub(left, right, "fsub");
			else if (name == "multiply")
				result = builder.CreateFMul(left, right, "fmul");
			else if (name == "divide")
				result = builder.CreateFDiv(left, right, "fdiv");
			else if (name == "modulo")
				result = builder.CreateFRem(left, right, "fmod");
		} else {
			if (name == "add")
				result = builder.CreateAdd(left, right, "add");
			else if (name == "subtract")
				result = builder.CreateSub(left, right, "sub");
			else if (name == "multiply")
				result = builder.CreateMul(left, right, "mul");
			else if (name == "divide")
				result = builder.CreateSDiv(left, right, "div");
			else if (name == "modulo")
				result = builder.CreateSRem(left, right, "mod");
		}
		return true;
	}

	// Comparison intrinsics
	if (isComparisonOperator(name)) {
		TRY_EXPR(left, args[0]);
		TRY_EXPR(right, args[1]);
		DataType leftType = getEffectiveType(context, args[0]);
		DataType rightType = getEffectiveType(context, args[1]);
		DataType promoted = DataType::promote(leftType, rightType);

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
		if (resultType.kind == DataType::Kind::Bool) {
			result = cmp;
			return true;
		}
		result = builder.CreateZExt(cmp, getLLVMType(context, resultType), "cmp_ext");
		return true;
	}

	// Logical operators
	if (name == "and" || name == "or") {
		TRY_EXPR(left, args[0]);
		TRY_EXPR(right, args[1]);
		DataType leftType = getEffectiveType(context, args[0]);
		DataType rightType = getEffectiveType(context, args[1]);

		left = convertConditionToBool(context, left, leftType, "tobool");
		right = convertConditionToBool(context, right, rightType, "tobool");

		if (name == "and")
			result = builder.CreateAnd(left, right, "and");
		else
			result = builder.CreateOr(left, right, "or");
		return true;
	}

	if (name == "not") {
		TRY_EXPR(val, args[0]);
		DataType valType = getEffectiveType(context, args[0]);

		val = convertConditionToBool(context, val, valType, "tobool");

		result = builder.CreateXor(val, builder.getTrue(), "not");
		return true;
	}

	// Negate
	if (name == "negate") {
		TRY_EXPR(val, args[0]);
		DataType valType = getEffectiveType(context, args[0]);
		if (valType.kind == DataType::Kind::Float)
			result = builder.CreateFNeg(val, "fneg");
		else
			result = builder.CreateNeg(val, "neg");
		return true;
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
				TRY_EXPR(val, args[0]);
				DataType valType = getEffectiveType(context, args[0]);
				if (valType.kind != DataType::Kind::Float || valType.byteSize != mathFloatBytes)
					val = ensureType(context, val, valType, mathFloat);
				llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, it->second, {val->getType()});
				result = builder.CreateCall(fn, {val}, name);
				// Convert back to f64 for SPIR-V so the rest of the computation stays consistent
				if (context.options.emitSPIRV)
					result = builder.CreateFPExt(result, llvm::Type::getDoubleTy(*context.llvmContext));
				return true;
			}
			TRY_EXPR(left, args[0]);
			TRY_EXPR(right, args[1]);
			DataType leftType = getEffectiveType(context, args[0]);
			DataType rightType = getEffectiveType(context, args[1]);
			left = ensureType(context, left, leftType, mathFloat);
			right = ensureType(context, right, rightType, mathFloat);
			llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(context.llvmModule, it->second, {left->getType()});
			result = builder.CreateCall(fn, {left, right}, name);
			if (context.options.emitSPIRV)
				result = builder.CreateFPExt(result, llvm::Type::getDoubleTy(*context.llvmContext));
			return true;
		}

		// atan2: no LLVM intrinsic, call libm
		if (name == "atan2") {
			TRY_EXPR(y, args[0]);
			TRY_EXPR(x, args[1]);
			DataType yType = getEffectiveType(context, args[0]);
			DataType xType = getEffectiveType(context, args[1]);
			DataType promoted = DataType::promote(yType, xType);
			if (promoted.kind != DataType::Kind::Float)
				promoted = {DataType::Kind::Float, 8};
			y = ensureType(context, y, yType, promoted);
			x = ensureType(context, x, xType, promoted);
			llvm::Type *floatType = promoted.toLLVM(*context.llvmContext);
			llvm::FunctionType *ft = llvm::FunctionType::get(floatType, {floatType, floatType}, false);
			const char *fnName = promoted.byteSize == 4 ? "atan2f" : "atan2";
			llvm::FunctionCallee callee = context.llvmModule->getOrInsertFunction(fnName, ft);
			result = builder.CreateCall(callee, {y, x}, "atan2");
			return true;
		}

		return true;
	}

	// Pointer intrinsics
	if (name == "address of") {
		llvm::Value *ptr = getVariablePointer(context, args[0]);
		assert(ptr && "address of requires a variable");
		result = ptr;
		return true;
	}

	if (name == "dereference") {
		TRY_EXPR(ptrVal, args[0]);
		DataType ptrType = getEffectiveType(context, args[0]);
		DataType elemType = ptrType.dereferenced();
		llvm::Type *elemLLVMType = getLLVMType(context, elemType);
		result = builder.CreateAlignedLoad(elemLLVMType, ptrVal, llvm::Align(8), "deref");
		return true;
	}

	// Array/pointer intrinsics
	if (name == "store at") {
		TRY_EXPR(ptr, args[0]);
		TRY_EXPR(index, args[1]);
		TRY_EXPR(value, args[2]);
		assert(getEffectiveType(context, args[0]).isPointer() && "store at requires a pointer argument");
		llvm::Value *elementPtr = builder.CreateGEP(builder.getInt64Ty(), ptr, index);
		builder.CreateAlignedStore(value, elementPtr, llvm::Align(8));
		return true;
	}

	if (name == "load at") {
		TRY_EXPR(ptr, args[0]);
		TRY_EXPR(index, args[1]);
		assert(getEffectiveType(context, args[0]).isPointer() && "load at requires a pointer argument");

		llvm::Value *elementPtr = builder.CreateGEP(builder.getInt64Ty(), ptr, index);
		result = builder.CreateAlignedLoad(builder.getInt64Ty(), elementPtr, llvm::Align(8));
		return true;
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

		TRY_EXPR(condValue, args[0]);
		DataType condType = getEffectiveType(context, args[0]);
		llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "while_cond_bool");
		builder.CreateCondBr(condBool, bodyBlock, exitBlock);

		builder.SetInsertPoint(bodyBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = condBlock;

		return true;
	}

	if (name == "if") {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "if requires a body section");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_then", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_exit", func);

		TRY_EXPR(condValue, args[0]);
		DataType condType = getEffectiveType(context, args[0]);
		llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "if_cond");
		builder.CreateCondBr(condBool, thenBlock, exitBlock);

		builder.SetInsertPoint(thenBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = nullptr;

		return true;
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

			TRY_EXPR(condValue, args[0]);
			DataType condType = getEffectiveType(context, args[0]);
			llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "elif_cond");
			builder.CreateCondBr(condBool, elifThenBlock, newExitBlock);

			builder.SetInsertPoint(elifThenBlock);
		}

		bodySection->exitBlock = newExitBlock;
		bodySection->branchBackBlock = nullptr;

		return true;
	}

	if (name == "switch") {
		llvm::Function *func = builder.GetInsertBlock()->getParent();

		TRY_EXPR(switchValue, args[0]);

		// Ensure the value is an integer (LLVM switch requires integer operand)
		assert(
			(getEffectiveType(context, args[0]).kind == DataType::Kind::Integer ||
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

		return true;
	}

	if (name == "case") {
		Section *bodySection = context.currentBodySection;
		assert(bodySection && "case requires a body section");

		assert(context.currentSwitchInst && "case outside of switch");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		// Evaluate the case value — must be a constant integer
		TRY_EXPR(caseValue, args[0]);
		llvm::ConstantInt *caseConst = llvm::dyn_cast<llvm::ConstantInt>(caseValue);
		assert(caseConst && "case value must be a constant integer");

		llvm::BasicBlock *caseBlock = llvm::BasicBlock::Create(*context.llvmContext, "case", func);
		context.currentSwitchInst->addCase(caseConst, caseBlock);

		builder.SetInsertPoint(caseBlock);
		bodySection->exitBlock = context.currentSwitchExitBlock;
		bodySection->branchBackBlock = nullptr;

		return true;
	}

	if (name == "return") {
		if (args.size() >= 1) {
			TRY_EXPR(returnValue, args[0]);
			builder.CreateRet(returnValue);
		}
		return true;
	}

	if (name == "call") {
		// Format: args[0]="library", args[1]="function", args[2]="return type", args[3+]=actual args
		std::string library = getStringLiteral(args[0]);
		std::string funcName = getStringLiteral(args[1]);
		std::string retTypeStr = getStringLiteral(args[2]);

		if (!library.empty() && library != "libc")
			context.requiredLibraries.insert(library);

		DataType returnType = DataType::fromString(retTypeStr);
		llvm::Type *returnLLVMType = returnType.toLLVM(*context.llvmContext);

		// Build call arguments — string literals become global constant pointers
		std::vector<llvm::Value *> callArgs;
		for (size_t i = 3; i < args.size(); ++i) {
			if (args[i]->kind == Expression::Kind::Literal) {
				if (auto *str = std::get_if<std::string>(&args[i]->literalValue)) {
					std::string globalName = ".str." + std::to_string(context.stringConstants.size());
					llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, *str, true);
					llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
						*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
					);
					context.stringConstants[*str] = strGlobal;
					callArgs.push_back(strGlobal);
					continue;
				}
			}
			llvm::Value *argVal;
			if (!generateExpressionCode(context, args[i], argVal))
				return false;
			if (argVal)
				callArgs.push_back(argVal);
		}

		// Get or create function declaration with proper return type
		llvm::Function *func = context.llvmModule->getFunction(funcName);
		if (!func) {
			llvm::FunctionType *funcType = llvm::FunctionType::get(returnLLVMType, {}, true);
			func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, context.llvmModule);
		}

		llvm::Value *callResult = builder.CreateCall(func, callArgs);
		// If return type is void, result stays nullptr
		if (returnType.kind != DataType::Kind::Void)
			result = callResult;
		return true;
	}

	if (name == "cast") {
		// Format: args[0]=value, args[1]=type (TypeReference)
		// Class cast: reinterpret as pointer to the class struct
		if (resultType.kind == DataType::Kind::Class) {
			TRY_EXPR(val, args[0]);
			DataType fromType = getEffectiveType(context, args[0]);
			if (val && !fromType.isPointer() && fromType.kind == DataType::Kind::Integer)
				val = builder.CreateIntToPtr(val, llvm::PointerType::getUnqual(*context.llvmContext), "itop_class");
			result = val;
			return true;
		}

		TRY_EXPR(val, args[0]);
		DataType fromType = getEffectiveType(context, args[0]);

		// Get target type from the TypeReference argument
		DataType typeArgType = getEffectiveType(context, args[1]);
		if (typeArgType.kind != DataType::Kind::TypeReference)
			return true; // unresolved type (e.g. spurious top-level instantiation)
		DataType toType = typeArgType.toReferencedType();

		result = ensureType(context, val, fromType, toType);
		return true;
	}

	if (name == "type") {
		// Types are compile-time only — no runtime code
		return true;
	}

	if (name == "add pointer depth") {
		// Compile-time only — modifies TypeReference, no runtime code
		return true;
	}

	if (name == "construct") {
		// Format: args[0]=type_pattern, args[1+]=field values
		ClassDefinition *classDef = resultType.classDefinition;
		ClassInstantiation &inst = classDef->instantiations[resultType.classInstIndex];
		llvm::Type *structType = getLLVMType(context, resultType);

		// Allocate struct on stack
		llvm::AllocaInst *alloca = createEntryAlloca(context, "class_tmp", resultType);

		// Store each field value
		for (size_t i = 0; i < inst.fieldTypes.size(); i++) {
			llvm::Value *fieldVal;
			if (!generateExpressionCode(context, args[i + 1], fieldVal))
				return false;
			DataType fieldFromType = getEffectiveType(context, args[i + 1]);
			fieldVal = ensureType(context, fieldVal, fieldFromType, inst.fieldTypes[i]);
			llvm::Value *fieldPtr = builder.CreateStructGEP(structType, alloca, i, "field_ptr");
			builder.CreateStore(fieldVal, fieldPtr);
		}

		result = alloca;
		return true;
	}

	if (name == "property") {
		// Format: args[0]=instance, args[1]=fieldname (string literal from {word:} capture)
		Expression *instExpr = resolveVariableBinding(context, args[0]);
		DataType instType = getEffectiveType(context, instExpr);
		ClassDefinition *classDef = instType.classDefinition;

		// Get field name from string literal
		Expression *propExpr = resolveVariableBinding(context, args[1]);
		std::string fieldName = getStringLiteral(propExpr);

		if (!classDef) {
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error, instType.toString() + " is not a class and has no properties", args[0]->range
			));
			return false;
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
			return false;
		}

		// Get instance pointer
		llvm::Value *instPtr = getVariablePointer(context, instExpr);
		llvm::Type *structType = getLLVMType(context, instType);
		llvm::Value *fieldPtr = builder.CreateStructGEP(structType, instPtr, fieldIdx, "field_ptr");
		DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
		result = builder.CreateAlignedLoad(getLLVMType(context, fieldType), fieldPtr, llvm::Align(8), fieldName + "_val");
		return true;
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
			return false;
		}
		llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
		if (!global) {
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error, "Shader input '" + inputName + "' requires --emit-spirv and --shader-stage", Range()
			));
			return false;
		}
		llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
		result = builder.CreateLoad(vec4Ty, global, inputName);
		return true;
	}

	if (name == "shader uniform") {
		// @intrinsic("shader uniform", uniformName) → load f32 from named uniform global
		// The SPIR-V patcher wraps this in a UBO struct with proper decorations
		std::string uniformName = getStringLiteral(args[0]);
		if (uniformName.empty()) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "shader uniform requires a string literal name", Range())
			);
			return false;
		}
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
		result = builder.CreateLoad(builder.getFloatTy(), global, uniformName + "_val");
		return true;
	}

	if (name == "shader output") {
		// @intrinsic("shader output", r, g, b, a) → store vec4 to shader output global
		TRY_EXPR(r, args[0]);
		TRY_EXPR(g, args[1]);
		TRY_EXPR(b, args[2]);
		TRY_EXPR(a, args[3]);

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
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "Shader output requires --emit-spirv and --shader-stage", Range())
			);
			return false;
		}
		builder.CreateStore(color, outGlobal);
		return true;
	}

	if (name == "extract element") {
		// @intrinsic("extract element", vector, index) → extract scalar from vector
		TRY_EXPR(vec, args[0]);
		if (auto *idxLit = std::get_if<int64_t>(&args[1]->literalValue)) {
			result = builder.CreateExtractElement(vec, (uint64_t)*idxLit, "elem");
			return true;
		}
		TRY_EXPR(idx, args[1]);
		result = builder.CreateExtractElement(vec, idx, "elem");
		return true;
	}

	ASSERT_UNREACHABLE("Unknown intrinsic: validated at parse time");
}
