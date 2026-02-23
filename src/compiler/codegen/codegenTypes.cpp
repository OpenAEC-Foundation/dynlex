#include "classDefinition.h"
#include "classSection.h"
#include "codegenInternal.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "intrinsicInfo.h"
#include "patternDefinition.h"
#include "sourceFile.h"
#include "type.h"
#include "variable.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <filesystem>
#include <unordered_map>

// Get the LLVM type for a given DataType
llvm::Type *getLLVMType(ParseContext &context, DataType type) { return type.toLLVM(*context.llvmContext); }

// Get the DWARF debug type for a given DataType
llvm::DIType *getDIType(ParseContext &context, DataType type) {
	if (!context.diBuilder)
		return nullptr;

	if (type.kind == DataType::Kind::Void)
		return nullptr;

	// Pointers: create pointer to inner type
	if (type.pointerDepth > 0) {
		DataType inner = type;
		inner.pointerDepth--;
		return context.diBuilder->createPointerType(getDIType(context, inner), 64);
	}

	switch (type.kind) {
	case DataType::Kind::Bool:
		return context.diBuilder->createBasicType("bool", 8, llvm::dwarf::DW_ATE_boolean);
	case DataType::Kind::Float: {
		int bits = type.numericSize * 8;
		std::string name = type.numericSize == 4 ? "f32" : "f64";
		return context.diBuilder->createBasicType(name, bits, llvm::dwarf::DW_ATE_float);
	}
	case DataType::Kind::Int: {
		int bits = type.numericSize * 8;
		return context.diBuilder->createBasicType("i" + std::to_string(bits), bits, llvm::dwarf::DW_ATE_signed);
	}
	case DataType::Kind::Class: {
		if (!type.classDefinition || type.classInstIndex < 0)
			return nullptr;
		ClassInstantiation &inst = type.classDefinition->instantiations[type.classInstIndex];
		auto &fields = type.classDefinition->fields;
		llvm::DIFile *file = nullptr;
		if (!type.classDefinition->range.line)
			return nullptr;
		file = getOrCreateDIFile(context, type.classDefinition->range.line->sourceFile);

		// Calculate struct layout
		std::vector<llvm::Metadata *> members;
		uint64_t offsetBits = 0;
		for (size_t i = 0; i < fields.size() && i < inst.fieldTypes.size(); i++) {
			llvm::DIType *fieldDIType = getDIType(context, inst.fieldTypes[i]);
			uint64_t fieldSizeBits = fieldDIType ? fieldDIType->getSizeInBits() : 64;
			auto *member = context.diBuilder->createMemberType(
				nullptr, fields[i].name, file, 0, fieldSizeBits, 0, offsetBits, llvm::DINode::FlagZero, fieldDIType
			);
			members.push_back(member);
			offsetBits += fieldSizeBits;
		}
		std::string className = type.classDefinition->patternNames.empty() ? "class" : type.classDefinition->patternNames[0];
		return context.diBuilder->createStructType(
			nullptr, className, file, 0, offsetBits, 0, llvm::DINode::FlagZero, nullptr,
			context.diBuilder->getOrCreateArray(members)
		);
	}
	default:
		return nullptr;
	}
}

// Get or create a DIFile for a source file
llvm::DIFile *getOrCreateDIFile(ParseContext &context, lsp::SourceFile *sourceFile) {
	if (!context.diBuilder || !sourceFile)
		return nullptr;

	auto it = context.diFiles.find(sourceFile->uri);
	if (it != context.diFiles.end())
		return it->second;

	// Convert URI to filesystem path (strip file:// prefix if present)
	std::string path = sourceFile->uri;
	if (path.starts_with("file://"))
		path = path.substr(7);

	std::filesystem::path fsPath(path);
	std::string directory = fsPath.parent_path().string();
	std::string filename = fsPath.filename().string();

	llvm::DIFile *diFile = context.diBuilder->createFile(filename, directory);
	context.diFiles[sourceFile->uri] = diFile;
	return diFile;
}

// Convert any value to boolean (i1) for conditional branches
llvm::Value *convertConditionToBool(ParseContext &context, llvm::Value *condValue, DataType condType, const std::string &name) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (condType.kind == DataType::Kind::Bool)
		return condValue; // already i1
	if (condType.kind == DataType::Kind::Float) {
		llvm::Type *floatTy = condType.toLLVM(*context.llvmContext);
		return builder.CreateFCmpONE(condValue, llvm::ConstantFP::get(floatTy, 0.0), name);
	}
	llvm::Type *intTy = condType.toLLVM(*context.llvmContext);
	return builder.CreateICmpNE(condValue, llvm::ConstantInt::get(intTy, 0), name);
}

// Resolve a Variable expression one step through the current macro's binding map.
// Returns the bound expression (which lives in the caller's scope), or expr unchanged
// if no binding exists. Each resolution crosses one scope boundary — the caller must
// pop the binding stack before evaluating the result (see MacroScopeGuard::popToCallerScope).
Expression *resolveVariableBinding(ParseContext &context, Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return expr;
	auto it = context.macroExpressionBindings.find(expr->variable->name);
	if (it != context.macroExpressionBindings.end() && it->second != expr)
		return it->second;
	return expr;
}

// Resolve an expression through all macro layers: variable bindings (which cross
// scope boundaries upward) and macro PatternCall expansions (which push new scopes
// downward). Variable bindings don't modify the stack; PatternCall expansions push
// one scope each. Returns the number of scopes pushed, so the caller can pop them
// when done. Use this when you need to see through macro indirection to inspect the
// underlying expression kind (e.g., detecting a property intrinsic inside a store).
void resolveThroughMacroLayers(ParseContext &context, Expression *&expr) {
	while (expr) {
		// Variable bindings: resolve in current scope, or pop to parent scopes
		if (expr->kind == Expression::Kind::Variable && expr->variable) {
			auto it = context.macroExpressionBindings.find(expr->variable->name);
			if (it != context.macroExpressionBindings.end() && it->second != expr) {
				expr = it->second;
				continue;
			}
			// Not found in current scope — try parent scopes
			if (!context.macroBindingStack.empty()) {
				context.macroExpressionBindings = context.macroBindingStack.top();
				context.macroBindingStack.pop();
				continue;
			}
		}
		// Macro PatternCall: expand into the macro body, pushing a new binding scope
		std::unordered_map<std::string, Expression *> innerBindings;
		Expression *bodyExpr = expandMacroPatternCall(expr, innerBindings);
		if (bodyExpr) {
			context.macroBindingStack.push(context.macroExpressionBindings);
			context.macroExpressionBindings = std::move(innerBindings);
			expr = bodyExpr;
			continue;
		}
		break;
	}
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
DataType getEffectiveType(ParseContext &context, Expression *expr) {
	if (!expr)
		return {};

	switch (expr->kind) {
	case Expression::Kind::Literal:
		return expr->type; // Literal types are always set by inference

	case Expression::Kind::Variable: {
		Expression *resolved = resolveVariableBinding(context, expr);
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
					DataType leftType = getEffectiveType(context, expr->arguments[1]);
					DataType rightType = getEffectiveType(context, expr->arguments[2]);
					DataType result;
					DataType::promoteArithmetic(leftType, rightType, result);
					return result;
				}
			case IntrinsicReturnKind::Bool:
				return {DataType::Kind::Bool};
			case IntrinsicReturnKind::Void:
				return {DataType::Kind::Void};
			case IntrinsicReturnKind::Float:
				return {DataType::Kind::Float, 4};
			case IntrinsicReturnKind::Custom:
				break;
			}
		}
		if (expr->intrinsicName == "address of" && expr->arguments.size() >= 2)
			return getEffectiveType(context, expr->arguments[1]).pointed();
		if (expr->intrinsicName == "dereference" && expr->arguments.size() >= 2)
			return getEffectiveType(context, expr->arguments[1]).dereferenced();
		if (expr->intrinsicName == "load at")
			return {DataType::Kind::Int, 8};
		if (expr->intrinsicName == "return" && expr->arguments.size() >= 2)
			return getEffectiveType(context, expr->arguments[1]);
		if (expr->intrinsicName == "call") {
			// Format: @intrinsic("call", "library", "function", type_ref, args...)
			if (expr->arguments.size() >= 4) {
				DataType retTypeRef = getEffectiveType(context, expr->arguments[3]);
				if (retTypeRef.kind == DataType::Kind::Type)
					return retTypeRef.toReferencedType();
			}
			return {DataType::Kind::Int, 4};
		}
		if (expr->intrinsicName == "construct" || expr->intrinsicName == "property")
			return expr->type; // DataType fully determined during inference
		if (expr->intrinsicName == "cast" && expr->arguments.size() >= 3) {
			if (expr->type.kind == DataType::Kind::Class)
				return expr->type;
			DataType typeArgType = getEffectiveType(context, expr->arguments[2]);
			if (typeArgType.kind == DataType::Kind::Type)
				return typeArgType.toReferencedType();
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
llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, DataType type) {
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
		DataType varType = {DataType::Kind::Float, 8}; // fallback
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

				// Emit debug info for global variable
				if (context.diBuilder && varDef->range.line) {
					llvm::DIFile *diFile = getOrCreateDIFile(context, varDef->range.line->sourceFile);
					unsigned line = varDef->range.line->sourceFileLineIndex + 1;
					llvm::DIType *diType = getDIType(context, varType);
					auto *gvExpr = context.diBuilder->createExpression();
					context.diBuilder->createGlobalVariableExpression(
						context.diCompileUnit, name, name, diFile, line, diType, /*IsLocalToUnit=*/true, gvExpr
					);
				}
			}
		} else {
			// Local variable - create alloca as before
			varDef->alloca = createEntryAlloca(context, name, varType);

			// Emit debug info for local variable
			if (context.diBuilder && varDef->range.line && context.currentDebugScope) {
				llvm::DIFile *diFile = getOrCreateDIFile(context, varDef->range.line->sourceFile);
				unsigned line = varDef->range.line->sourceFileLineIndex + 1;
				llvm::DIType *diType = getDIType(context, varType);
				if (diType) {
					auto *diVar = context.diBuilder->createAutoVariable(context.currentDebugScope, name, diFile, line, diType);
					context.diBuilder->insertDeclare(
						varDef->alloca, diVar, context.diBuilder->createExpression(),
						llvm::DILocation::get(*context.llvmContext, line, varDef->range.start() + 1, context.currentDebugScope),
						static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder).GetInsertBlock()
					);
				}
			}
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
		Expression *resolved = resolveVariableBinding(context, expr);
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
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, DataType fromType, DataType toType) {
	if (fromType == toType || !val)
		return val;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *targetLLVM = toType.toLLVM(*context.llvmContext);

	// Pointer ↔ Integer conversions (check first, before kind-based checks)
	if (fromType.isPointer() && toType.kind == DataType::Kind::Int)
		return builder.CreatePtrToInt(val, targetLLVM, "ptoi");
	if (fromType.kind == DataType::Kind::Int && toType.isPointer())
		return builder.CreateIntToPtr(val, targetLLVM, "itop");

	// Numeric conversions
	if (fromType.isNumeric() && toType.isNumeric()) {
		if (fromType.kind == DataType::Kind::Int && toType.kind == DataType::Kind::Int) {
			if (fromType.numericSize < toType.numericSize)
				return builder.CreateSExt(val, targetLLVM, "sext");
			return builder.CreateTrunc(val, targetLLVM, "trunc");
		}
		if (fromType.kind == DataType::Kind::Float && toType.kind == DataType::Kind::Float) {
			if (fromType.numericSize < toType.numericSize)
				return builder.CreateFPExt(val, targetLLVM, "fpext");
			return builder.CreateFPTrunc(val, targetLLVM, "fptrunc");
		}
		if (fromType.kind == DataType::Kind::Int && toType.kind == DataType::Kind::Float)
			return builder.CreateSIToFP(val, targetLLVM, "itof");
		return builder.CreateFPToSI(val, targetLLVM, "ftoi");
	}

	// Bool → Numeric
	if (fromType.kind == DataType::Kind::Bool && toType.isNumeric()) {
		if (toType.kind == DataType::Kind::Float) {
			llvm::Value *intVal = builder.CreateZExt(val, builder.getInt64Ty(), "btoi");
			return builder.CreateSIToFP(intVal, targetLLVM, "itof");
		}
		return builder.CreateZExt(val, targetLLVM, "btoi");
	}

	// Unsupported conversion - this should not happen if type inference is correct
	assert(false && "Unsupported type conversion in ensureType");
	return val;
}
