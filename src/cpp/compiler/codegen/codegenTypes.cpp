#include "bindingResolution.h"
#include "classDefinition.h"
#include "classSection.h"
#include "codegenInternal.h"
#include "compileTimeValue.h"
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
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <tuple>
#include <unordered_map>

// Get the LLVM type for a given DataType
llvm::Type *getLLVMType(ParseContext &context, DataType type) {
	return type.toLLVM(*context.llvmContext, context.llvmModule->getDataLayout());
}

llvm::Align getLLVMABIAlignment(ParseContext &context, DataType type) {
	return llvm::Align(type.getABIAlignment(context.llvmModule->getDataLayout(), *context.llvmContext));
}

unsigned getClassFieldLLVMIndex(ParseContext &context, const DataType &classType, int fieldIndex) {
	requireCompilerInvariant(
		classType.kind == DataType::Kind::Class && classType.classDefinition && classType.classInstIndex >= 0,
		"class field access requires a concrete class type"
	);
	requireCompilerInvariant(
		classType.classInstIndex < static_cast<int>(classType.classDefinition->instantiations.size()),
		"class field access references a missing instantiation"
	);
	(void)getLLVMType(context, classType);
	const ClassInstantiation &instantiation = classType.classDefinition->instantiations[classType.classInstIndex];
	requireCompilerInvariant(
		fieldIndex >= 0 && fieldIndex < static_cast<int>(instantiation.fieldTypes.size()),
		"class field access references a missing field"
	);
	requireCompilerInvariant(
		instantiation.llvmFieldIndices.size() == instantiation.fieldTypes.size(),
		"class layout is missing logical-to-LLVM field indices"
	);
	return instantiation.llvmFieldIndices[fieldIndex];
}

llvm::Value *getVectorLaneIndexValue(ParseContext &context, unsigned index) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	return builder.getInt32(index);
}

static DataType concretizeClassType(DataType type) {
	if (type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex < 0 &&
		!type.classDefinition->instantiations.empty()) {
		type.classInstIndex = 0;
	}
	return type;
}

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
		return context.diBuilder->createPointerType(
			getDIType(context, inner), context.llvmModule->getDataLayout().getPointerSizeInBits()
		);
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
	case DataType::Kind::Array: {
		if (!type.arrayElementType)
			return nullptr;
		llvm::DIType *elementType = getDIType(context, *type.arrayElementType);
		if (!elementType)
			return nullptr;
		auto subscripts = context.diBuilder->getOrCreateArray({context.diBuilder->getOrCreateSubrange(0, type.arraySize)});
		const llvm::DataLayout &dataLayout = context.llvmModule->getDataLayout();
		return context.diBuilder->createArrayType(
			type.getByteSize(dataLayout, *context.llvmContext) * 8, type.getABIAlignment(dataLayout, *context.llvmContext) * 8,
			elementType, subscripts
		);
	}
	case DataType::Kind::Vector:
	case DataType::Kind::Matrix:
		return nullptr;
	case DataType::Kind::Class: {
		if (!type.classDefinition || type.classInstIndex < 0)
			return nullptr;
		ClassInstantiation &inst = type.classDefinition->instantiations[type.classInstIndex];
		auto &fields = type.classDefinition->fields;
		llvm::DIFile *file = nullptr;
		if (!type.classDefinition->range.line)
			return nullptr;
		file = getOrCreateDIFile(context, type.classDefinition->range.line->sourceFile);

		llvm::StructType *llvmStruct = llvm::cast<llvm::StructType>(getLLVMType(context, type));
		const llvm::DataLayout &dataLayout = context.llvmModule->getDataLayout();
		const llvm::StructLayout *structLayout = dataLayout.getStructLayout(llvmStruct);
		std::vector<llvm::Metadata *> members;
		for (size_t i = 0; i < fields.size() && i < inst.fieldTypes.size(); i++) {
			llvm::DIType *fieldDIType = getDIType(context, inst.fieldTypes[i]);
			llvm::Type *llvmFieldType = getLLVMType(context, inst.fieldTypes[i]);
			llvm::TypeSize fieldSize = dataLayout.getTypeSizeInBits(llvmFieldType);
			requireCompilerInvariant(!fieldSize.isScalable(), "debug metadata requires a fixed-size class field");
			uint64_t fieldSizeBits = fieldSize.getFixedValue();
			uint64_t fieldAlignmentBits =
				std::max<uint64_t>(inst.fieldTypes[i].getABIAlignment(dataLayout, *context.llvmContext), fields[i].alignment) *
				8;
			uint64_t offsetBits = structLayout->getElementOffset(inst.llvmFieldIndices[i]) * 8;
			auto *member = context.diBuilder->createMemberType(
				nullptr, fields[i].name, file, 0, fieldSizeBits, fieldAlignmentBits, offsetBits, llvm::DINode::FlagZero,
				fieldDIType
			);
			members.push_back(member);
		}
		std::string className = type.classDefinition->patternNames.empty() ? "class" : type.classDefinition->patternNames[0];
		return context.diBuilder->createStructType(
			nullptr, className, file, 0, structLayout->getSizeInBytes() * 8,
			type.getABIAlignment(dataLayout, *context.llvmContext) * 8, llvm::DINode::FlagZero, nullptr,
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
		llvm::Type *floatTy = getLLVMType(context, condType);
		return builder.CreateFCmpONE(condValue, llvm::ConstantFP::get(floatTy, 0.0), name);
	}
	llvm::Type *intTy = getLLVMType(context, condType);
	return builder.CreateICmpNE(condValue, llvm::ConstantInt::get(intTy, 0), name);
}

static void ensureFlexBindingRootFrame(ParseContext &context) {
	if (context.flexBindingFrames.empty())
		context.flexBindingFrames.pushFrame(BindingFrame{});
}

Expression *resolveVariableBinding(ParseContext &context, Expression *expr) {
	ensureFlexBindingRootFrame(context);
	return resolveVariableBindingAcrossFrames(expr, context.flexBindingFrames);
}

// Resolve an expression through all flex layers: variable bindings (which cross
// scope boundaries upward) and flex PatternCall expansions (which push new scopes
// downward). Variable bindings don't modify the stack; PatternCall expansions push
// one scope each. Returns the number of scopes pushed, so the caller can pop them
// when done. Use this when you need to see through flex indirection to inspect the
// underlying expression kind (e.g., detecting a property intrinsic inside a store).
void resolveThroughFlexLayers(ParseContext &context, Expression *&expr) {
	ensureFlexBindingRootFrame(context);
	resolveThroughBindingLayers(expr, context.flexBindingFrames, [&](Expression *expression, BindingFrame &innerBindings) {
		if (!expression || expression->kind != Expression::Kind::PatternCall)
			return static_cast<Expression *>(nullptr);
		PatternDefinition *definition = finalizedPatternDefinition(context, expression);
		if (!definition->section || !definition->section->isFlex)
			return static_cast<Expression *>(nullptr);
		requireCompilerInvariant(
			expression->inferredFlexExpansion, "codegen flex-layer resolution is missing the inferred expansion"
		);
		collectPatternCallBindings(expression, definition, innerBindings);
		return expression->inferredFlexExpansion;
	});
}

// FlexScopeGuard implementation
void FlexScopeGuard::popToCallerScope() {
	ensureFlexBindingRootFrame(context);
	requireCompilerInvariant(context.flexBindingFrames.hasParentScope(), "FlexScopeGuard requires a caller flex scope");
	savedBindingFrames = context.flexBindingFrames;
	popBindingScopeOrFail(context.flexBindingFrames, "Missing flex binding scope for FlexScopeGuard");
	active = true;
}

FlexScopeGuard::~FlexScopeGuard() {
	if (active)
		context.flexBindingFrames = savedBindingFrames;
}

DataType finalizedExpressionType(ParseContext &, Expression *expr) {
	requireCompilerInvariant(expr != nullptr, "codegen requested the type of a null expression");
	requireCompilerInvariant(expr->type.isDeduced(), "expression reached codegen without a finalized inferred type");
	return concretizeClassType(expr->type);
}

PatternDefinition *finalizedPatternDefinition(ParseContext &, Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;

	auto &defs = expr->patternMatch->matchingDefinitions;
	requireCompilerInvariant(expr->selectedPatternDefinition, "pattern call reached codegen without a finalized overload");
	requireCompilerInvariant(
		std::find(defs.begin(), defs.end(), expr->selectedPatternDefinition) != defs.end(),
		"finalized overload no longer belongs to the matched pattern endpoint"
	);
	return expr->selectedPatternDefinition;
}

// Create an alloca at function entry (avoids stack growth in loops)
llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, DataType type) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *func = builder.GetInsertBlock()->getParent();
	llvm::IRBuilder<> entryBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
	llvm::Type *llvmType = getLLVMType(context, type);
	llvm::AllocaInst *alloca = entryBuilder.CreateAlloca(llvmType, nullptr, name);
	alloca->setAlignment(getLLVMABIAlignment(context, type));
	return alloca;
}

// Generate a unique function name for a pattern
std::string getPatternFunctionName(Section *section) {
	requireCompilerInvariant(section != nullptr, "function naming requires a section");
	std::string name;
	if (!section->patternDefinitions.empty()) {
		name = std::string(section->patternDefinitions.front()->range.subString);
	} else if ((section->type == SectionType::Retain || section->type == SectionType::Release) && section->parent &&
			   !section->parent->patternDefinitions.empty()) {
		name = sectionTypeToString(section->type) + " " +
			   std::string(section->parent->patternDefinitions.front()->range.subString);
	} else {
		crashCompilerBug("generated function section has no pattern identity");
	}
	for (char &c : name) {
		if (!isalnum(c) && c != '_') {
			c = (c == ' ') ? '_' : (c % 10 + '0');
		}
	}
	return name;
}

static void
collectFinalizedVariableTypes(InstantiatedSectionBody *body, VariableReference *definition, std::optional<DataType> &type) {
	if (!body)
		return;
	std::function<void(Expression *)> visitExpression = [&](Expression *expression) {
		if (!expression)
			return;
		if (expression->kind == Expression::Kind::Variable && expression->variable &&
			normalizeBindingReference(expression->variable) == definition && expression->type.isDeduced()) {
			DataType expressionType = concretizeClassType(expression->type);
			if (type)
				requireCompilerInvariant(
					*type == expressionType, "variable has inconsistent finalized types in one instantiation"
				);
			else
				type = expressionType;
		}
		for (Expression *argument : expression->arguments)
			visitExpression(argument);
	};
	for (Expression *expression : body->lineExpressions)
		visitExpression(expression);
	for (const auto &child : body->childBodies)
		collectFinalizedVariableTypes(child.get(), definition, type);
}

// Allocate all variables for a section at its start from finalized inference metadata.
void allocateSectionVariables(ParseContext &context, Section *section, InstantiatedSectionBody *body) {
	std::vector<std::pair<std::string, VariableReference *>> definitions(
		section->variableDefinitions.begin(), section->variableDefinitions.end()
	);
	std::ranges::sort(definitions, [](const auto &left, const auto &right) {
		requireCompilerInvariant(left.second != nullptr, "section variable definition is null");
		requireCompilerInvariant(right.second != nullptr, "section variable definition is null");
		requireCompilerInvariant(left.second->range.line != nullptr, "section variable definition has no source line");
		requireCompilerInvariant(right.second->range.line != nullptr, "section variable definition has no source line");
		return std::tuple(left.second->range.line->mergedLineIndex, left.second->range.start(), left.first) <
			   std::tuple(right.second->range.line->mergedLineIndex, right.second->range.start(), right.first);
	});
	for (auto &[name, varDef] : definitions) {
		// Call-bound parameters already have storage behind their argument
		// pointer; variable resolution finds them there first. Parameters the
		// active match did not bind (their choice alternative was not taken)
		// fall through and get local storage like any other variable.
		if (context.patternBindings.contains(name) ||
			(context.currentCodegenInstantiation &&
			 context.currentCodegenInstantiation->requiredCompileTimeParameters.contains(name)))
			continue;
		Variable *var = section->findVariable(name);
		requireCompilerInvariant(var != nullptr, "variableDefinitions contains a name missing from section variable metadata");
		std::optional<DataType> finalizedType;
		if (body)
			collectFinalizedVariableTypes(body, normalizeBindingReference(varDef), finalizedType);
		DataType varType = finalizedType.value_or(var->type);
		// Compile-time-only parameters (fixed values, type and constraint
		// parameters) have no runtime representation to allocate.
		if (!varType.isRuntimeValueType())
			continue;

		// Check if this is a global variable
		if (var && var->isGlobal) {
			// Create or get existing global variable
			if (!context.globalLLVMVariables.contains(name)) {
				llvm::Type *llvmType = getLLVMType(context, varType);
				llvm::Constant *initializer = llvm::Constant::getNullValue(llvmType);
				auto *globalVar = new llvm::GlobalVariable(
					*context.llvmModule, llvmType, false, // not constant
					llvm::GlobalValue::InternalLinkage, initializer, name
				);
				globalVar->setAlignment(getLLVMABIAlignment(context, varType));
				context.globalLLVMVariables[name] = globalVar;
				// Store in alloca field so existing code can find it
				varDef->alloca = reinterpret_cast<llvm::AllocaInst *>(globalVar);
				if (typeHasManagedLifecycle(varType))
					registerManagedGlobalStorage(context, globalVar, varType);

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
			if (typeHasManagedLifecycle(varType))
				registerManagedStorage(context, varDef->alloca, varType, section);

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
// Recursively resolves through nested flex binding scopes to find the actual variable.
llvm::Value *getVariablePointer(ParseContext &context, Expression *expr) {
	ensureFlexBindingRootFrame(context);
	BindingScopeTrail scopeTrail;
	// Resolve through flex binding layers and keep a trail so we can restore
	// the exact stack state before returning to the caller.
	expr = resolveVariableBindingAcrossScopes(expr, context.flexBindingFrames, &scopeTrail);

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

	// Restore all popped scopes in reverse order.
	restoreBindingScopes(context.flexBindingFrames, scopeTrail);

	return result;
}

// Ensure a value has the target LLVM type by inserting conversions if needed
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, DataType fromType, DataType toType) {
	if (fromType == toType || !val)
		return val;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *targetLLVM = getLLVMType(context, toType);

	// Pointer ↔ Integer conversions (check first, before kind-based checks)
	if (fromType.isPointer() && toType.isPointer())
		return builder.CreateBitCast(val, targetLLVM, "ptop");
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

	// Numeric -> Bool
	if (fromType.isNumeric() && toType.kind == DataType::Kind::Bool)
		return convertConditionToBool(context, val, fromType, "tobool");

	if (toType.kind == DataType::Kind::Vector && fromType.isNumeric()) {
		llvm::Value *scalar = ensureType(context, val, fromType, toType.vectorElementType());
		llvm::Value *vectorValue = llvm::Constant::getNullValue(targetLLVM);
		for (int i = 0; i < toType.vectorSize(); i++)
			vectorValue = builder.CreateInsertElement(vectorValue, scalar, getVectorLaneIndexValue(context, i), "splat");
		return vectorValue;
	}

	if (fromType.kind == DataType::Kind::Vector && toType.kind == DataType::Kind::Vector &&
		fromType.vectorSize() == toType.vectorSize()) {
		llvm::Value *result = llvm::Constant::getNullValue(targetLLVM);
		for (int i = 0; i < toType.vectorSize(); i++) {
			llvm::Value *lane = builder.CreateExtractElement(val, getVectorLaneIndexValue(context, i), "vec_lane");
			lane = ensureType(context, lane, fromType.vectorElementType(), toType.vectorElementType());
			result = builder.CreateInsertElement(result, lane, getVectorLaneIndexValue(context, i), "vec_cast");
		}
		return result;
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
	crashCompilerBug("Unsupported type conversion in ensureType");
	return val;
}
