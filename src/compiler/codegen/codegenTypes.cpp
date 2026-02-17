#include "classDefinition.h"
#include "classSection.h"
#include "codegenInternal.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "intrinsicInfo.h"
#include "patternDefinition.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <unordered_map>

// Get the LLVM type for a given Type
llvm::Type *getLLVMType(ParseContext &context, Type type) { return type.toLLVM(*context.llvmContext); }

// Convert any value to boolean (i1) for conditional branches
llvm::Value *convertConditionToBool(ParseContext &context, llvm::Value *condValue, Type condType, const std::string &name) {
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

// Resolve a variable expression through the current macro's bindings (single level).
// With scoped bindings, only the current macro's parameters are in the map.
// Caller scope resolution is handled by popping the binding stack.
Expression *resolveMacroBinding(ParseContext &context, Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return expr;
	auto it = context.macroExpressionBindings.find(expr->variable->name);
	if (it != context.macroExpressionBindings.end() && it->second != expr)
		return it->second;
	return expr;
}

// MacroScopeGuard implementation
void MacroScopeGuard::popToCallerScope() {
	assert(!context.macroBindingStack.empty());
	savedBindings = context.macroExpressionBindings;
	context.macroExpressionBindings = context.macroBindingStack.top();
	context.macroBindingStack.pop();
	active = true;
}

MacroScopeGuard::~MacroScopeGuard() {
	if (active) {
		context.macroBindingStack.push(context.macroExpressionBindings);
		context.macroExpressionBindings = savedBindings;
	}
}

// Resolve the effective type of an expression during codegen.
// Follows macro expression bindings and pattern parameter types to compute the real type,
// even for expressions inside non-macro function bodies whose .type was never inferred.
Type getEffectiveType(ParseContext &context, Expression *expr) {
	if (!expr)
		return {};

	switch (expr->kind) {
	case Expression::Kind::Literal:
		return expr->type; // Literal types are always set by inference

	case Expression::Kind::Variable: {
		Expression *resolved = resolveMacroBinding(context, expr);
		if (resolved != expr) {
			MacroScopeGuard guard(context);
			if (!context.macroBindingStack.empty())
				guard.popToCallerScope();
			return getEffectiveType(context, resolved);
		}

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
		const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
		if (info) {
			switch (info->returnKind) {
			case IntrinsicReturnKind::SameAsArgs:
				if (info->argCount == 2) {
					return getEffectiveType(context, expr->arguments[1]);
				} else {
					Type leftType = getEffectiveType(context, expr->arguments[1]);
					Type rightType = getEffectiveType(context, expr->arguments[2]);
					return isPointerArithmeticOperator(expr->intrinsicName) ? Type::promoteArithmetic(leftType, rightType)
																			: Type::promote(leftType, rightType);
				}
			case IntrinsicReturnKind::Bool:
				return {Type::Kind::Bool};
			case IntrinsicReturnKind::Void:
				return {Type::Kind::Void};
			case IntrinsicReturnKind::Float:
				return {Type::Kind::Float, 4};
			case IntrinsicReturnKind::Custom:
				break;
			}
		}
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
		if (expr->intrinsicName == "construct" || expr->intrinsicName == "property")
			return expr->type; // Type fully determined during inference
		if (expr->intrinsicName == "cast" && expr->arguments.size() >= 3) {
			// Class cast: type was fully determined during inference
			if (expr->type.kind == Type::Kind::Class)
				return expr->type;
			// Format: @intrinsic("cast", value, type_string[, bit_size])
			// Resolve through macro bindings since cast args may be macro parameters
			Expression *typeStrExpr = resolveMacroBinding(context, expr->arguments[2]);
			std::string target;
			if (auto *str = std::get_if<std::string>(&typeStrExpr->literalValue))
				target = *str;
			if (target == "integer" || target == "float") {
				Type::Kind kind = target == "integer" ? Type::Kind::Integer : Type::Kind::Float;
				int byteSize = 8;
				if (expr->arguments.size() >= 4) {
					Expression *bitsExpr = resolveMacroBinding(context, expr->arguments[3]);
					if (auto *bits = std::get_if<int64_t>(&bitsExpr->literalValue))
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
llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, Type type) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *func = builder.GetInsertBlock()->getParent();
	llvm::IRBuilder<> entryBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
	llvm::Type *llvmType = getLLVMType(context, type);
	llvm::AllocaInst *alloca = entryBuilder.CreateAlloca(llvmType, nullptr, name);
	alloca->setAlignment(llvm::Align(8));
	return alloca;
}

// Generate a unique function name for a pattern
std::string getPatternFunctionName(Section *section) {
	std::string name = (std::string)section->patternDefinitions.front()->range.subString;
	for (char &c : name) {
		if (!isalnum(c) && c != '_') {
			c = (c == ' ') ? '_' : (c % 10 + '0');
		}
	}
	return name;
}

// Allocate all variables for a section at its start
void allocateSectionVariables(ParseContext &context, Section *section) {
	for (auto &[name, varDef] : section->variableDefinitions) {
		Type varType = {Type::Kind::Integer}; // fallback
		Variable *var = section->findVariable(name);
		if (var)
			varType = var->type;
		if (!varType.isDeduced())
			continue;

		// Check if this is a global variable
		if (var && var->isGlobal) {
			// Create or get existing global variable
			if (!context.globalLLVMVariables.contains(name)) {
				llvm::Type *llvmType = varType.toLLVM(*context.llvmContext);
				llvm::Constant *initializer = llvm::Constant::getNullValue(llvmType);
				auto *globalVar = new llvm::GlobalVariable(
					*context.llvmModule, llvmType, false, // not constant
					llvm::GlobalValue::InternalLinkage, initializer, name
				);
				context.globalLLVMVariables[name] = globalVar;
				// Store in alloca field so existing code can find it
				varDef->alloca = reinterpret_cast<llvm::AllocaInst *>(globalVar);
			}
		} else {
			// Local variable - create alloca as before
			varDef->alloca = createEntryAlloca(context, name, varType);
		}
	}
}

// Get the pointer for a variable expression (for store operations).
// Recursively resolves through nested macro binding scopes to find the actual variable.
llvm::Value *getVariablePointer(ParseContext &context, Expression *expr) {
	// Resolve through potentially multiple levels of macro bindings.
	// Each level pops to the caller's scope, so nested macros like
	// `add value to target` → `set var to val` can resolve var → target → iteration.
	std::vector<std::unordered_map<std::string, Expression *>> poppedScopes;

	while (true) {
		Expression *resolved = resolveMacroBinding(context, expr);
		if (resolved == expr)
			break; // No more macro bindings to resolve
		// Pop to caller scope so the resolved expression is evaluated in the right context
		if (!context.macroBindingStack.empty()) {
			poppedScopes.push_back(context.macroExpressionBindings);
			context.macroExpressionBindings = context.macroBindingStack.top();
			context.macroBindingStack.pop();
		}
		expr = resolved;
	}

	llvm::Value *result = nullptr;

	if (expr && expr->kind == Expression::Kind::Variable && expr->variable) {
		std::string varName = expr->variable->name;

		auto bindingIt = context.patternBindings.find(varName);
		if (bindingIt != context.patternBindings.end()) {
			result = bindingIt->second;
		} else {
			VariableReference *varRef = expr->variable;
			VariableReference *definition = varRef->definition ? varRef->definition : varRef;
			if (definition->alloca)
				result = definition->alloca;
		}
	}

	// Restore all popped scopes in reverse order
	for (auto it = poppedScopes.rbegin(); it != poppedScopes.rend(); ++it) {
		context.macroBindingStack.push(context.macroExpressionBindings);
		context.macroExpressionBindings = *it;
	}

	return result;
}

// Ensure a value has the target LLVM type by inserting conversions if needed
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, Type fromType, Type toType) {
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
