#pragma once
#include <cassert>
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

	bool operator==(const DataType &other) const {
		if (kind != other.kind || pointerDepth != other.pointerDepth || numericSize != other.numericSize)
			return false;
		if (kind == Kind::Class)
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
		if (kind == Kind::Class) {
			if (classDefinition != other.classDefinition)
				return classDefinition < other.classDefinition;
			return classInstIndex < other.classInstIndex;
		}
		return false;
	}

	bool isNumeric() const { return (kind == Kind::Float || kind == Kind::Int) && pointerDepth == 0; }
	bool isPointer() const { return pointerDepth > 0; }
	bool isBytePointer() const { return kind == Kind::Int && numericSize == 1 && pointerDepth == 1; }
	// wether this type is a specific type and the type pattern has been resolved
	bool isDeduced() const { return kind != Kind::Any && kind != Kind::Unresolved; }

	int getByteSize() const;

	// Whether this type can be refined to a more specific type
	// bool canRefineTo(const DataType &target) const {
	//	if (pointerDepth != target.pointerDepth)
	//		return false;
	//	return false;
	//}

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
		if (a.isNumeric() && b.isNumeric()) {
			result = {};
			result.kind = (a.kind == Kind::Float || b.kind == Kind::Float) ? Kind::Float : Kind::Int;
			result.numericSize = std::max(a.numericSize, b.numericSize);
			return true;
		}
		return false;
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
		return result;
	}

	// Parse a type from a string (e.g. "i32", "f64", "void")
	static DataType fromString(const std::string &s) {
		if (s == "void")
			return {Kind::Void};
		if (s == "bool")
			return {Kind::Bool};
		if (s.size() >= 2 && s[0] == 'i')
			return {Kind::Int, std::stoi(s.substr(1)) / 8};
		if (s.size() >= 2 && s[0] == 'f')
			return {Kind::Float, std::stoi(s.substr(1)) / 8};
		return {};
	}

	llvm::Type *toLLVM(llvm::LLVMContext &ctx) const;

	std::string toString() const;
};
