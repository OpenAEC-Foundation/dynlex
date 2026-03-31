#pragma once
#include <algorithm>
#include <cassert>
#include <cctype>
#include <memory>
#include <string>

namespace llvm {
class Type;
class LLVMContext;
} // namespace llvm

struct ClassDefinition;
struct Expression;

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
		Type
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
	DataType vectorElementType() const {
		assert(hasVectorPayload() && arrayElementType && "Vector type must have element type");
		return *arrayElementType;
	}
	DataType matrixElementType() const {
		assert(hasMatrixPayload() && arrayElementType && "Matrix type must have element type");
		return *arrayElementType;
	}
	bool isPointer() const { return pointerDepth > 0; }
	bool isBytePointer() const { return kind == Kind::Int && numericSize == 1 && pointerDepth == 1; }
	// wether this type is a specific type and the type pattern has been resolved
	bool isDeduced() const { return kind != Kind::Any && kind != Kind::Unresolved; }

	static bool supportsRuntimeConversion(const DataType &fromType, const DataType &toType) {
		if (!fromType.isDeduced() || !toType.isDeduced())
			return false;
		if (fromType == toType)
			return true;
		if (fromType.kind == Kind::Void || fromType.kind == Kind::Type || toType.kind == Kind::Void ||
			toType.kind == Kind::Type)
			return false;

		if (fromType.isPointer() && toType.isPointer())
			return true;
		if (fromType.isPointer() && toType.kind == Kind::Int && toType.pointerDepth == 0)
			return true;
		if (fromType.kind == Kind::Int && fromType.pointerDepth == 0 && toType.isPointer())
			return true;

		if (fromType.isNumeric() && toType.isNumeric())
			return true;
		if (fromType.isNumeric() && toType.kind == Kind::Bool && toType.pointerDepth == 0)
			return true;
		if (fromType.kind == Kind::Bool && fromType.pointerDepth == 0 && toType.isNumeric())
			return true;

		if (toType.kind == Kind::Vector && toType.pointerDepth == 0 && toType.arrayElementType && fromType.isNumeric())
			return supportsRuntimeConversion(fromType, *toType.arrayElementType);
		if (fromType.kind == Kind::Vector && fromType.pointerDepth == 0 && fromType.arrayElementType &&
			toType.kind == Kind::Vector && toType.pointerDepth == 0 && toType.arrayElementType &&
			fromType.arraySize == toType.arraySize)
			return supportsRuntimeConversion(*fromType.arrayElementType, *toType.arrayElementType);

		return false;
	}

	int getByteSize() const;

	// Return this type with one more level of indirection
	DataType pointed() const {
		assert(isDeduced() && "Cannot take pointer to unresolved type");
		DataType result = *this;
		result.pointerDepth++;
		return result;
	}

	// Return this type with one less level of indirection
	DataType dereferenced() const {
		assert(pointerDepth > 0 && "Cannot dereference non-pointer type");
		DataType result = *this;
		result.pointerDepth--;
		return result;
	}

	// Promote for arithmetic, including pointer + number -> pointer
	static bool promoteArithmetic(const DataType &a, const DataType &b, DataType &result) {
		if (a.kind == Kind::Unresolved || b.kind == Kind::Unresolved) {
			result = {Kind::Unresolved};
			return true;
		}
		if (a.isPointer() && b.isNumeric()) {
			result = a;
			return true;
		}
		if (b.isPointer() && a.isNumeric()) {
			result = b;
			return true;
		}
		if (a.isVector() && b.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(a.vectorElementType(), b, elem))
				return false;
			result = a;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (b.isVector() && a.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(a, b.vectorElementType(), elem))
				return false;
			result = b;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (a.isVector() && b.isVector() && a.vectorSize() == b.vectorSize()) {
			DataType elem;
			if (!promoteArithmetic(a.vectorElementType(), b.vectorElementType(), elem))
				return false;
			result = a;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (a.isMatrix() && b.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(a.matrixElementType(), b, elem))
				return false;
			result = a;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (b.isMatrix() && a.isNumeric()) {
			DataType elem;
			if (!promoteArithmetic(a, b.matrixElementType(), elem))
				return false;
			result = b;
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (a.isMatrix() && b.isVector() && a.matrixColumns() == b.vectorSize()) {
			DataType elem;
			if (!promoteArithmetic(a.matrixElementType(), b.vectorElementType(), elem))
				return false;
			result = {Kind::Vector};
			result.arraySize = a.matrixRows();
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (a.isVector() && b.isMatrix() && a.vectorSize() == b.matrixRows()) {
			DataType elem;
			if (!promoteArithmetic(a.vectorElementType(), b.matrixElementType(), elem))
				return false;
			result = {Kind::Vector};
			result.arraySize = b.matrixColumns();
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (a.isMatrix() && b.isMatrix() && a.matrixColumns() == b.matrixRows()) {
			DataType elem;
			if (!promoteArithmetic(a.matrixElementType(), b.matrixElementType(), elem))
				return false;
			result = {Kind::Matrix};
			result.matrixRowCount = a.matrixRows();
			result.arraySize = b.matrixColumns();
			result.arrayElementType = std::make_shared<DataType>(elem);
			return true;
		}
		if (a.isNumeric() && b.isNumeric()) {
			result = {};
			result.kind = (a.kind == Kind::Float || b.kind == Kind::Float) ? Kind::Float : Kind::Int;
			result.numericSize = std::max(a.numericSize, b.numericSize);
			return true;
		}
		return false;
	}

	// Promote for bitwise operators: integers only, using the wider integer width.
	static bool promoteBitwise(const DataType &a, const DataType &b, DataType &result) {
		if (a.kind == Kind::Unresolved || b.kind == Kind::Unresolved) {
			result = {Kind::Unresolved};
			return true;
		}
		if (!a.isInteger() || !b.isInteger())
			return false;
		result = {Kind::Int, std::max(a.numericSize, b.numericSize)};
		return true;
	}

	// Convert a Type literal to the type it references
	DataType toReferencedType() const {
		assert(kind == Kind::Type && "Can only convert Type literals");
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

	// Parse a type from a string (e.g. "i32", "f64", "void")
	static DataType fromString(const std::string &s) {
		if (s == "void")
			return {Kind::Void};
		if (s == "bool")
			return {Kind::Bool};
		auto isBitSuffix = [&](char prefix) {
			return s.size() >= 2 && s[0] == prefix && std::all_of(s.begin() + 1, s.end(), [](unsigned char ch) {
				return std::isdigit(ch);
			});
		};
		if (isBitSuffix('i'))
			return {Kind::Int, std::stoi(s.substr(1)) / 8};
		if (isBitSuffix('f'))
			return {Kind::Float, std::stoi(s.substr(1)) / 8};
		return {};
	}

	llvm::Type *toLLVM(llvm::LLVMContext &ctx) const;

	std::string toString() const;
};

inline int defaultFloatByteSize(bool emitSPIRV) { return emitSPIRV ? 4 : 8; }

inline DataType defaultFloatType(bool emitSPIRV) { return {DataType::Kind::Float, defaultFloatByteSize(emitSPIRV)}; }
