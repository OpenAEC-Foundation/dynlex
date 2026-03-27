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
#include <unordered_map>

// Get the LLVM type for a given DataType
llvm::Type *getLLVMType(ParseContext &context, DataType type) { return type.toLLVM(*context.llvmContext); }

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

static DataType resolveBuiltInPropertyType(const DataType &ownerType, const std::string &fieldName) {
	if (fieldName == "data" && ownerType.isBytePointer())
		return ownerType;
	return {};
}

static bool instantiateClassFromCodegenArgumentTypes(
	ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef, int baseClassInstIndex
) {
	if (!classDef || classDef->fields.size() != argumentTypes.size())
		return false;

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(argumentTypes.size());
	for (size_t i = 0; i < argumentTypes.size(); i++) {
		DataType argumentType = concretizeClassType(argumentTypes[i]);
		if (!argumentType.isDeduced())
			return false;
		DataType fieldType = classDef->fields[i].declaredType;
		if (baseClassInstIndex >= 0 && static_cast<size_t>(baseClassInstIndex) < classDef->instantiations.size() &&
			i < classDef->instantiations[baseClassInstIndex].fieldTypes.size()) {
			fieldType = classDef->instantiations[baseClassInstIndex].fieldTypes[i];
		}
		if (fieldType.kind == DataType::Kind::Any) {
			fieldType = argumentType;
		} else if (fieldType.kind == DataType::Kind::Array && fieldType.arraySize >= 0 && !fieldType.arrayElementType) {
			if (argumentType.kind != DataType::Kind::Array || argumentType.arraySize != fieldType.arraySize)
				return false;
			fieldType = argumentType;
		} else if (fieldType.kind == DataType::Kind::Class && fieldType.classInstIndex < 0) {
			fieldType = concretizeClassType(fieldType);
		}

		if (!DataType::supportsRuntimeConversion(argumentType, fieldType))
			return false;

		if (!fieldType.isDeduced())
			return false;
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	outTypeRef = {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
	return true;
}

static DataType resolveConstructResultType(ParseContext &context, Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::IntrinsicCall || expr->arguments.size() < 2)
		return {};

	DataType typeRefType = getEffectiveType(context, expr->arguments[1]);
	if (typeRefType.kind != DataType::Kind::Type)
		return {};

	if (typeRefType.referencedKind == DataType::Kind::Array) {
		DataType arrayType = typeRefType.toReferencedType();
		if (arrayType.arraySize != static_cast<int>(expr->arguments.size()) - 2)
			return {};

		DataType elementType = arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
		bool allDeduced = true;
		for (size_t i = 2; i < expr->arguments.size(); i++) {
			DataType argType = getEffectiveType(context, expr->arguments[i]);
			if (!argType.isDeduced())
				allDeduced = false;
			if (!arrayType.arrayElementType) {
				if (!elementType.isDeduced())
					elementType = argType;
				else if (elementType != argType)
					allDeduced = false;
			}
		}
		if (allDeduced && elementType.isDeduced()) {
			arrayType.arrayElementType = std::make_shared<DataType>(elementType);
			return arrayType;
		}
		return {};
	}

	if (typeRefType.referencedKind == DataType::Kind::Vector) {
		DataType vectorType = typeRefType.toReferencedType();
		if (vectorType.arraySize != static_cast<int>(expr->arguments.size()) - 2)
			return {};
		for (size_t i = 2; i < expr->arguments.size(); i++) {
			DataType argType = getEffectiveType(context, expr->arguments[i]);
			if (!argType.isDeduced())
				return {};
			DataType promoted;
			if (!DataType::promoteArithmetic(argType, *vectorType.arrayElementType, promoted) ||
				promoted != *vectorType.arrayElementType)
				return {};
		}
		return vectorType;
	}

	if (typeRefType.referencedKind == DataType::Kind::Matrix) {
		DataType matrixType = typeRefType.toReferencedType();
		if (expr->arguments.size() == 3) {
			DataType valueType = getEffectiveType(context, expr->arguments[2]);
			if (valueType.kind == DataType::Kind::Array && valueType.arrayElementType &&
				valueType.arraySize == matrixType.matrixRows() * matrixType.matrixColumns()) {
				DataType promoted;
				if (DataType::promoteArithmetic(*valueType.arrayElementType, matrixType.matrixElementType(), promoted) &&
					promoted == matrixType.matrixElementType())
					return matrixType;
			}
		}
		return {};
	}

	if (typeRefType.classDefinition) {
		std::vector<DataType> argumentTypes;
		argumentTypes.reserve(expr->arguments.size() - 2);
		for (size_t i = 2; i < expr->arguments.size(); i++) {
			DataType argumentType = getEffectiveType(context, expr->arguments[i]);
			if (!argumentType.isDeduced())
				return {};
			argumentTypes.push_back(argumentType);
		}

		DataType instantiatedTypeRef;
		if (instantiateClassFromCodegenArgumentTypes(
				typeRefType.classDefinition, argumentTypes, instantiatedTypeRef, typeRefType.classInstIndex
			))
			return concretizeClassType(instantiatedTypeRef.toReferencedType());

		DataType targetType = concretizeClassType(typeRefType.toReferencedType());
		if (expr->arguments.size() == targetType.classDefinition->fields.size() + 2 && targetType.classInstIndex >= 0) {
			const auto &fieldTypes = targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
			if (argumentTypes.size() != fieldTypes.size())
				return {};
			for (size_t i = 0; i < fieldTypes.size(); i++) {
				if (!DataType::supportsRuntimeConversion(concretizeClassType(argumentTypes[i]), fieldTypes[i]))
					return {};
			}
			return targetType;
		}
		return {};
	}

	if (expr->arguments.size() == 3) {
		DataType targetType = typeRefType.toReferencedType();
		DataType valueType = getEffectiveType(context, expr->arguments[2]);
		if (valueType.isDeduced())
			return concretizeClassType(targetType);
	}

	return {};
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
	case DataType::Kind::Array: {
		if (!type.arrayElementType)
			return nullptr;
		llvm::DIType *elementType = getDIType(context, *type.arrayElementType);
		if (!elementType)
			return nullptr;
		auto subscripts = context.diBuilder->getOrCreateArray({context.diBuilder->getOrCreateSubrange(0, type.arraySize)});
		return context.diBuilder->createArrayType(static_cast<uint64_t>(type.getByteSize()) * 8, 0, elementType, subscripts);
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

static void ensureMacroBindingRootFrame(ParseContext &context) {
	if (context.macroBindingFrames.empty())
		context.macroBindingFrames.pushFrame({});
}

// Resolve a Variable expression one step through the current macro's binding map.
// Returns the bound expression (which lives in the caller's scope), or expr unchanged
// if no binding exists. Each resolution crosses one scope boundary — the caller must
// pop the binding stack before evaluating the result (see MacroScopeGuard::popToCallerScope).
static Expression *materializeCodegenCompileTimeLiteral(ParseContext &context, const CompileTimeValue &value) {
	Expression *literal = new Expression();
	if (const auto *number = std::get_if<double>(&value)) {
		literal->kind = Expression::Kind::Literal;
		literal->literalValue = *number;
	} else if (const auto *text = std::get_if<std::string>(&value)) {
		literal->kind = Expression::Kind::Literal;
		literal->literalValue = *text;
	} else if (const auto *boolean = std::get_if<bool>(&value)) {
		literal->kind = Expression::Kind::Literal;
		literal->literalValue = *boolean ? 1.0 : 0.0;
	} else if (const auto *typeRef = std::get_if<DataType>(&value)) {
		literal->kind = Expression::Kind::TypedPlaceholder;
		literal->type = *typeRef;
	} else {
		delete literal;
		return nullptr;
	}
	context.ownedCodegenLiteralRoots.push_back(literal);
	return literal;
}

Expression *resolveVariableBinding(ParseContext &context, Expression *expr) {
	if (expr && expr->kind == Expression::Kind::Variable && expr->variable && context.currentCodegenInstantiation) {
		const std::string &name = expr->variable->name;
		if (context.currentCodegenInstantiation->requiredCompileTimeParameters.contains(name)) {
			auto constIt = context.currentCodegenInstantiation->constantParameterValues.find(name);
			if (constIt != context.currentCodegenInstantiation->constantParameterValues.end()) {
				if (Expression *literal = materializeCodegenCompileTimeLiteral(context, constIt->second))
					return literal;
			}
		}
	}
	ensureMacroBindingRootFrame(context);
	return resolveVariableBindingAcrossFrames(expr, context.macroBindingFrames);
}

// Resolve an expression through all macro layers: variable bindings (which cross
// scope boundaries upward) and macro PatternCall expansions (which push new scopes
// downward). Variable bindings don't modify the stack; PatternCall expansions push
// one scope each. Returns the number of scopes pushed, so the caller can pop them
// when done. Use this when you need to see through macro indirection to inspect the
// underlying expression kind (e.g., detecting a property intrinsic inside a store).
void resolveThroughMacroLayers(ParseContext &context, Expression *&expr) {
	ensureMacroBindingRootFrame(context);
	resolveThroughBindingLayers(expr, context.macroBindingFrames, [&](Expression *expression, BindingMap &innerBindings) {
		return expandMacroPatternCall(context, expression, innerBindings);
	});
}

// MacroScopeGuard implementation
void MacroScopeGuard::popToCallerScope() {
	ensureMacroBindingRootFrame(context);
	assert(context.macroBindingFrames.hasParentScope());
	savedBindingFrames = context.macroBindingFrames;
	popBindingScopeOrFail(context.macroBindingFrames, "Missing macro binding scope for MacroScopeGuard");
	active = true;
}

MacroScopeGuard::~MacroScopeGuard() {
	if (active)
		context.macroBindingFrames = savedBindingFrames;
}

// Resolve the effective type of an expression during codegen.
// Follows macro expression bindings and pattern parameter types to compute the real type,
// even for expressions inside non-macro function bodies whose .type was never inferred.
DataType getEffectiveType(ParseContext &context, Expression *expr) {
	if (!expr)
		return {};

	switch (expr->kind) {
	case Expression::Kind::Literal:
		if (expr->type.isDeduced())
			return expr->type;
		if (std::holds_alternative<double>(expr->literalValue)) {
			double value = std::get<double>(expr->literalValue);
			std::string_view literalText = expr->range.subString;
			bool explicitlyFloat = literalText.find('.') != std::string_view::npos ||
								   literalText.find('e') != std::string_view::npos ||
								   literalText.find('E') != std::string_view::npos;
			if (!explicitlyFloat && std::trunc(value) == value)
				return {DataType::Kind::Int, 4};
			return {DataType::Kind::Float, context.options.emitSPIRV ? 4 : 8};
		}
		if (std::holds_alternative<std::string>(expr->literalValue)) {
			DataType stringType{DataType::Kind::Int, 1};
			stringType.pointerDepth = 1;
			return stringType;
		}
		return expr->type;

	case Expression::Kind::ArrayLiteral:
		return expr->type;

	case Expression::Kind::Variable: {
		Expression *resolved = resolveVariableBinding(context, expr);
		if (resolved != expr) {
			MacroScopeGuard guard(context);
			if (context.macroBindingFrames.hasParentScope())
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
				if (expr->arguments.size() == 2) {
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
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		if (kind == IntrinsicKind::AddressOf)
			return getEffectiveType(context, expr->arguments[1]).pointed();
		if (kind == IntrinsicKind::Dereference)
			return concretizeClassType(getEffectiveType(context, expr->arguments[1]).dereferenced());
		if (kind == IntrinsicKind::LoadAt) {
			DataType ptrType = getEffectiveType(context, expr->arguments[1]);
			if (ptrType.isPointer()) {
				DataType pointedType = ptrType.dereferenced();
				if (pointedType.kind == DataType::Kind::Array && pointedType.arrayElementType)
					return *pointedType.arrayElementType;
				return pointedType;
			}
			return {DataType::Kind::Int, 8};
		}
		if (kind == IntrinsicKind::Return && expr->arguments.size() > 1) {
			return getEffectiveType(context, expr->arguments[1]);
		}
		if (kind == IntrinsicKind::Call) {
			// Format: @intrinsic("call", "library", "function", type_ref, args...)
			DataType retTypeRef = getEffectiveType(context, expr->arguments[3]);
			assert(retTypeRef.kind == DataType::Kind::Type && "call return type must be a compile-time type reference");
			assert(
				retTypeRef.referencedKind != DataType::Kind::Type && retTypeRef.referencedKind != DataType::Kind::Unresolved &&
				"call return type must resolve to a concrete runtime type"
			);
			return retTypeRef.toReferencedType();
		}
		if (kind == IntrinsicKind::Select && expr->arguments.size() > 3) {
			CompileTimeValue conditionValue = evaluateCompileTimeValue(
				expr->arguments[1], context, context.macroBindingFrames, context.currentCodegenInstantiation
			);
			std::optional<bool> condition = compileTimeTruthiness(conditionValue);
			if (condition.has_value())
				return getEffectiveType(context, expr->arguments[*condition ? 2 : 3]);
			DataType trueType = getEffectiveType(context, expr->arguments[2]);
			DataType falseType = getEffectiveType(context, expr->arguments[3]);
			if (trueType.isDeduced() && falseType.isDeduced() && trueType == falseType)
				return trueType;
			return expr->type;
		}
		if (kind == IntrinsicKind::Function)
			return {DataType::Kind::Int, 1, 1};
		if (kind == IntrinsicKind::Construct) {
			if (expr->type.isDeduced())
				return concretizeClassType(expr->type);
			return resolveConstructResultType(context, expr);
		}
		if (kind == IntrinsicKind::Type) {
			Expression *kindExpr = resolveVariableBinding(context, expr->arguments[1]);
			if (auto *kindStr = std::get_if<std::string>(&kindExpr->literalValue)) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				if (*kindStr == "int") {
					typeRef.referencedKind = DataType::Kind::Int;
					typeRef.numericSize = 4;
				} else if (*kindStr == "float") {
					typeRef.referencedKind = DataType::Kind::Float;
					typeRef.numericSize = 8;
				} else if (*kindStr == "bool") {
					typeRef.referencedKind = DataType::Kind::Bool;
				} else if (*kindStr == "void") {
					typeRef.referencedKind = DataType::Kind::Void;
				} else if (*kindStr == "string") {
					typeRef.referencedKind = DataType::Kind::Int;
					typeRef.numericSize = 1;
					typeRef.pointerDepth = 1;
				} else if (*kindStr == "type") {
					typeRef.referencedKind = DataType::Kind::Type;
				} else {
					return expr->type;
				}
				if (expr->arguments.size() > 2) {
					Expression *bitsExpr = resolveVariableBinding(context, expr->arguments[2]);
					if (auto *bits = std::get_if<double>(&bitsExpr->literalValue))
						typeRef.numericSize = static_cast<int>(*bits) / 8;
				}
				return typeRef;
			}
		}
		if (kind == IntrinsicKind::AddPointerDepth) {
			DataType innerType = getEffectiveType(context, expr->arguments[1]);
			if (innerType.kind == DataType::Kind::Type) {
				innerType.pointerDepth++;
				return innerType;
			}
		}
		if (kind == IntrinsicKind::SizeOf)
			return {DataType::Kind::Int, 8};
		if (kind == IntrinsicKind::TypeOf) {
			DataType valueType = getEffectiveType(context, expr->arguments[1]);
			if (valueType.isDeduced()) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				typeRef.referencedKind = valueType.kind;
				typeRef.numericSize = valueType.numericSize;
				typeRef.pointerDepth = valueType.pointerDepth;
				typeRef.classDefinition = valueType.classDefinition;
				typeRef.classInstIndex = valueType.classInstIndex;
				typeRef.arraySize = valueType.arraySize;
				typeRef.matrixRowCount = valueType.matrixRowCount;
				typeRef.arrayElementType =
					valueType.arrayElementType ? std::make_shared<DataType>(*valueType.arrayElementType) : nullptr;
				return typeRef;
			}
		}
		if (kind == IntrinsicKind::Array)
			return expr->type;
		if ((kind == IntrinsicKind::Vector || kind == IntrinsicKind::Matrix) && expr->type.kind == DataType::Kind::Type)
			return expr->type;
		if (kind == IntrinsicKind::Property) {
			DataType ownerType = getEffectiveType(context, expr->arguments[1]);
			std::string fieldName = getStringLiteral(resolveVariableBinding(context, expr->arguments[2]));
			DataType builtInPropertyType = resolveBuiltInPropertyType(ownerType, fieldName);
			if (builtInPropertyType.isDeduced())
				return builtInPropertyType;
			return expr->type; // Class property type determined during inference
		}
		if (kind == IntrinsicKind::Cast) {
			if (expr->type.kind == DataType::Kind::Class)
				return concretizeClassType(expr->type);
			DataType typeArgType = getEffectiveType(context, expr->arguments[2]);
			if (typeArgType.kind == DataType::Kind::Type)
				return concretizeClassType(typeArgType.toReferencedType());
		}
		return expr->type;
	}

	case Expression::Kind::PatternCall: {
		if (expr->type.isDeduced())
			return concretizeClassType(expr->type);
		if (!expr->patternMatch || !expr->patternMatch->matchedEndNode)
			return expr->type;

		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
		if (defs.empty())
			return expr->type;

		PatternDefinition *matchedDef = selectCodegenOverload(context, expr);
		assert(matchedDef && "Pattern call missing overload selection from type inference");
		assert(std::find(defs.begin(), defs.end(), matchedDef) != defs.end() && "Selected overload no longer matches call");
		assert(std::find(defs.begin(), defs.end(), matchedDef) != defs.end() && "Selected overload no longer matches call");
		assert(matchedDef && "No overload matched during codegen");
		assert(matchedDef->section && "Selected overload has no section");

		Section *matchedSection = matchedDef->section;
		if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
			auto *classSec = static_cast<ClassSection *>(matchedSection);
			return {DataType::Kind::Type, 0, 0, classSec->classDefinition, -1, nullptr, DataType::Kind::Class};
		}

		if (matchedSection->isMacro) {
			BindingMap innerBindings;
			Expression *bodyExpr = expandMacroPatternCall(context, expr, innerBindings);
			if (!bodyExpr)
				return expr->type;

			pushBindingScope(context.macroBindingFrames, std::move(innerBindings));
			DataType result = getEffectiveType(context, bodyExpr);
			popBindingScopeOrFail(
				context.macroBindingFrames, "Missing macro binding scope while restoring effective-type context"
			);
			return concretizeClassType(result);
		}

		std::vector<DataType> argTypes;
		std::vector<std::pair<std::string, Expression *>> orderedBindings;
		collectPatternCallBindingPairs(expr, matchedDef, orderedBindings);
		for (const auto &[ignoredParameterName, argumentExpression] : orderedBindings) {
			(void)ignoredParameterName;
			DataType argType = getEffectiveType(context, argumentExpression);
			if (!argType.isDeduced())
				return expr->type;
			argTypes.push_back(argType);
		}

		auto evaluateParameterValue = [&](Expression *argumentExpression) {
			return argumentExpression
					   ? evaluateCompileTimeValue(
							 argumentExpression, context, context.macroBindingFrames, context.currentCodegenInstantiation
						 )
					   : CompileTimeValue{};
		};
		auto instKey = findMatchingInstantiationKey(matchedSection, orderedBindings, argTypes, evaluateParameterValue);
		auto instIt = instKey ? matchedSection->instantiations.find(*instKey) : matchedSection->instantiations.end();
		if (instIt != matchedSection->instantiations.end() && instIt->second.returnType.isDeduced())
			return concretizeClassType(instIt->second.returnType);
		assert(
			instIt != matchedSection->instantiations.end() &&
			"Missing inferred instantiation for deduced non-macro pattern call in getEffectiveType"
		);
		return expr->type;
	}

	default:
		return expr->type;
	}
}

PatternDefinition *selectCodegenOverload(ParseContext &context, Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;

	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return nullptr;
	if (defs.size() == 1)
		return defs.front();

	std::vector<DataType> argTypes;
	argTypes.reserve(expr->arguments.size());
	bool allArgumentTypesDeduced = true;
	for (Expression *arg : expr->arguments) {
		DataType argType = getEffectiveType(context, arg);
		argTypes.push_back(argType);
		if (!argType.isDeduced())
			allArgumentTypesDeduced = false;
	}
	if (allArgumentTypesDeduced) {
		PatternDefinition *matchedDef = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypes);
		if (matchedDef)
			return matchedDef;
	}

	if (context.currentCodegenInstantiation) {
		auto selectedIt = context.currentCodegenInstantiation->selectedOverloadsByCall.find(expr);
		if (selectedIt != context.currentCodegenInstantiation->selectedOverloadsByCall.end() &&
			std::find(defs.begin(), defs.end(), selectedIt->second) != defs.end()) {
			return selectedIt->second;
		}
	}

	if (expr->selectedPatternDefinition && std::find(defs.begin(), defs.end(), expr->selectedPatternDefinition) != defs.end())
		return expr->selectedPatternDefinition;

	return nullptr;
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
		Variable *var = section->findVariable(name);
		assert(var && "Internal compiler error: variableDefinitions contains a name missing from section variable metadata");
		DataType varType = var->type;
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
	ensureMacroBindingRootFrame(context);
	BindingScopeTrail scopeTrail;
	// Resolve through macro binding layers and keep a trail so we can restore
	// the exact stack state before returning to the caller.
	expr = resolveVariableBindingAcrossScopes(expr, context.macroBindingFrames, &scopeTrail);

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
	restoreBindingScopes(context.macroBindingFrames, scopeTrail);

	return result;
}

// Ensure a value has the target LLVM type by inserting conversions if needed
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, DataType fromType, DataType toType) {
	if (fromType == toType || !val)
		return val;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Type *targetLLVM = toType.toLLVM(*context.llvmContext);

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
	assert(false && "Unsupported type conversion in ensureType");
	return val;
}
