#pragma once
#include <string>
#include <unordered_map>

// Describes how an intrinsic's return type relates to its arguments
enum class IntrinsicReturnKind {
	SameAsArgs, // return type = promoted type of arguments (arithmetic, math)
	Bool,       // returns boolean
	Void,       // no return value
	Float,      // always returns float (shader I/O)
	Custom,     // special handling required
};

#define DYNLEX_INTRINSIC_FIXED_TABLE(X)                                                                                     \
	X(Add, "add", 3, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Subtract, "subtract", 3, IntrinsicReturnKind::SameAsArgs)                                                             \
	X(Multiply, "multiply", 3, IntrinsicReturnKind::SameAsArgs)                                                             \
	X(Divide, "divide", 3, IntrinsicReturnKind::SameAsArgs)                                                                 \
	X(Modulo, "modulo", 3, IntrinsicReturnKind::SameAsArgs)                                                                 \
	X(Negate, "negate", 2, IntrinsicReturnKind::SameAsArgs)                                                                 \
	X(Sin, "sin", 2, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Cos, "cos", 2, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Sqrt, "sqrt", 2, IntrinsicReturnKind::SameAsArgs)                                                                     \
	X(Abs, "abs", 2, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Floor, "floor", 2, IntrinsicReturnKind::SameAsArgs)                                                                   \
	X(Ceil, "ceil", 2, IntrinsicReturnKind::SameAsArgs)                                                                     \
	X(Round, "round", 2, IntrinsicReturnKind::SameAsArgs)                                                                   \
	X(Exp, "exp", 2, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Log, "log", 2, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Pow, "pow", 3, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Atan2, "atan2", 3, IntrinsicReturnKind::SameAsArgs)                                                                   \
	X(Min, "min", 3, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(Max, "max", 3, IntrinsicReturnKind::SameAsArgs)                                                                       \
	X(LessThan, "less than", 3, IntrinsicReturnKind::Bool)                                                                  \
	X(GreaterThan, "greater than", 3, IntrinsicReturnKind::Bool)                                                            \
	X(Equal, "equal", 3, IntrinsicReturnKind::Bool)                                                                         \
	X(NotEqual, "not equal", 3, IntrinsicReturnKind::Bool)                                                                  \
	X(LessThanOrEqual, "less than or equal", 3, IntrinsicReturnKind::Bool)                                                  \
	X(GreaterThanOrEqual, "greater than or equal", 3, IntrinsicReturnKind::Bool)                                            \
	X(And, "and", 3, IntrinsicReturnKind::Bool)                                                                             \
	X(Or, "or", 3, IntrinsicReturnKind::Bool)                                                                               \
	X(Not, "not", 2, IntrinsicReturnKind::Bool)                                                                             \
	X(Discard, "discard", 2, IntrinsicReturnKind::Void)                                                                     \
	X(Store, "store", 3, IntrinsicReturnKind::Void)                                                                         \
	X(StoreAt, "store at", 4, IntrinsicReturnKind::Void)                                                                    \
	X(LoopWhile, "loop while", 2, IntrinsicReturnKind::Void)                                                                \
	X(If, "if", 2, IntrinsicReturnKind::Void)                                                                               \
	X(ElseIf, "else if", 2, IntrinsicReturnKind::Void)                                                                      \
	X(Else, "else", 1, IntrinsicReturnKind::Void)                                                                           \
	X(Switch, "switch", 2, IntrinsicReturnKind::Void)                                                                       \
	X(Case, "case", 2, IntrinsicReturnKind::Void)                                                                           \
	X(ShaderOutput, "shader output", 5, IntrinsicReturnKind::Void)                                                          \
	X(ShaderInput, "shader input", 2, IntrinsicReturnKind::Float)                                                           \
	X(ShaderUniform, "shader uniform", 2, IntrinsicReturnKind::Float)                                                       \
	X(ExtractElement, "extract element", 3, IntrinsicReturnKind::Float)                                                     \
	X(AddressOf, "address of", 2, IntrinsicReturnKind::Custom)                                                              \
	X(Dereference, "dereference", 2, IntrinsicReturnKind::Custom)                                                            \
	X(LoadAt, "load at", 3, IntrinsicReturnKind::Custom)                                                                    \
	X(Property, "property", 3, IntrinsicReturnKind::Custom)                                                                 \
	X(Cast, "cast", 3, IntrinsicReturnKind::Custom)                                                                         \
	X(TypeOf, "type of", 2, IntrinsicReturnKind::Custom)                                                                    \
	X(BuildInfo, "build info", 2, IntrinsicReturnKind::Custom)                                                              \
	X(Select, "select", 4, IntrinsicReturnKind::Custom)                                                                     \
	X(AddPointerDepth, "add pointer depth", 2, IntrinsicReturnKind::Custom)

#define DYNLEX_INTRINSIC_RANGED_TABLE(X)                                                                                    \
	X(Construct, "construct", 2, -1, IntrinsicReturnKind::Custom)                                                           \
	X(Return, "return", 1, 2, IntrinsicReturnKind::Custom)                                                                  \
	X(Call, "call", 4, -1, IntrinsicReturnKind::Custom)                                                                     \
	X(Type, "type", 2, 3, IntrinsicReturnKind::Custom)                                                                      \
	X(Array, "array", 2, 3, IntrinsicReturnKind::Custom)                                                                    \
	X(Vector, "vector", 2, 3, IntrinsicReturnKind::Custom)                                                                  \
	X(Matrix, "matrix", 3, 4, IntrinsicReturnKind::Custom)

enum class IntrinsicKind {
	Unknown,
#define DYNLEX_INTRINSIC_KIND_ENUM_FIXED(kind, name, minArgCount, returnKind) kind,
#define DYNLEX_INTRINSIC_KIND_ENUM_RANGED(kind, name, minArgCount, maxArgCount, returnKind) kind,
	DYNLEX_INTRINSIC_FIXED_TABLE(DYNLEX_INTRINSIC_KIND_ENUM_FIXED)
	DYNLEX_INTRINSIC_RANGED_TABLE(DYNLEX_INTRINSIC_KIND_ENUM_RANGED)
#undef DYNLEX_INTRINSIC_KIND_ENUM_FIXED
#undef DYNLEX_INTRINSIC_KIND_ENUM_RANGED
};

struct IntrinsicInfo {
	int minArgCount; // expected minimum argument count including name
	int maxArgCount; // expected maximum argument count including name (-1 = unbounded)
	IntrinsicReturnKind returnKind;
	IntrinsicKind kind;

	constexpr IntrinsicInfo(int minArgCount_, IntrinsicReturnKind returnKind_, IntrinsicKind kind_)
		: minArgCount(minArgCount_), maxArgCount(minArgCount_), returnKind(returnKind_), kind(kind_) {}

	constexpr IntrinsicInfo(int minArgCount_, int maxArgCount_, IntrinsicReturnKind returnKind_, IntrinsicKind kind_)
		: minArgCount(minArgCount_), maxArgCount(maxArgCount_), returnKind(returnKind_), kind(kind_) {}
};

enum class ArithmeticIntrinsicKind { None, Add, Subtract, Multiply, Divide, Modulo };

// Central registry of all intrinsic signatures.
// Argument count includes the intrinsic name argument (e.g. @intrinsic("add", a, b) -> count=3).
// Intrinsic argument indexing is always:
//   arguments[0] = intrinsic name literal (e.g. "add")
//   arguments[1..] = user-supplied arguments
inline const std::unordered_map<std::string, IntrinsicInfo> &intrinsicRegistry() {
	static const std::unordered_map<std::string, IntrinsicInfo> registry = {
#define DYNLEX_INTRINSIC_REG_ENTRY_FIXED(kind, name, minArgCount, returnKind) {name, {minArgCount, returnKind, IntrinsicKind::kind}},
#define DYNLEX_INTRINSIC_REG_ENTRY_RANGED(kind, name, minArgCount, maxArgCount, returnKind)                                  \
	{name, {minArgCount, maxArgCount, returnKind, IntrinsicKind::kind}},
		DYNLEX_INTRINSIC_FIXED_TABLE(DYNLEX_INTRINSIC_REG_ENTRY_FIXED)
		DYNLEX_INTRINSIC_RANGED_TABLE(DYNLEX_INTRINSIC_REG_ENTRY_RANGED)
#undef DYNLEX_INTRINSIC_REG_ENTRY_FIXED
#undef DYNLEX_INTRINSIC_REG_ENTRY_RANGED
	};
	return registry;
}

inline const IntrinsicInfo *findIntrinsic(const std::string &name) {
	auto it = intrinsicRegistry().find(name);
	return it != intrinsicRegistry().end() ? &it->second : nullptr;
}

inline IntrinsicKind intrinsicKind(const std::string &name) {
	const IntrinsicInfo *info = findIntrinsic(name);
	return info ? info->kind : IntrinsicKind::Unknown;
}

inline ArithmeticIntrinsicKind arithmeticIntrinsicKind(const std::string &name) {
	switch (intrinsicKind(name)) {
	case IntrinsicKind::Add:
		return ArithmeticIntrinsicKind::Add;
	case IntrinsicKind::Subtract:
		return ArithmeticIntrinsicKind::Subtract;
	case IntrinsicKind::Multiply:
		return ArithmeticIntrinsicKind::Multiply;
	case IntrinsicKind::Divide:
		return ArithmeticIntrinsicKind::Divide;
	case IntrinsicKind::Modulo:
		return ArithmeticIntrinsicKind::Modulo;
	default:
		return ArithmeticIntrinsicKind::None;
	}
}

inline bool isArithmeticIntrinsic(ArithmeticIntrinsicKind kind) { return kind != ArithmeticIntrinsicKind::None; }

inline bool isPointerArithmeticIntrinsic(ArithmeticIntrinsicKind kind) {
	return kind == ArithmeticIntrinsicKind::Add || kind == ArithmeticIntrinsicKind::Subtract;
}

inline bool isLogicalIntrinsicKind(IntrinsicKind kind) {
	return kind == IntrinsicKind::And || kind == IntrinsicKind::Or || kind == IntrinsicKind::Not;
}

inline bool isComparisonIntrinsicKind(IntrinsicKind kind) {
	switch (kind) {
	case IntrinsicKind::LessThan:
	case IntrinsicKind::GreaterThan:
	case IntrinsicKind::Equal:
	case IntrinsicKind::NotEqual:
	case IntrinsicKind::LessThanOrEqual:
	case IntrinsicKind::GreaterThanOrEqual:
		return true;
	default:
		return false;
	}
}

inline bool intrinsicArgumentIsCompileTimeOnly(const std::string &name, int argIndex) {
	switch (intrinsicKind(name)) {
	case IntrinsicKind::Construct:
		return argIndex == 1;
	case IntrinsicKind::Cast:
		return argIndex == 2;
	case IntrinsicKind::Type:
		return argIndex >= 1;
	case IntrinsicKind::Array:
		return argIndex >= 1;
	case IntrinsicKind::Vector:
		return argIndex >= 1;
	case IntrinsicKind::Matrix:
		return argIndex >= 1;
	case IntrinsicKind::AddPointerDepth:
		return argIndex == 1;
	case IntrinsicKind::Call:
		return argIndex == 3;
	case IntrinsicKind::Property:
		return argIndex == 2; // field name token
	default:
		return false;
	}
}

#undef DYNLEX_INTRINSIC_FIXED_TABLE
#undef DYNLEX_INTRINSIC_RANGED_TABLE
