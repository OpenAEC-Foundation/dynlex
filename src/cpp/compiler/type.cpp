#include "type.h"
#include "classDefinition.h"
#include "classLayout.h"
#include "compilerUtils.h"
#include "typeConstraint.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/MathExtras.h"
#include <set>

namespace {

uint64_t fixedAllocationSize(const llvm::DataLayout &dataLayout, llvm::Type *type) {
	llvm::TypeSize allocationSize = dataLayout.getTypeAllocSize(type);
	requireCompilerInvariant(!allocationSize.isScalable(), "class layout requires fixed-size fields");
	return allocationSize.getFixedValue();
}

using ClassInstanceKey = std::pair<const ClassDefinition *, int>;

bool typeHasManagedLifecycle(const DataType &type, std::set<ClassInstanceKey> &visited) {
	if (type.kind == DataType::Kind::Array)
		return type.arrayElementType && typeHasManagedLifecycle(*type.arrayElementType, visited);
	if (type.kind != DataType::Kind::Class || type.isPointer())
		return false;
	requireCompilerInvariant(
		type.classDefinition && type.classInstIndex >= 0, "managed lifecycle requires a concrete class type"
	);
	const ClassDefinition &definition = *type.classDefinition;
	requireCompilerInvariant(
		type.classInstIndex < static_cast<int>(definition.instantiations.size()),
		"managed lifecycle references a missing class instantiation"
	);
	requireCompilerInvariant(
		static_cast<bool>(definition.retainSection) == static_cast<bool>(definition.releaseSection),
		"managed class has an incomplete lifecycle"
	);
	if (definition.retainSection)
		return true;
	if (!visited.insert({&definition, type.classInstIndex}).second)
		return false;
	for (const DataType &fieldType : definition.instantiations[type.classInstIndex].fieldTypes) {
		if (typeHasManagedLifecycle(fieldType, visited))
			return true;
	}
	return false;
}

std::string canonicalTypePatternName(std::string_view patternName) {
	std::string result;
	result.reserve(patternName.size());
	bool omittedParameter = false;
	for (size_t index = 0; index < patternName.size();) {
		if (patternName[index] == '{') {
			size_t closing = patternName.find('}', index + 1);
			if (closing == std::string_view::npos) {
				result.append(patternName.substr(index));
				break;
			}
			omittedParameter = true;
			result += ' ';
			index = closing + 1;
			continue;
		}
		if (patternName[index] != '[') {
			result += patternName[index++];
			continue;
		}
		size_t closing = patternName.find(']', index + 1);
		if (closing == std::string_view::npos) {
			result.append(patternName.substr(index));
			break;
		}
		std::string_view alternatives = patternName.substr(index + 1, closing - index - 1);
		size_t separator = alternatives.find('|');
		result.append(alternatives.substr(0, separator));
		index = closing + 1;
	}

	std::string normalized;
	normalized.reserve(result.size());
	bool pendingSpace = false;
	for (char character : result) {
		if (std::isspace(static_cast<unsigned char>(character))) {
			pendingSpace = !normalized.empty();
			continue;
		}
		if (pendingSpace) {
			normalized += ' ';
			pendingSpace = false;
		}
		normalized += character;
	}
	if (omittedParameter) {
		for (size_t position = normalized.find(" by "); position != std::string::npos; position = normalized.find(" by "))
			normalized.replace(position, 4, " ");
	}
	return normalized;
}

bool startsWithDeterminer(std::string_view value) {
	for (std::string_view determiner : {"a ", "an ", "any ", "one ", "some ", "the "}) {
		if (value.starts_with(determiner))
			return true;
	}
	return false;
}

std::string withIndefiniteArticle(std::string phrase) {
	if (phrase.empty())
		return "a class value";
	if (startsWithDeterminer(phrase))
		return phrase;
	char authoredInitial = phrase.front();
	char initial = static_cast<char>(std::tolower(static_cast<unsigned char>(authoredInitial)));
	bool initialism = std::isupper(static_cast<unsigned char>(authoredInitial));
	for (size_t index = 1; initialism && index < phrase.size() && !std::isspace(static_cast<unsigned char>(phrase[index]));
		 index++) {
		char character = phrase[index];
		if (std::isalpha(static_cast<unsigned char>(character)) && !std::isupper(static_cast<unsigned char>(character))) {
			initialism = false;
		}
	}
	constexpr std::string_view vowelSoundInitials = "AEFHILMNORSX";
	bool startsWithVowelSound = initialism
									? vowelSoundInitials.contains(authoredInitial)
									: initial == 'a' || initial == 'e' || initial == 'i' || initial == 'o' || initial == 'u';
	return std::string(startsWithVowelSound ? "an " : "a ") + phrase;
}

std::string scalarTypeName(DataType::Kind kind, int numericSize) {
	if (numericSize <= 0)
		return kind == DataType::Kind::Int ? "an integer" : "a floating-point number";
	int bitCount = numericSize * 8;
	if (kind == DataType::Kind::Int && bitCount == 8)
		return "a byte";
	std::string article = bitCount == 8 ? "an " : "a ";
	std::string category = kind == DataType::Kind::Int ? "integer" : "floating-point number";
	return article + std::to_string(bitCount) + "-bit " + category;
}

std::string aggregateItemCount(int count, std::string_view singular, std::string_view plural) {
	if (count == 1)
		return "one " + std::string(singular);
	return std::to_string(count) + " " + std::string(plural);
}

std::string classTypeName(const ClassDefinition *definition) {
	if (!definition)
		return "a class value";
	requireCompilerInvariant(!definition->displayPatternNames.empty(), "resolved class has no display pattern name");
	return withIndefiniteArticle(canonicalTypePatternName(definition->displayPatternNames.front()));
}

std::string qualifyCompileTimeValue(std::string phrase) {
	if (phrase.starts_with("an "))
		return "a compile-time-known " + phrase.substr(3);
	if (phrase.starts_with("a "))
		return "a compile-time-known " + phrase.substr(2);
	if (phrase == "nothing")
		return phrase;
	return "a compile-time-known value matching " + phrase;
}

} // namespace

std::string typeToUserName(const DataType &type) {
	DataType valueType = type;
	int pointerDepth = valueType.pointerDepth;
	valueType.pointerDepth = 0;

	std::string result;
	switch (valueType.kind) {
	case DataType::Kind::Any:
		result = "a value";
		break;
	case DataType::Kind::Unresolved:
		result = "an unresolved value";
		break;
	case DataType::Kind::Void:
		result = "nothing";
		break;
	case DataType::Kind::Bool:
		result = "a boolean";
		break;
	case DataType::Kind::Int:
	case DataType::Kind::Float:
		result = scalarTypeName(valueType.kind, valueType.numericSize);
		break;
	case DataType::Kind::Array:
		result = "a fixed array containing " + aggregateItemCount(valueType.arraySize, "item", "items");
		if (valueType.arrayElementType)
			result += " whose type is " + typeToUserName(*valueType.arrayElementType);
		break;
	case DataType::Kind::Vector:
		result = "a vector containing " + aggregateItemCount(valueType.arraySize, "value", "values");
		if (valueType.arrayElementType)
			result += " whose type is " + typeToUserName(*valueType.arrayElementType);
		break;
	case DataType::Kind::Matrix:
		result = "a " + std::to_string(valueType.matrixRowCount) + "-by-" + std::to_string(valueType.arraySize) + " matrix";
		if (valueType.arrayElementType)
			result += " whose element type is " + typeToUserName(*valueType.arrayElementType);
		break;
	case DataType::Kind::Class:
		result = classTypeName(valueType.classDefinition);
		break;
	case DataType::Kind::Type:
		result = "a type";
		break;
	case DataType::Kind::Constraint:
		result = "a type constraint";
		break;
	}

	for (int level = 0; level < pointerDepth; level++)
		result = "a pointer to " + result;
	return result;
}

std::string typeToUserName(const TypeConstraint &constraint) {
	if (!constraint.isResolved())
		return "an unresolved type constraint";

	std::string result;
	if (!constraint.kind) {
		if (constraint.requiresNumeric)
			result = "a number";
		else if (constraint.numericSize) {
			int bitCount = *constraint.numericSize * 8;
			result = std::string(bitCount == 8 ? "an " : "a ") + std::to_string(bitCount) + "-bit value";
		} else if (constraint.constrainsClassDefinition)
			result = classTypeName(constraint.classDefinition);
		else
			result = "a value";
	} else {
		switch (*constraint.kind) {
		case DataType::Kind::Any:
			result = "a value";
			break;
		case DataType::Kind::Unresolved:
			result = "an unresolved value";
			break;
		case DataType::Kind::Void:
			result = "nothing";
			break;
		case DataType::Kind::Bool:
			result = "a boolean";
			break;
		case DataType::Kind::Int:
		case DataType::Kind::Float:
			result = scalarTypeName(*constraint.kind, constraint.numericSize.value_or(0));
			break;
		case DataType::Kind::Array:
			result = constraint.arraySize
						 ? "a fixed array containing " + aggregateItemCount(*constraint.arraySize, "item", "items")
						 : "a fixed array";
			if (constraint.elementConstraint)
				result += " whose item type is " + typeToUserName(*constraint.elementConstraint);
			break;
		case DataType::Kind::Vector:
			result = constraint.arraySize
						 ? "a vector containing " + aggregateItemCount(*constraint.arraySize, "value", "values")
						 : "a vector";
			if (constraint.elementConstraint)
				result += " whose value type is " + typeToUserName(*constraint.elementConstraint);
			break;
		case DataType::Kind::Matrix:
			if (constraint.matrixRows && constraint.matrixColumns) {
				result = "a " + std::to_string(*constraint.matrixRows) + "-by-" + std::to_string(*constraint.matrixColumns) +
						 " matrix";
			} else {
				result = "a matrix";
			}
			if (constraint.elementConstraint)
				result += " whose element type is " + typeToUserName(*constraint.elementConstraint);
			break;
		case DataType::Kind::Class:
			result = classTypeName(constraint.classDefinition);
			break;
		case DataType::Kind::Type:
			result = "a type";
			break;
		case DataType::Kind::Constraint:
			result = "a type constraint";
			break;
		}
	}

	for (int level = 0; level < constraint.pointerDepth.value_or(0); level++)
		result = "a pointer to " + result;
	if (constraint.requiresCompileTimeValue)
		result = qualifyCompileTimeValue(std::move(result));
	return result;
}

std::string DataType::toString() const {
	std::string result;
	switch (kind) {
	case Kind::Void:
		result += "void";
		break;
	case Kind::Bool:
		result += "bool";
		break;
	case Kind::Int:
		result += "i" + std::to_string(numericSize * 8);
		break;
	case Kind::Float:
		result += "f" + std::to_string(numericSize * 8);
		break;
	case Kind::Array:
		result += "array[" + std::to_string(arraySize) + "]";
		if (arrayElementType)
			result += " of " + arrayElementType->toString();
		break;
	case Kind::Vector:
		result += "vector[" + std::to_string(arraySize) + "]";
		if (arrayElementType)
			result += " of " + arrayElementType->toString();
		break;
	case Kind::Matrix:
		result += "matrix[" + std::to_string(matrixRowCount) + "x" + std::to_string(arraySize) + "]";
		if (arrayElementType)
			result += " of " + arrayElementType->toString();
		break;
	case Kind::Class:
		result += (classDefinition && !classDefinition->patternNames.empty()) ? classDefinition->patternNames[0] : "class";
		break;
	case Kind::Type:
		result += "type";
		break;
	case Kind::Constraint:
		result += "constraint";
		break;
	case Kind::Any:
	case Kind::Unresolved:
		return result + "unresolved(" + (typeExpression ? std::string("expr") : std::string("?")) + ")";
	}
	for (int pointerLevel = 0; pointerLevel < pointerDepth; pointerLevel++)
		result += "*";
	return result;
}

uint64_t DataType::getByteSize(const llvm::DataLayout &dataLayout, llvm::LLVMContext &llvmContext) const {
	requireCompilerInvariant(isRuntimeValueType(), "byte size requires a concrete runtime value type");
	llvm::Type *llvmType = toLLVM(llvmContext, dataLayout);
	requireCompilerInvariant(llvmType->isSized(), "byte size requires a sized LLVM type");
	return fixedAllocationSize(dataLayout, llvmType);
}

uint64_t DataType::getABIAlignment(const llvm::DataLayout &dataLayout, llvm::LLVMContext &llvmContext) const {
	llvm::Type *llvmType = toLLVM(llvmContext, dataLayout);
	if (kind == Kind::Class && pointerDepth == 0) {
		requireCompilerInvariant(
			classDefinition && classInstIndex >= 0 && classInstIndex < static_cast<int>(classDefinition->instantiations.size()),
			"class alignment requires a concrete class instantiation"
		);
		uint64_t alignment = classDefinition->instantiations[classInstIndex].llvmABIAlignment;
		requireCompilerInvariant(alignment > 0, "class layout is missing its ABI alignment");
		return alignment;
	}
	if (kind == Kind::Array && pointerDepth == 0) {
		requireCompilerInvariant(static_cast<bool>(arrayElementType), "array alignment requires an element type");
		return arrayElementType->getABIAlignment(dataLayout, llvmContext);
	}
	return dataLayout.getABITypeAlign(llvmType).value();
}

llvm::Type *DataType::toLLVM(llvm::LLVMContext &ctx, const llvm::DataLayout &dataLayout) const {
	// Any pointer type maps to opaque ptr
	if (pointerDepth > 0)
		return llvm::PointerType::getUnqual(ctx);

	switch (kind) {
	case Kind::Void:
		return llvm::Type::getVoidTy(ctx);
	case Kind::Bool:
		return llvm::Type::getInt1Ty(ctx);
	case Kind::Float:
		switch (numericSize) {
		case 4:
			return llvm::Type::getFloatTy(ctx);
		case 8:
			return llvm::Type::getDoubleTy(ctx);
		default:
			crashCompilerBug("Float type must have a valid numericSize (4/8) before codegen");
		}
	case Kind::Int:
		switch (numericSize) {
		case 1:
			return llvm::Type::getInt8Ty(ctx);
		case 2:
			return llvm::Type::getInt16Ty(ctx);
		case 4:
			return llvm::Type::getInt32Ty(ctx);
		case 8:
			return llvm::Type::getInt64Ty(ctx);
		default:
			crashCompilerBug("Integer type must have a valid numericSize (1/2/4/8) before codegen");
		}
	case Kind::Array:
		requireCompilerInvariant(arrayElementType && arraySize >= 0, "Array type must have element type and size");
		return llvm::ArrayType::get(arrayElementType->toLLVM(ctx, dataLayout), arraySize);
	case Kind::Vector:
		requireCompilerInvariant(arrayElementType && arraySize > 0, "Vector type must have element type and size");
		return llvm::FixedVectorType::get(arrayElementType->toLLVM(ctx, dataLayout), arraySize);
	case Kind::Matrix: {
		requireCompilerInvariant(
			arrayElementType && arraySize > 0 && matrixRowCount > 0, "Matrix type must have element type and dimensions"
		);
		llvm::Type *rowVectorType = llvm::FixedVectorType::get(arrayElementType->toLLVM(ctx, dataLayout), arraySize);
		return llvm::ArrayType::get(rowVectorType, matrixRowCount);
	}
	case Kind::Class: {
		requireCompilerInvariant(
			classDefinition && classInstIndex >= 0, "Class type must have classDefinition and instantiation index"
		);
		ClassInstantiation &inst = classDefinition->instantiations[classInstIndex];
		if (!inst.llvmStructType) {
			inst.llvmStructType = llvm::StructType::create(ctx, "class");
			std::vector<llvm::Type *> llvmFields;
			llvmFields.reserve(inst.fieldTypes.size());
			std::vector<uint64_t> fieldAlignments;
			fieldAlignments.reserve(inst.fieldTypes.size());
			requireCompilerInvariant(
				inst.fieldTypes.size() == classDefinition->fields.size(),
				"class instantiation field count differs from its definition"
			);
			for (size_t fieldIndex = 0; fieldIndex < inst.fieldTypes.size(); fieldIndex++) {
				llvm::Type *fieldType = inst.fieldTypes[fieldIndex].toLLVM(ctx, dataLayout);
				uint64_t fieldAlignment = inst.fieldTypes[fieldIndex].getABIAlignment(dataLayout, ctx);
				fieldAlignment = std::max<uint64_t>(fieldAlignment, classDefinition->fields[fieldIndex].alignment);
				llvmFields.push_back(fieldType);
				fieldAlignments.push_back(fieldAlignment);
			}
			LLVMClassLayout layout =
				layoutLLVMClass(ctx, dataLayout, *inst.llvmStructType, llvmFields, fieldAlignments, classDefinition->alignment);
			inst.llvmFieldIndices = std::move(layout.fieldIndices);
			inst.llvmABIAlignment = layout.abiAlignment;
		}
		return inst.llvmStructType;
	}
	case Kind::Type:
		crashCompilerBug("Type is compile-time only, cannot be converted to LLVM type");
	case Kind::Constraint:
		crashCompilerBug("Constraint is compile-time only, cannot be converted to LLVM type");
	case Kind::Unresolved:
		crashCompilerBug("Unresolved type must be resolved before codegen");
	case Kind::Any:
		crashCompilerBug("Any type must be resolved before codegen");
	}
	crashCompilerBug("Unknown type kind");
}

bool typeHasManagedLifecycle(const DataType &type) {
	std::set<ClassInstanceKey> visited;
	return typeHasManagedLifecycle(type, visited);
}
