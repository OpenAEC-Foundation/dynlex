#pragma once
#include "compilerUtils.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace llvm {
class DataLayout;
class Type;
class LLVMContext;
} // namespace llvm

struct ClassDefinition;
struct Expression;
struct TypeConstraint;

struct DataType {
	enum class Kind {
		// any type (unconstrained)
		Any,
		// unresolved: the type is defined with a type reference which isn't resolved yet
		Unresolved,
		// nothing
		Void,
		Bool,
		Float,
		Int,
		Array,
		Vector,
		Matrix,
		// a class with members. for example point
		Class,
		// a type literal. for example, we can pass a type literal like '32 bit int' as argument.
		Type,
		// a compile-time value describing which argument types and values a pattern accepts.
		Constraint
	};

	Kind kind = Kind::Unresolved;
	int numericSize = 0;						// Int/Float: 1/2/4/8, others: 0
	int pointerDepth = 0;						// 0=value, 1=ptr, 2=ptr-to-ptr, ...
	ClassDefinition *classDefinition = nullptr; // For Kind::Class and Kind::Type (class type refs)
	int classInstIndex = -1;					// Index into classDefinition->instantiations
	Expression *typeExpression = nullptr;		// For Kind::Unresolved: class pattern reference to resolve
	Kind referencedKind = Kind::Unresolved;		// For Kind::Type: the kind this type literal refers to
	int arraySize = 0;							// For Kind::Array and Type(Array)
	std::shared_ptr<DataType> arrayElementType; // For Kind::Array and Type(Array)
	int matrixRowCount = 0;						// For Kind::Matrix and Type(Matrix): row count, arraySize stores column count

	DataType() = default;
	DataType(Kind kind) : kind(kind) {}
	DataType(Kind kind, int numericSize) : kind(kind), numericSize(numericSize) {}
	DataType(
		Kind kind, int numericSize, int pointerDepth, ClassDefinition *classDefinition = nullptr, int classInstIndex = -1,
		Expression *typeExpression = nullptr, Kind referencedKind = Kind::Unresolved, int arraySize = 0,
		std::shared_ptr<DataType> arrayElementType = nullptr
	)
		: kind(kind), numericSize(numericSize), pointerDepth(pointerDepth), classDefinition(classDefinition),
		  classInstIndex(classInstIndex), typeExpression(typeExpression), referencedKind(referencedKind), arraySize(arraySize),
		  arrayElementType(std::move(arrayElementType)) {}

	bool hasArrayPayload() const { return kind == Kind::Array || (kind == Kind::Type && referencedKind == Kind::Array); }
	bool hasVectorPayload() const { return kind == Kind::Vector || (kind == Kind::Type && referencedKind == Kind::Vector); }
	bool hasMatrixPayload() const { return kind == Kind::Matrix || (kind == Kind::Type && referencedKind == Kind::Matrix); }

	bool operator==(const DataType &other) const {
		if (kind != other.kind || pointerDepth != other.pointerDepth || numericSize != other.numericSize ||
			referencedKind != other.referencedKind || arraySize != other.arraySize || matrixRowCount != other.matrixRowCount)
			return false;
		if (hasArrayPayload() || hasVectorPayload() || hasMatrixPayload()) {
			bool hasElem = !!arrayElementType;
			bool otherHasElem = !!other.arrayElementType;
			if (hasElem != otherHasElem)
				return false;
			if (hasElem && *arrayElementType != *other.arrayElementType)
				return false;
		}
		if (kind == Kind::Class)
			return classDefinition == other.classDefinition && classInstIndex == other.classInstIndex;
		if (kind == Kind::Type && referencedKind == Kind::Class)
			return classDefinition == other.classDefinition && classInstIndex == other.classInstIndex;
		return true; // Void, Bool, Int, Float, Type: kind+pointerDepth+numericSize suffice
	}
	bool operator!=(const DataType &other) const { return !(*this == other); }
	bool operator<(const DataType &other) const {
		if (kind != other.kind)
			return kind < other.kind;
		if (pointerDepth != other.pointerDepth)
			return pointerDepth < other.pointerDepth;
		if (numericSize != other.numericSize)
			return numericSize < other.numericSize;
		if (referencedKind != other.referencedKind)
			return referencedKind < other.referencedKind;
		if (arraySize != other.arraySize)
			return arraySize < other.arraySize;
		if (matrixRowCount != other.matrixRowCount)
			return matrixRowCount < other.matrixRowCount;
		if (hasArrayPayload() || hasVectorPayload() || hasMatrixPayload()) {
			if (!!arrayElementType != !!other.arrayElementType)
				return !!arrayElementType < !!other.arrayElementType;
			if (arrayElementType && *arrayElementType != *other.arrayElementType)
				return *arrayElementType < *other.arrayElementType;
		}
		if (kind == Kind::Class) {
			if (classDefinition != other.classDefinition)
				return classDefinition < other.classDefinition;
			return classInstIndex < other.classInstIndex;
		}
		if (kind == Kind::Type && referencedKind == Kind::Class) {
			if (classDefinition != other.classDefinition)
				return classDefinition < other.classDefinition;
			return classInstIndex < other.classInstIndex;
		}
		return false;
	}

	bool isNumeric() const { return (kind == Kind::Float || kind == Kind::Int) && pointerDepth == 0; }
	bool isInteger() const { return kind == Kind::Int && pointerDepth == 0; }
	bool isVector() const { return kind == Kind::Vector && pointerDepth == 0; }
	bool isMatrix() const { return kind == Kind::Matrix && pointerDepth == 0; }
	int vectorSize() const { return arraySize; }
	int matrixColumns() const { return arraySize; }
	int matrixRows() const { return matrixRowCount; }
	std::optional<int> extent(int dimension) const {
		DataType valueType = *this;
		if (valueType.kind == Kind::Type)
			valueType = valueType.toReferencedType();
		if (valueType.pointerDepth != 0 || dimension < 0)
			return std::nullopt;
		if ((valueType.kind == Kind::Array || valueType.kind == Kind::Vector) && dimension == 0)
			return valueType.arraySize;
		if (valueType.kind == Kind::Matrix) {
			if (dimension == 0)
				return valueType.matrixRows();
			if (dimension == 1)
				return valueType.matrixColumns();
		}
		return std::nullopt;
	}
	DataType vectorElementType() const {
		requireCompilerInvariant(hasVectorPayload() && arrayElementType, "Vector type must have element type");
		return *arrayElementType;
	}
	DataType matrixElementType() const {
		requireCompilerInvariant(hasMatrixPayload() && arrayElementType, "Matrix type must have element type");
		return *arrayElementType;
	}
	bool hasAggregateElementType() const {
		return pointerDepth == 0 && (kind == Kind::Array || kind == Kind::Vector || kind == Kind::Matrix) &&
			   static_cast<bool>(arrayElementType);
	}
	DataType aggregateElementType() const {
		requireCompilerInvariant(hasAggregateElementType(), "Aggregate type must have an element type");
		return *arrayElementType;
	}
	bool isPointer() const { return pointerDepth > 0; }
	bool isMetaType() const { return kind == Kind::Type || kind == Kind::Constraint; }
	bool isRuntimeValueType() const { return isDeduced() && kind != Kind::Void && !isMetaType(); }
	bool isBytePointer() const { return kind == Kind::Int && numericSize == 1 && pointerDepth == 1; }
	// wether this type is a specific type and the type pattern has been resolved
	bool isDeduced() const { return kind != Kind::Any && kind != Kind::Unresolved; }
	bool isConcrete() const {
		if (!isDeduced())
			return false;
		// A pointer has a complete runtime representation even while a recursive
		// pointee class is still being instantiated.
		if (pointerDepth > 0)
			return kind != Kind::Class || classDefinition;
		if (kind == Kind::Array)
			return arraySize >= 0 && arrayElementType && arrayElementType->isConcrete();
		if (kind == Kind::Vector)
			return arraySize > 0 && arrayElementType && arrayElementType->isConcrete();
		if (kind == Kind::Matrix)
			return matrixRowCount > 0 && arraySize > 0 && arrayElementType && arrayElementType->isConcrete();
		if (kind == Kind::Class)
			return classDefinition && classInstIndex >= 0;
		if (kind == Kind::Type) {
			if (referencedKind == Kind::Array)
				return arraySize >= 0 && arrayElementType && arrayElementType->isConcrete();
			if (referencedKind == Kind::Vector)
				return arraySize > 0 && arrayElementType && arrayElementType->isConcrete();
			if (referencedKind == Kind::Matrix)
				return matrixRowCount > 0 && arraySize > 0 && arrayElementType && arrayElementType->isConcrete();
			if (referencedKind == Kind::Class)
				return classDefinition && classInstIndex >= 0;
			return referencedKind != Kind::Any && referencedKind != Kind::Unresolved;
		}
		return true;
	}
	static bool supportsRuntimeConversion(const DataType &fromType, const DataType &toType) {
		const DataType &concreteFromType = fromType;
		const DataType &concreteToType = toType;
		if (!concreteFromType.isDeduced() || !concreteToType.isDeduced())
			return false;
		if (concreteFromType == concreteToType)
			return true;
		if (concreteFromType.kind == Kind::Void || concreteFromType.isMetaType() || concreteToType.kind == Kind::Void ||
			concreteToType.isMetaType())
			return false;

		if (concreteFromType.isPointer() && concreteToType.isPointer())
			return true;
		if (concreteFromType.isPointer() && concreteToType.kind == Kind::Int && concreteToType.pointerDepth == 0)
			return true;
		if (concreteFromType.kind == Kind::Int && concreteFromType.pointerDepth == 0 && concreteToType.isPointer())
			return true;

		if (concreteFromType.isNumeric() && concreteToType.isNumeric())
			return true;
		if (concreteFromType.isNumeric() && concreteToType.kind == Kind::Bool && concreteToType.pointerDepth == 0)
			return true;
		if (concreteFromType.kind == Kind::Bool && concreteFromType.pointerDepth == 0 && concreteToType.isNumeric())
			return true;

		if (concreteToType.kind == Kind::Vector && concreteToType.pointerDepth == 0 && concreteToType.arrayElementType &&
			concreteFromType.isNumeric())
			return supportsRuntimeConversion(concreteFromType, *concreteToType.arrayElementType);
		bool fromIsSequentialAggregate = (concreteFromType.kind == Kind::Array || concreteFromType.kind == Kind::Vector) &&
										 concreteFromType.pointerDepth == 0 && concreteFromType.arrayElementType;
		bool toIsSequentialAggregate = (concreteToType.kind == Kind::Array || concreteToType.kind == Kind::Vector) &&
									   concreteToType.pointerDepth == 0 && concreteToType.arrayElementType;
		if (fromIsSequentialAggregate && toIsSequentialAggregate && concreteFromType.arraySize == concreteToType.arraySize)
			return supportsRuntimeConversion(*concreteFromType.arrayElementType, *concreteToType.arrayElementType);

		return false;
	}

	uint64_t getByteSize(const llvm::DataLayout &dataLayout, llvm::LLVMContext &llvmContext) const;
	uint64_t getABIAlignment(const llvm::DataLayout &dataLayout, llvm::LLVMContext &llvmContext) const;

	// Return this type with one more level of indirection
	DataType pointed() const {
		requireCompilerInvariant(isDeduced(), "Cannot take pointer to unresolved type");
		DataType result = *this;
		result.pointerDepth++;
		return result;
	}

	// Return this type with one less level of indirection
	DataType dereferenced() const {
		requireCompilerInvariant(pointerDepth > 0, "Cannot dereference non-pointer type");
		DataType result = *this;
		result.pointerDepth--;
		return result;
	}

	// Promote for arithmetic, including pointer + number -> pointer
	static bool promoteArithmetic(const DataType &a, const DataType &b, DataType &result) {
		DataType left = a;
		DataType right = b;
		if (left.kind == Kind::Unresolved || right.kind == Kind::Unresolved) {
			result = {Kind::Unresolved};
			return true;
		}
		if (left.isPointer() && right.isNumeric()) {
			result = left;
			return true;
		}
		if (right.isPointer() && left.isNumeric()) {
			result = right;
			return true;
		}
		if (left.isVector() && right.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(left.vectorElementType(), right, elem))
				return false;
			result = left;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (right.isVector() && left.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(left, right.vectorElementType(), elem))
				return false;
			result = right;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (left.isVector() && right.isVector() && left.vectorSize() == right.vectorSize()) {
			DataType elem;
			if (!promoteArithmetic(left.vectorElementType(), right.vectorElementType(), elem))
				return false;
			result = left;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (left.isMatrix() && right.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(left.matrixElementType(), right, elem))
				return false;
			result = left;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (right.isMatrix() && left.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(left, right.matrixElementType(), elem))
				return false;
			result = right;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (left.isMatrix() && right.isVector() && left.matrixColumns() == right.vectorSize()) {
			DataType elem;
			if (!promoteArithmetic(left.matrixElementType(), right.vectorElementType(), elem))
				return false;
			result = {Kind::Vector};
			result.arraySize = left.matrixRows();
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (left.isVector() && right.isMatrix() && left.vectorSize() == right.matrixRows()) {
			DataType elem;
			if (!promoteArithmetic(left.vectorElementType(), right.matrixElementType(), elem))
				return false;
			result = {Kind::Vector};
			result.arraySize = right.matrixColumns();
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (left.isMatrix() && right.isMatrix() && left.matrixColumns() == right.matrixRows()) {
			DataType elem;
			if (!promoteArithmetic(left.matrixElementType(), right.matrixElementType(), elem))
				return false;
			result = {Kind::Matrix};
			result.matrixRowCount = left.matrixRows();
			result.arraySize = right.matrixColumns();
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (left.isNumeric() && right.isNumeric()) {
			result = {};
			result.kind = (left.kind == Kind::Float || right.kind == Kind::Float) ? Kind::Float : Kind::Int;
			result.numericSize = std::max(left.numericSize, right.numericSize);
			return true;
		}
		return false;
	}

	static bool promoteEquality(const DataType &a, const DataType &b, DataType &result) {
		if (a.kind == Kind::Bool && b.kind == Kind::Bool) {
			result = {Kind::Bool};
			return true;
		}
		return promoteArithmetic(a, b, result);
	}

	// Promote for bitwise operators: integers only, using the wider integer width.
	static bool promoteBitwise(const DataType &a, const DataType &b, DataType &result) {
		DataType left = a;
		DataType right = b;
		if (left.kind == Kind::Unresolved || right.kind == Kind::Unresolved) {
			result = {Kind::Unresolved};
			return true;
		}
		if (!left.isInteger() || !right.isInteger())
			return false;
		result = {Kind::Int, std::max(left.numericSize, right.numericSize)};
		return true;
	}

	// Convert a value type to a Type literal that references it.
	DataType asTypeReference() const {
		requireCompilerInvariant(isDeduced() && kind != Kind::Type, "Only value types can become type references");
		DataType result{Kind::Type};
		result.referencedKind = kind;
		result.numericSize = numericSize;
		result.pointerDepth = pointerDepth;
		result.classDefinition = classDefinition;
		result.classInstIndex = classInstIndex;
		result.arraySize = arraySize;
		result.matrixRowCount = matrixRowCount;
		result.arrayElementType = arrayElementType ? std::make_shared<DataType>(*arrayElementType) : nullptr;
		return result;
	}

	// Convert a Type literal to the type it references
	DataType toReferencedType() const {
		requireCompilerInvariant(kind == Kind::Type, "Can only convert Type literals");
		DataType result;
		result.kind = referencedKind;
		result.numericSize = numericSize;
		result.pointerDepth = pointerDepth;
		result.classDefinition = classDefinition;
		result.classInstIndex = classInstIndex;
		result.arraySize = arraySize;
		result.matrixRowCount = matrixRowCount;
		result.arrayElementType = arrayElementType ? std::make_shared<DataType>(*arrayElementType) : nullptr;
		return result;
	}

	llvm::Type *toLLVM(llvm::LLVMContext &ctx, const llvm::DataLayout &dataLayout) const;

	std::string toString() const;
};

bool typeHasManagedLifecycle(const DataType &type);
std::string typeToUserName(const DataType &type);
std::string typeToUserName(const TypeConstraint &constraint);

inline int defaultFloatByteSize(bool emitSPIRV) { return emitSPIRV ? 4 : 8; }

inline DataType defaultFloatType(bool emitSPIRV) { return {DataType::Kind::Float, defaultFloatByteSize(emitSPIRV)}; }

inline std::optional<DataType>
makeBuiltinTypeReference(std::string_view kindName, bool emitSPIRV, std::optional<int> numericByteSize = std::nullopt) {
	DataType result{DataType::Kind::Type};
	if (kindName == "int") {
		result.referencedKind = DataType::Kind::Int;
		result.numericSize = numericByteSize.value_or(4);
	} else if (kindName == "float") {
		result.referencedKind = DataType::Kind::Float;
		result.numericSize = numericByteSize.value_or(defaultFloatByteSize(emitSPIRV));
	} else if (kindName == "bool") {
		if (numericByteSize)
			return std::nullopt;
		result.referencedKind = DataType::Kind::Bool;
	} else if (kindName == "nothing") {
		if (numericByteSize)
			return std::nullopt;
		result.referencedKind = DataType::Kind::Void;
	} else if (kindName == "string") {
		if (numericByteSize)
			return std::nullopt;
		result.referencedKind = DataType::Kind::Int;
		result.numericSize = 1;
		result.pointerDepth = 1;
	} else if (kindName == "type") {
		if (numericByteSize)
			return std::nullopt;
		result.referencedKind = DataType::Kind::Type;
	} else if (kindName == "constraint") {
		if (numericByteSize)
			return std::nullopt;
		result.referencedKind = DataType::Kind::Constraint;
	} else if (kindName == "any") {
		result.referencedKind = DataType::Kind::Any;
		result.numericSize = numericByteSize.value_or(0);
	} else {
		return std::nullopt;
	}
	return result;
}
