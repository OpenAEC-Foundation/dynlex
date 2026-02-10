#pragma once
#include <cassert>
#include <string>

namespace llvm {
class Type;
class LLVMContext;
} // namespace llvm

struct Type {
	enum class Kind { Undeduced, Void, Bool, Numeric, Integer, Float, String };

	Kind kind = Kind::Undeduced;
	int byteSize = 0;	  // Integer: 1/2/4/8, Float: 4/8, others: 0
	int pointerDepth = 0; // 0=value, 1=ptr, 2=ptr-to-ptr, ...

	bool operator==(const Type &other) const {
		return kind == other.kind && byteSize == other.byteSize && pointerDepth == other.pointerDepth;
	}
	bool operator!=(const Type &other) const { return !(*this == other); }
	bool operator<(const Type &other) const {
		if (kind != other.kind)
			return kind < other.kind;
		if (byteSize != other.byteSize)
			return byteSize < other.byteSize;
		return pointerDepth < other.pointerDepth;
	}

	bool isNumeric() const {
		return pointerDepth == 0 && (kind == Kind::Numeric || kind == Kind::Integer || kind == Kind::Float);
	}
	bool isPointer() const { return pointerDepth > 0; }
	bool isDeduced() const { return kind != Kind::Undeduced; }

	// Whether this type can be refined to a more specific type
	bool canRefineTo(const Type &target) const {
		if (!isDeduced())
			return true;
		if (pointerDepth != target.pointerDepth)
			return false;
		if (kind == Kind::Numeric && (target.kind == Kind::Integer || target.kind == Kind::Float))
			return true;
		// Same kind but different size: allow refinement to larger or more specific size
		if (kind == target.kind && kind == Kind::Integer && byteSize == 0)
			return true;
		if (kind == target.kind && kind == Kind::Float && byteSize == 0)
			return true;
		return false;
	}

	// Return this type with one more level of indirection
	Type pointed() const {
		assert(isDeduced() && "Cannot take pointer to undeduced type");
		return {kind, byteSize, pointerDepth + 1};
	}

	// Return this type with one less level of indirection
	Type dereferenced() const {
		assert(pointerDepth > 0 && "Cannot dereference non-pointer type");
		return {kind, byteSize, pointerDepth - 1};
	}

	// Promote two numeric types for arithmetic: Numeric adapts, Float wins over Integer
	static Type promote(const Type &a, const Type &b) {
		assert((a.isNumeric() || a.kind == Kind::Undeduced) && "First operand must be numeric type");
		assert((b.isNumeric() || b.kind == Kind::Undeduced) && "Second operand must be numeric type");

		if (a.kind == Kind::Float || b.kind == Kind::Float) {
			// Float wins. Pick the larger byteSize.
			int aSize = a.kind == Kind::Float ? a.byteSize : 0;
			int bSize = b.kind == Kind::Float ? b.byteSize : 0;
			int floatSize = std::max(aSize, bSize);
			// When mixing Integer + Float, use max of both sizes (i64+f32 → f64 to avoid precision loss)
			if (a.kind == Kind::Integer || b.kind == Kind::Integer) {
				int intSize = a.kind == Kind::Integer ? a.byteSize : b.byteSize;
				floatSize = std::max(floatSize, intSize);
			}
			return {Kind::Float, floatSize};
		}
		if (a.kind == Kind::Integer || b.kind == Kind::Integer) {
			// Both Integer (or Integer + Numeric): pick larger byteSize
			int aSize = a.kind == Kind::Integer ? a.byteSize : 0;
			int bSize = b.kind == Kind::Integer ? b.byteSize : 0;
			return {Kind::Integer, std::max(aSize, bSize)};
		}
		if (a.kind == Kind::Numeric || b.kind == Kind::Numeric)
			return {Kind::Numeric};
		return {Kind::Undeduced};
	}

	// Promote for arithmetic, including pointer + integer -> pointer
	static Type promoteArithmetic(const Type &a, const Type &b) {
		if (a.isPointer() && (b.isNumeric() || b.kind == Kind::Undeduced))
			return a;
		if (b.isPointer() && (a.isNumeric() || a.kind == Kind::Undeduced))
			return b;
		return promote(a, b);
	}

	static Type fromString(const std::string &s) {
		if (s == "void")
			return {Kind::Void};
		if (s == "bool")
			return {Kind::Bool};
		if (s == "i8")
			return {Kind::Integer, 1};
		if (s == "i16")
			return {Kind::Integer, 2};
		if (s == "i32")
			return {Kind::Integer, 4};
		if (s == "i64")
			return {Kind::Integer, 8};
		if (s == "f32")
			return {Kind::Float, 4};
		if (s == "f64")
			return {Kind::Float, 8};
		if (s == "pointer")
			return {Kind::Integer, 8, 1};
		if (s == "string")
			return {Kind::String};
		assert(false && "Unknown type string");
		return {};
	}

	llvm::Type *toLLVM(llvm::LLVMContext &ctx) const;

	std::string toString() const {
		std::string base;
		switch (kind) {
		case Kind::Undeduced:
			base = "undeduced";
			break;
		case Kind::Void:
			base = "void";
			break;
		case Kind::Bool:
			base = "bool";
			break;
		case Kind::Numeric:
			base = "numeric";
			break;
		case Kind::Integer:
			switch (byteSize) {
			case 1:
				base = "i8";
				break;
			case 2:
				base = "i16";
				break;
			case 4:
				base = "i32";
				break;
			case 8:
				base = "i64";
				break;
			default:
				base = "integer";
				break;
			}
			break;
		case Kind::Float:
			switch (byteSize) {
			case 4:
				base = "f32";
				break;
			case 8:
				base = "f64";
				break;
			default:
				base = "float";
				break;
			}
			break;
		case Kind::String:
			base = "string";
			break;
		default:
			base = "unknown";
			break;
		}
		for (int i = 0; i < pointerDepth; i++)
			base = "pointer to " + base;
		return base;
	}
};
