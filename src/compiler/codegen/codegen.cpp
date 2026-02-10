#include "codegen.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "expression.h"
#include "native.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <unordered_map>

// Forward declarations
static bool generateSectionCode(ParseContext &context, Section *section);
static llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr);
static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<Expression *> &args, Type resultType);
static llvm::Function *generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<Type> &argTypes
);

// Get the LLVM type for a given Type
static llvm::Type *getLLVMType(ParseContext &context, Type type) { return type.toLLVM(*context.llvmContext); }

// Convert any value to boolean (i1) for conditional branches
static llvm::Value *
convertConditionToBool(ParseContext &context, llvm::Value *condValue, Type condType, const std::string &name) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (condType.kind == Type::Kind::Bool)
		return condValue; // already i1
	if (condType.kind == Type::Kind::Float) {
		llvm::Type *floatTy = condType.toLLVM(*context.llvmContext);
		return builder.CreateFCmpONE(condValue, llvm::ConstantFP::get(floatTy, 0.0), name);
	}
	llvm::Type *intTy = condType.toLLVM(*context.llvmContext);
	return builder.CreateICmpNE(condValue, llvm::ConstantInt::get(intTy, 0), name);
}

// Resolve a variable expression through macro bindings, stopping on self-reference.
static Expression *resolveMacroBinding(ParseContext &context, Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return expr;
	auto it = context.macroExpressionBindings.find(expr->variable->name);
	if (it != context.macroExpressionBindings.end() && it->second != expr)
		return resolveMacroBinding(context, it->second);
	return expr;
}

// Resolve the effective type of an expression during codegen.
// Follows macro expression bindings and pattern parameter types to compute the real type,
// even for expressions inside non-macro function bodies whose .type was never inferred.
static Type getEffectiveType(ParseContext &context, Expression *expr) {
	if (!expr)
		return {};

	switch (expr->kind) {
	case Expression::Kind::Literal:
		return expr->type; // Literal types are always set by inference

	case Expression::Kind::Variable: {
		Expression *resolved = resolveMacroBinding(context, expr);
		if (resolved != expr)
			return getEffectiveType(context, resolved);

		if (!expr->variable)
			return expr->type;
		std::string name = expr->variable->name;

		// Check pattern parameter types (monomorphized function: typed parameters)
		auto paramIt = context.patternParamTypes.find(name);
		if (paramIt != context.patternParamTypes.end())
			return paramIt->second;

		// Look up in section variables
		Section *sec = expr->range.line ? expr->range.line->section : nullptr;
		Variable *var = sec ? sec->findVariable(name) : nullptr;
		if (var)
			return var->type;

		return expr->type;
	}

	case Expression::Kind::IntrinsicCall: {
		// For intrinsics in non-macro function bodies, expr->type may be Undeduced.
		// Compute the type dynamically from the resolved argument types.
		if (isArithmeticOperator(expr->intrinsicName)) {
			if (expr->arguments.size() >= 3) {
				Type leftType = getEffectiveType(context, expr->arguments[1]);
				Type rightType = getEffectiveType(context, expr->arguments[2]);
				return isPointerArithmeticOperator(expr->intrinsicName) ? Type::promoteArithmetic(leftType, rightType)
																		: Type::promote(leftType, rightType);
			}
		}
		if (isComparisonOperator(expr->intrinsicName) || expr->intrinsicName == "and" || expr->intrinsicName == "or" ||
			expr->intrinsicName == "not")
			return {Type::Kind::Bool};
		if (expr->intrinsicName == "store" || expr->intrinsicName == "store at" || expr->intrinsicName == "loop while" ||
			expr->intrinsicName == "if" || expr->intrinsicName == "else if" || expr->intrinsicName == "else" ||
			expr->intrinsicName == "switch" || expr->intrinsicName == "case")
			return {Type::Kind::Void};
		if (expr->intrinsicName == "address of" && expr->arguments.size() >= 2)
			return getEffectiveType(context, expr->arguments[1]).pointed();
		if (expr->intrinsicName == "dereference" && expr->arguments.size() >= 2)
			return getEffectiveType(context, expr->arguments[1]).dereferenced();
		if (expr->intrinsicName == "load at")
			return {Type::Kind::Integer, 8};
		if (expr->intrinsicName == "return" && expr->arguments.size() >= 2)
			return getEffectiveType(context, expr->arguments[1]);
		if (expr->intrinsicName == "call") {
			// Format: @intrinsic("call", "library", "function", "return type", args...)
			if (expr->arguments.size() >= 4) {
				std::string retTypeStr;
				if (auto *str = std::get_if<std::string>(&expr->arguments[3]->literalValue))
					retTypeStr = *str;
				if (!retTypeStr.empty())
					return Type::fromString(retTypeStr);
			}
			return {Type::Kind::Integer, 4};
		}
		if (expr->intrinsicName == "cast" && expr->arguments.size() >= 3) {
			// Format: @intrinsic("cast", value, type_string[, bit_size])
			std::string target;
			if (auto *str = std::get_if<std::string>(&expr->arguments[2]->literalValue))
				target = *str;
			if (target == "integer" || target == "float") {
				Type::Kind kind = target == "integer" ? Type::Kind::Integer : Type::Kind::Float;
				int byteSize = 8;
				if (expr->arguments.size() >= 4) {
					if (auto *bits = std::get_if<int64_t>(&expr->arguments[3]->literalValue))
						byteSize = *bits / 8;
				}
				return {kind, byteSize};
			}
			if (!target.empty())
				return Type::fromString(target);
		}
		return expr->type;
	}

	case Expression::Kind::PatternCall:
		return expr->type;

	default:
		return expr->type;
	}
}

// Create an alloca at function entry (avoids stack growth in loops)
static llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, Type type) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *func = builder.GetInsertBlock()->getParent();
	llvm::IRBuilder<> entryBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
	llvm::Type *llvmType = getLLVMType(context, type);
	llvm::AllocaInst *alloca = entryBuilder.CreateAlloca(llvmType, nullptr, name);
	alloca->setAlignment(llvm::Align(8));
	return alloca;
}

// Generate a unique function name for a pattern
static std::string getPatternFunctionName(Section *section) {
	std::string name = (std::string)section->patternDefinitions.front()->range.subString;
	for (char &c : name) {
		if (!isalnum(c) && c != '_') {
			c = (c == ' ') ? '_' : (c % 10 + '0');
		}
	}
	return name;
}

// Generate a monomorphized LLVM function for a pattern definition with specific argument types
static llvm::Function *generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<Type> &argTypes
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	std::vector<std::string> varNames;
	for (const auto &[name, expr] : paramBindings) {
		varNames.push_back(name);
	}

	// All parameters are opaque pointers
	std::vector<llvm::Type *> paramTypes(varNames.size(), llvm::PointerType::getUnqual(*context.llvmContext));

	// Return type: void for effects, per-instantiation for expressions
	llvm::Type *returnType;
	if (section->type == SectionType::Effect) {
		returnType = builder.getVoidTy();
	} else {
		auto it = section->instantiations.find(argTypes);
		assert(it != section->instantiations.end() && "Missing instantiation for arg types");
		assert(it->second.returnType.isDeduced() && "Return type must be deduced before codegen");
		returnType = getLLVMType(context, it->second.returnType);
	}

	llvm::FunctionType *funcType = llvm::FunctionType::get(returnType, paramTypes, false);

	// Name includes type signature for uniqueness
	std::string funcName = getPatternFunctionName(section);
	for (const Type &t : argTypes) {
		funcName += "_" + t.toString();
	}

	llvm::Function *func = llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, context.llvmModule);

	size_t argIdx = 0;
	for (auto &arg : func->args()) {
		arg.setName(varNames[argIdx++]);
	}

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", func);

	// Save all codegen state
	llvm::BasicBlock *savedBlock = builder.GetInsertBlock();
	llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();
	auto savedPatternBindings = context.patternBindings;
	auto savedParamTypes = context.patternParamTypes;

	builder.SetInsertPoint(entry);

	// Set up bindings: map parameter names to LLVM values and their types
	context.patternBindings.clear();
	context.patternParamTypes.clear();
	argIdx = 0;
	for (auto &arg : func->args()) {
		context.patternBindings[varNames[argIdx]] = &arg;
		context.patternParamTypes[varNames[argIdx]] = argTypes[argIdx];
		argIdx++;
	}

	// Generate function body
	for (Section *child : section->children) {
		generateSectionCode(context, child);
	}

	// Add return for effects (expression functions use @intrinsic("return"))
	if (section->type == SectionType::Effect) {
		builder.CreateRetVoid();
	}

	// Restore all codegen state
	context.patternBindings = savedPatternBindings;
	context.patternParamTypes = savedParamTypes;

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
	}

	return func;
}

// Allocate all variables for a section at its start
static void allocateSectionVariables(ParseContext &context, Section *section) {
	for (auto &[name, varDef] : section->variableDefinitions) {
		Type varType = {Type::Kind::Integer}; // fallback
		Variable *var = section->findVariable(name);
		if (var)
			varType = var->type;
		varDef->alloca = createEntryAlloca(context, name, varType);
	}
}

// Get the pointer for a variable expression (for store operations)
static llvm::Value *getVariablePointer(ParseContext &context, Expression *expr) {
	expr = resolveMacroBinding(context, expr);
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return nullptr;

	std::string varName = expr->variable->name;

	auto bindingIt = context.patternBindings.find(varName);
	if (bindingIt != context.patternBindings.end())
		return bindingIt->second;

	VariableReference *varRef = expr->variable;
	VariableReference *definition = varRef->definition ? varRef->definition : varRef;
	if (definition->alloca)
		return definition->alloca;

	return nullptr;
}

// Ensure a value has the target LLVM type by inserting conversions if needed
static llvm::Value *ensureType(ParseContext &context, llvm::Value *val, Type fromType, Type toType) {
	if (fromType == toType || !val)
		return val;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *targetLLVM = toType.toLLVM(*context.llvmContext);

	// Pointer ↔ Integer conversions (check first, before kind-based checks)
	if (fromType.isPointer() && toType.kind == Type::Kind::Integer && !toType.isPointer())
		return builder.CreatePtrToInt(val, targetLLVM, "ptoi");
	if (!fromType.isPointer() && fromType.kind == Type::Kind::Integer && toType.isPointer())
		return builder.CreateIntToPtr(val, targetLLVM, "itop");

	// Integer → Integer (different sizes)
	if (fromType.kind == Type::Kind::Integer && toType.kind == Type::Kind::Integer) {
		if (fromType.byteSize < toType.byteSize)
			return builder.CreateSExt(val, targetLLVM, "sext");
		return builder.CreateTrunc(val, targetLLVM, "trunc");
	}

	// Float → Float (different sizes)
	if (fromType.kind == Type::Kind::Float && toType.kind == Type::Kind::Float) {
		if (fromType.byteSize < toType.byteSize)
			return builder.CreateFPExt(val, targetLLVM, "fpext");
		return builder.CreateFPTrunc(val, targetLLVM, "fptrunc");
	}

	// Integer → Float
	if (fromType.kind == Type::Kind::Integer && toType.kind == Type::Kind::Float)
		return builder.CreateSIToFP(val, targetLLVM, "itof");

	// Float → Integer
	if (fromType.kind == Type::Kind::Float && toType.kind == Type::Kind::Integer)
		return builder.CreateFPToSI(val, targetLLVM, "ftoi");

	// Bool → Integer
	if (fromType.kind == Type::Kind::Bool && toType.kind == Type::Kind::Integer)
		return builder.CreateZExt(val, targetLLVM, "btoi");

	// Bool → Float
	if (fromType.kind == Type::Kind::Bool && toType.kind == Type::Kind::Float) {
		llvm::Value *intVal = builder.CreateZExt(val, builder.getInt64Ty(), "btoi");
		return builder.CreateSIToFP(intVal, targetLLVM, "itof");
	}

	// Unsupported conversion - this should not happen if type inference is correct
	assert(false && "Unsupported type conversion in ensureType");
	return val;
}

// Generate code for an expression
static llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr) {
	if (!expr)
		return nullptr;

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (auto *intVal = std::get_if<int64_t>(&expr->literalValue)) {
			Type intType = getEffectiveType(context, expr);
			llvm::Type *llvmIntType = intType.toLLVM(*context.llvmContext);
			return llvm::ConstantInt::get(llvmIntType, *intVal, true);
		}
		if (auto *doubleVal = std::get_if<double>(&expr->literalValue)) {
			Type floatType = getEffectiveType(context, expr);
			llvm::Type *llvmFloatType = floatType.toLLVM(*context.llvmContext);
			return llvm::ConstantFP::get(llvmFloatType, *doubleVal);
		}
		if (auto *strVal = std::get_if<std::string>(&expr->literalValue)) {
			// TODO: strings are currently just i8* pointers to constant data.
			// String operations (concatenation, slicing, etc.) need runtime support.
			auto it = context.stringConstants.find(*strVal);
			if (it != context.stringConstants.end())
				return it->second;
			std::string globalName = ".str." + std::to_string(context.stringConstants.size());
			llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, *strVal, true);
			llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
				*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
			);
			context.stringConstants[*strVal] = strGlobal;
			return strGlobal;
		}
		// Unknown literal variant type - should never reach here after type inference
		ASSERT_UNREACHABLE("Unknown literal type in codegen");
	}

	case Expression::Kind::Variable: {
		Expression *resolved = resolveMacroBinding(context, expr);
		if (resolved != expr)
			return generateExpressionCode(context, resolved);

		if (!expr->variable)
			return nullptr;
		std::string varName = expr->variable->name;

		// Determine this variable's type for loading
		Type varType = getEffectiveType(context, expr);
		llvm::Type *loadType = getLLVMType(context, varType);

		// Pattern parameter: load from function argument pointer
		auto bindingIt = context.patternBindings.find(varName);
		if (bindingIt != context.patternBindings.end())
			return builder.CreateAlignedLoad(loadType, bindingIt->second, llvm::Align(8), varName + "_val");

		// Local variable: load from alloca
		VariableReference *varRef = expr->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		if (definition->alloca)
			return builder.CreateAlignedLoad(loadType, definition->alloca, llvm::Align(8), varName + "_val");

		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unknown variable: " + varName, expr->range));
		return nullptr;
	}

	case Expression::Kind::PatternCall: {
		if (!expr->patternMatch || !expr->patternMatch->matchedEndNode)
			return nullptr;

		PatternDefinition *matchedDef = expr->patternMatch->matchedEndNode->matchingDefinition;
		Section *matchedSection = matchedDef->section;
		if (!matchedSection)
			return nullptr;

		// Sort arguments by source position
		std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);

		// Build parameter name → argument expression mapping
		std::vector<std::pair<std::string, Expression *>> paramBindings;
		size_t argIndex = 0;
		for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
			auto paramIt = node->parameterNames.find(matchedDef);
			if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
				paramBindings.push_back({paramIt->second, sortedArgs[argIndex++]});
			}
		}

		if (matchedSection->isMacro) {
			// Macro: inline the body with expression substitution
			auto savedMacroBindings = context.macroExpressionBindings;
			Section *savedBodySection = context.currentBodySection;

			for (const auto &[paramName, argExpr] : paramBindings) {
				context.macroExpressionBindings[paramName] = argExpr;
			}

			// Only section-type macros (like "if condition:", "loop while condition:")
			// should pick up and process the body section opened by this line.
			// Expression/effect macros (like "not value:", "a + b") must NOT process
			// the body section, even if they appear on a line that opens one.
			Section *bodySection = nullptr;
			if (matchedSection->type == SectionType::Section) {
				bodySection = expr->range.line ? expr->range.line->sectionOpening : nullptr;
				context.currentBodySection = bodySection;
			}

			llvm::Value *result = nullptr;
			for (Section *child : matchedSection->children) {
				for (CodeLine *line : child->codeLines) {
					if (line->expression)
						result = generateExpressionCode(context, line->expression);
				}
			}

			if (bodySection) {
				generateSectionCode(context, bodySection);
				if (bodySection->exitBlock) {
					if (!builder.GetInsertBlock()->getTerminator()) {
						llvm::BasicBlock *target =
							bodySection->branchBackBlock ? bodySection->branchBackBlock : bodySection->exitBlock;
						builder.CreateBr(target);
					}
					builder.SetInsertPoint(bodySection->exitBlock);
				}
			}

			context.macroExpressionBindings = savedMacroBindings;
			context.currentBodySection = savedBodySection;
			return result;
		}

		// Non-macro pattern: monomorphized function call.
		// Compute argument types at this call site for specialization.
		std::vector<Type> argTypes;
		for (const auto &[paramName, argExpr] : paramBindings) {
			argTypes.push_back(getEffectiveType(context, argExpr));
		}

		// Look up or generate the specialized function
		Instantiation &inst = matchedSection->instantiations[argTypes];
		if (!inst.llvmFunction) {
			inst.llvmFunction = generateSpecializedFunction(context, matchedSection, paramBindings, argTypes);
		}
		llvm::Function *func = inst.llvmFunction;

		// Build call arguments: pass variable pointers or temp allocas
		std::vector<llvm::Value *> args;
		for (size_t i = 0; i < paramBindings.size(); i++) {
			Expression *argExpr = paramBindings[i].second;
			llvm::Value *ptr = getVariablePointer(context, argExpr);
			if (ptr) {
				args.push_back(ptr);
			} else {
				llvm::Value *argVal = generateExpressionCode(context, argExpr);
				if (argVal) {
					llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "tmp", argTypes[i]);
					builder.CreateAlignedStore(argVal, tempAlloca, llvm::Align(8));
					args.push_back(tempAlloca);
				}
			}
		}

		return builder.CreateCall(func, args);
	}

	case Expression::Kind::IntrinsicCall: {
		std::vector<Expression *> args(expr->arguments.begin() + 1, expr->arguments.end());
		return generateIntrinsicCode(context, expr->intrinsicName, args, getEffectiveType(context, expr));
	}

	case Expression::Kind::Pending:
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unresolved pending expression", expr->range));
		return nullptr;
	}

	return nullptr;
}

// Helper to extract string literal from expression
static std::string getStringLiteral(Expression *expr) {
	if (expr && expr->kind == Expression::Kind::Literal) {
		if (auto *str = std::get_if<std::string>(&expr->literalValue))
			return *str;
	}
	return "";
}

// Generate code for an intrinsic call.
// All type decisions use getEffectiveType to resolve through macro/pattern bindings.
static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<Expression *> &args, Type resultType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	if (name == "store") {
		if (args.size() >= 2) {
			llvm::Value *ptr = getVariablePointer(context, args[0]);
			llvm::Value *val = generateExpressionCode(context, args[1]);
			if (ptr && val) {
				Type varType = getEffectiveType(context, args[0]);
				Type valType = getEffectiveType(context, args[1]);
				val = ensureType(context, val, valType, varType);
				builder.CreateAlignedStore(val, ptr, llvm::Align(8));
			} else if (!ptr) {
				context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Cannot store to non-variable", Range()));
			}
		}
		return nullptr;
	}

	// Arithmetic intrinsics
	if (isArithmeticOperator(name)) {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			Type leftType = getEffectiveType(context, args[0]);
			Type rightType = getEffectiveType(context, args[1]);

			// Pointer arithmetic: ptr +/- integer → GEP
			if (isPointerArithmeticOperator(name) && (leftType.isPointer() || rightType.isPointer())) {
				llvm::Value *ptrVal = leftType.isPointer() ? left : right;
				llvm::Value *indexVal = leftType.isPointer() ? right : left;
				Type ptrType = leftType.isPointer() ? leftType : rightType;
				llvm::Type *elemType = ptrType.dereferenced().toLLVM(*context.llvmContext);
				if (name == "subtract" && leftType.isPointer())
					indexVal = builder.CreateNeg(indexVal, "neg_idx");
				return builder.CreateGEP(elemType, ptrVal, indexVal, "ptr_arith");
			}

			Type promoted = Type::promote(leftType, rightType);

			left = ensureType(context, left, leftType, promoted);
			right = ensureType(context, right, rightType, promoted);

			if (promoted.kind == Type::Kind::Float) {
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
		}
		// Arithmetic operator called with insufficient arguments
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Arithmetic operator requires 2 operands", Range()));
		return nullptr;
	}

	// Comparison intrinsics
	if (isComparisonOperator(name)) {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			Type leftType = getEffectiveType(context, args[0]);
			Type rightType = getEffectiveType(context, args[1]);
			Type promoted = Type::promote(leftType, rightType);

			left = ensureType(context, left, leftType, promoted);
			right = ensureType(context, right, rightType, promoted);

			llvm::Value *cmp;
			if (promoted.kind == Type::Kind::Float) {
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
			if (resultType.kind == Type::Kind::Bool)
				return cmp; // already i1
			return builder.CreateZExt(cmp, getLLVMType(context, resultType), "cmp_ext");
		}
		// Comparison operator called with insufficient arguments
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Comparison operator requires 2 operands", Range()));
		return nullptr;
	}

	// Logical operators
	if (name == "and" || name == "or") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			Type leftType = getEffectiveType(context, args[0]);
			Type rightType = getEffectiveType(context, args[1]);

			left = convertConditionToBool(context, left, leftType, "tobool");
			right = convertConditionToBool(context, right, rightType, "tobool");

			if (name == "and")
				return builder.CreateAnd(left, right, "and");
			else
				return builder.CreateOr(left, right, "or");
		}
		return builder.getFalse();
	}

	if (name == "not") {
		if (args.size() >= 1) {
			llvm::Value *val = generateExpressionCode(context, args[0]);
			Type valType = getEffectiveType(context, args[0]);

			val = convertConditionToBool(context, val, valType, "tobool");

			return builder.CreateXor(val, builder.getTrue(), "not");
		}
		return builder.getFalse();
	}

	// Pointer intrinsics
	if (name == "address of") {
		if (args.size() >= 1) {
			llvm::Value *ptr = getVariablePointer(context, args[0]);
			assert(ptr && "address of requires a variable");
			return ptr;
		}
		return nullptr;
	}

	if (name == "dereference") {
		if (args.size() >= 1) {
			llvm::Value *ptrVal = generateExpressionCode(context, args[0]);
			Type ptrType = getEffectiveType(context, args[0]);
			Type elemType = ptrType.dereferenced();
			llvm::Type *elemLLVMType = getLLVMType(context, elemType);
			return builder.CreateAlignedLoad(elemLLVMType, ptrVal, llvm::Align(8), "deref");
		}
		return nullptr;
	}

	// Array/pointer intrinsics
	if (name == "store at") {
		if (args.size() >= 3) {
			llvm::Value *ptr = generateExpressionCode(context, args[0]);
			llvm::Value *index = generateExpressionCode(context, args[1]);
			llvm::Value *value = generateExpressionCode(context, args[2]);
			Type ptrType = getEffectiveType(context, args[0]);

			llvm::Value *ptrAsPtr = ptrType.isPointer() ? ptr : builder.CreateIntToPtr(ptr, builder.getPtrTy());
			llvm::Value *elementPtr = builder.CreateGEP(builder.getInt64Ty(), ptrAsPtr, index);
			builder.CreateAlignedStore(value, elementPtr, llvm::Align(8));
		}
		return nullptr;
	}

	if (name == "load at") {
		if (args.size() >= 2) {
			llvm::Value *ptr = generateExpressionCode(context, args[0]);
			llvm::Value *index = generateExpressionCode(context, args[1]);
			Type ptrType = getEffectiveType(context, args[0]);

			llvm::Value *ptrAsPtr = ptrType.isPointer() ? ptr : builder.CreateIntToPtr(ptr, builder.getPtrTy());
			llvm::Value *elementPtr = builder.CreateGEP(builder.getInt64Ty(), ptrAsPtr, index);
			return builder.CreateAlignedLoad(builder.getInt64Ty(), elementPtr, llvm::Align(8));
		}
		// 'load at' intrinsic called with insufficient arguments
		context.diagnostics.push_back(
			Diagnostic(Diagnostic::Level::Error, "'load at' intrinsic requires 2 arguments (pointer, index)", Range())
		);
		return nullptr;
	}

	if (name == "loop while") {
		if (args.empty()) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "loop while requires a condition", Range()));
			return nullptr;
		}

		Section *bodySection = context.currentBodySection;
		if (!bodySection) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "loop while requires a body section", Range()));
			return nullptr;
		}

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_cond", func);
		llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_body", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_exit", func);

		builder.CreateBr(condBlock);
		builder.SetInsertPoint(condBlock);

		llvm::Value *condValue = generateExpressionCode(context, args[0]);
		Type condType = getEffectiveType(context, args[0]);
		llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "while_cond_bool");
		builder.CreateCondBr(condBool, bodyBlock, exitBlock);

		builder.SetInsertPoint(bodyBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = condBlock;

		return nullptr;
	}

	if (name == "if") {
		if (args.empty()) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "if requires a condition", Range()));
			return nullptr;
		}

		Section *bodySection = context.currentBodySection;
		if (!bodySection) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "if requires a body section", Range()));
			return nullptr;
		}

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_then", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_exit", func);

		llvm::Value *condValue = generateExpressionCode(context, args[0]);
		Type condType = getEffectiveType(context, args[0]);
		llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "if_cond");
		builder.CreateCondBr(condBool, thenBlock, exitBlock);

		builder.SetInsertPoint(thenBlock);
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (name == "else" || name == "else if") {
		Section *bodySection = context.currentBodySection;
		if (!bodySection) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, name + " requires a body section", Range()));
			return nullptr;
		}

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
			// Evaluate condition, branch to elif_then or newExitBlock
			if (args.empty()) {
				context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "else if requires a condition", Range()));
				return nullptr;
			}

			llvm::BasicBlock *elifThenBlock = llvm::BasicBlock::Create(*context.llvmContext, "elif_then", func);

			llvm::Value *condValue = generateExpressionCode(context, args[0]);
			Type condType = getEffectiveType(context, args[0]);
			llvm::Value *condBool = convertConditionToBool(context, condValue, condType, "elif_cond");
			builder.CreateCondBr(condBool, elifThenBlock, newExitBlock);

			builder.SetInsertPoint(elifThenBlock);
		}

		bodySection->exitBlock = newExitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (name == "switch") {
		if (args.empty()) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "switch requires a value", Range()));
			return nullptr;
		}

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		llvm::Value *switchValue = generateExpressionCode(context, args[0]);
		Type switchType = getEffectiveType(context, args[0]);

		// Ensure the value is an integer (LLVM switch requires integer operand)
		if (switchType.kind != Type::Kind::Integer && switchType.kind != Type::Kind::Bool) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "switch requires an integer value", Range())
			);
			return nullptr;
		}

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
		if (args.empty()) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "case requires a value", Range()));
			return nullptr;
		}

		Section *bodySection = context.currentBodySection;
		if (!bodySection) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "case requires a body section", Range()));
			return nullptr;
		}

		assert(context.currentSwitchInst && "case outside of switch");

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		// Evaluate the case value — must be a constant integer
		llvm::Value *caseValue = generateExpressionCode(context, args[0]);
		llvm::ConstantInt *caseConst = llvm::dyn_cast<llvm::ConstantInt>(caseValue);
		assert(caseConst && "case value must be a constant integer");

		llvm::BasicBlock *caseBlock = llvm::BasicBlock::Create(*context.llvmContext, "case", func);
		context.currentSwitchInst->addCase(caseConst, caseBlock);

		builder.SetInsertPoint(caseBlock);
		bodySection->exitBlock = context.currentSwitchExitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (name == "return") {
		if (args.size() >= 1) {
			llvm::Value *returnValue = generateExpressionCode(context, args[0]);
			builder.CreateRet(returnValue);
		}
		return nullptr;
	}

	if (name == "call") {
		// Format: args[0]="library", args[1]="function", args[2]="return type", args[3+]=actual args
		if (args.size() >= 3) {
			std::string library = getStringLiteral(args[0]);
			std::string funcName = getStringLiteral(args[1]);
			std::string retTypeStr = getStringLiteral(args[2]);

			if (!library.empty() && library != "libc")
				context.requiredLibraries.insert(library);

			Type returnType = Type::fromString(retTypeStr);
			llvm::Type *returnLLVMType = returnType.toLLVM(*context.llvmContext);

			// Build call arguments — string literals become global constant pointers
			std::vector<llvm::Value *> callArgs;
			for (size_t i = 3; i < args.size(); ++i) {
				if (args[i]->kind == Expression::Kind::Literal) {
					if (auto *str = std::get_if<std::string>(&args[i]->literalValue)) {
						std::string globalName = ".str." + std::to_string(context.stringConstants.size());
						llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, *str, true);
						llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
							*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst,
							globalName
						);
						context.stringConstants[*str] = strGlobal;
						callArgs.push_back(strGlobal);
						continue;
					}
				}
				llvm::Value *argVal = generateExpressionCode(context, args[i]);
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
			// If return type is void, return nullptr (no value to use)
			if (returnType.kind == Type::Kind::Void)
				return nullptr;
			return callResult;
		}
		return nullptr;
	}

	if (name == "cast") {
		// Format: args[0]=value, args[1]=type_string[, args[2]=bit_size]
		if (args.size() >= 2) {
			std::string targetStr = getStringLiteral(args[1]);
			llvm::Value *val = generateExpressionCode(context, args[0]);
			Type fromType = getEffectiveType(context, args[0]);

			if (targetStr == "string") {
				// Convert to string via snprintf to a stack buffer
				if (fromType.kind == Type::Kind::String)
					return val; // already a string
				llvm::AllocaInst *strBuf = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt64(32), "strbuf");
				llvm::Function *snprintfFunc = context.llvmModule->getFunction("snprintf");
				if (!snprintfFunc) {
					llvm::FunctionType *ft = llvm::FunctionType::get(builder.getInt32Ty(), {}, true);
					snprintfFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "snprintf", context.llvmModule);
				}
				const char *fmt = (fromType.kind == Type::Kind::Float) ? "%g" : "%ld";
				llvm::Value *printVal = val;
				if (fromType.kind == Type::Kind::Integer && fromType.byteSize < 8)
					printVal = builder.CreateSExt(val, builder.getInt64Ty(), "widen_for_printf");
				auto fmtIt = context.stringConstants.find(fmt);
				llvm::Value *fmtVal;
				if (fmtIt != context.stringConstants.end()) {
					fmtVal = fmtIt->second;
				} else {
					std::string globalName = ".str." + std::to_string(context.stringConstants.size());
					llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, fmt, true);
					auto *strGlobal = new llvm::GlobalVariable(
						*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
					);
					context.stringConstants[fmt] = strGlobal;
					fmtVal = strGlobal;
				}
				builder.CreateCall(snprintfFunc, {strBuf, builder.getInt64(32), fmtVal, printVal});
				return strBuf;
			}

			Type toType;
			if (targetStr == "integer" || targetStr == "float") {
				Type::Kind kind = targetStr == "integer" ? Type::Kind::Integer : Type::Kind::Float;
				int byteSize = 8;
				if (args.size() >= 3) {
					if (auto *bits = std::get_if<int64_t>(&args[2]->literalValue))
						byteSize = *bits / 8;
				}
				toType = {kind, byteSize};
			} else {
				toType = Type::fromString(targetStr);
			}
			return ensureType(context, val, fromType, toType);
		}
		return nullptr;
	}

	context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unknown intrinsic: " + name, Range()));
	return nullptr;
}

// Generate code for a section (process pattern references)
static bool generateSectionCode(ParseContext &context, Section *section) {
	allocateSectionVariables(context, section);

	for (CodeLine *line : section->codeLines) {
		if (line->expression)
			generateExpressionCode(context, line->expression);
	}

	return true;
}

bool generateCode(ParseContext &context) {
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("3bx_module", *context.llvmContext);
	context.llvmBuilder = new llvm::IRBuilder<>(*context.llvmContext);
	context.llvmModule->setTargetTriple(llvm::sys::getDefaultTargetTriple());

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	// No first pass — non-macro functions are generated on-demand via monomorphization.

	// Create main function
	llvm::FunctionType *mainType = llvm::FunctionType::get(builder.getInt32Ty(), false);
	llvm::Function *mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", mainFunc);
	builder.SetInsertPoint(entry);

	if (!generateSectionCode(context, context.mainSection))
		return false;

	builder.CreateRet(builder.getInt32(0));

	// Verify
	std::string error;
	llvm::raw_string_ostream errorStream(error);
	if (llvm::verifyModule(*context.llvmModule, &errorStream)) {
		llvm::errs() << "\n=== Invalid LLVM IR (for debugging) ===\n";
		context.llvmModule->print(llvm::errs(), nullptr);
		llvm::errs() << "=== End Invalid LLVM IR ===\n\n";
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "LLVM verification failed: " + error, Range()));
		return false;
	}

	// Optimization passes
	if (context.options.optimizationLevel > 0) {
		llvm::LoopAnalysisManager lam;
		llvm::FunctionAnalysisManager fam;
		llvm::CGSCCAnalysisManager cgam;
		llvm::ModuleAnalysisManager mam;

		llvm::PassBuilder pb;
		pb.registerModuleAnalyses(mam);
		pb.registerCGSCCAnalyses(cgam);
		pb.registerFunctionAnalyses(fam);
		pb.registerLoopAnalyses(lam);
		pb.crossRegisterProxies(lam, fam, cgam, mam);

		llvm::OptimizationLevel optLevel;
		switch (context.options.optimizationLevel) {
		case 1:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		case 2:
			optLevel = llvm::OptimizationLevel::O2;
			break;
		case 3:
			optLevel = llvm::OptimizationLevel::O3;
			break;
		default:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		}

		llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(optLevel);
		mpm.run(*context.llvmModule, mam);
	}

	// Output
	if (context.options.emitLLVM) {
		std::string outputPath = context.options.outputPath;
		if (outputPath.empty())
			outputPath = context.options.inputPath + ".ll";
		std::error_code ec;
		llvm::raw_fd_ostream out(outputPath, ec);
		if (ec) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "Failed to open output file: " + ec.message(), Range())
			);
			return false;
		}
		context.llvmModule->print(out, nullptr);
	} else {
		if (!emitNativeExecutable(context))
			return false;
	}

	return true;
}
