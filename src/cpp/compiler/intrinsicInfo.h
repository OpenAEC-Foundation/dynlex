#pragma once
#include <string>
#include <unordered_map>

// Describes how an intrinsic's return type relates to its arguments
enum class IntrinsicReturnKind {
	SameAsArgs, // return type = promoted type of arguments (arithmetic, math)
	SameAsInts, // return type = promoted integer type of arguments (bitwise)
	Bool,		// returns boolean
	Void,		// no return value
	Float,		// always returns float (shader I/O)
	Custom,		// special handling required
};

#define DYNLEX_INTRINSIC_FIXED_TABLE(X)                                                                                        \
	X(Add, "add", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Subtract, "subtract", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                          \
	X(Multiply, "multiply", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                          \
	X(Divide, "divide", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                              \
	X(Modulo, "modulo", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                              \
	X(BitwiseAnd, "bitwise and", 3, IntrinsicReturnKind::SameAsInts, 0, 0)                                                     \
	X(BitwiseOr, "bitwise or", 3, IntrinsicReturnKind::SameAsInts, 0, 0)                                                       \
	X(BitwiseXor, "bitwise xor", 3, IntrinsicReturnKind::SameAsInts, 0, 0)                                                     \
	X(ShiftLeft, "shift left", 3, IntrinsicReturnKind::SameAsInts, 0, 0)                                                       \
	X(ShiftRight, "shift right", 3, IntrinsicReturnKind::SameAsInts, 0, 0)                                                     \
	X(BitwiseNot, "bitwise not", 2, IntrinsicReturnKind::SameAsInts, 0, 0)                                                     \
	X(Negate, "negate", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                              \
	X(Sin, "sin", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Cos, "cos", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Sqrt, "sqrt", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                  \
	X(Abs, "abs", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Floor, "floor", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                \
	X(Ceil, "ceil", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                  \
	X(Round, "round", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                \
	X(Exp, "exp", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Log, "log", 2, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Pow, "pow", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Atan2, "atan2", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                \
	X(Min, "min", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(Max, "max", 3, IntrinsicReturnKind::SameAsArgs, 0, 0)                                                                    \
	X(LessThan, "less than", 3, IntrinsicReturnKind::Bool, 0, 0)                                                               \
	X(GreaterThan, "greater than", 3, IntrinsicReturnKind::Bool, 0, 0)                                                         \
	X(Equal, "equal", 3, IntrinsicReturnKind::Bool, 0, 0)                                                                      \
	X(NotEqual, "not equal", 3, IntrinsicReturnKind::Bool, 0, 0)                                                               \
	X(LessThanOrEqual, "less than or equal", 3, IntrinsicReturnKind::Bool, 0, 0)                                               \
	X(GreaterThanOrEqual, "greater than or equal", 3, IntrinsicReturnKind::Bool, 0, 0)                                         \
	X(And, "and", 3, IntrinsicReturnKind::Bool, 0, 0)                                                                          \
	X(Or, "or", 3, IntrinsicReturnKind::Bool, 0, 0)                                                                            \
	X(Not, "not", 2, IntrinsicReturnKind::Bool, 0, 0)                                                                          \
	X(Discard, "discard", 2, IntrinsicReturnKind::Void, 0, 0)                                                                  \
	X(Store, "store", 3, IntrinsicReturnKind::Void, 0, 0)                                                                      \
	X(StoreAt, "store at", 4, IntrinsicReturnKind::Void, 0, 0)                                                                 \
	X(LoopWhile, "loop while", 2, IntrinsicReturnKind::Void, 0, 0)                                                             \
	X(ExecuteBody, "execute body", 1, IntrinsicReturnKind::Void, 0, 0)                                                         \
	X(If, "if", 2, IntrinsicReturnKind::Void, 0, 0)                                                                            \
	X(ElseIf, "else if", 2, IntrinsicReturnKind::Void, 0, 0)                                                                   \
	X(Else, "else", 1, IntrinsicReturnKind::Void, 0, 0)                                                                        \
	X(Switch, "switch", 2, IntrinsicReturnKind::Void, 0, 0)                                                                    \
	X(Case, "case", 2, IntrinsicReturnKind::Void, 0, 0)                                                                        \
	X(ShaderOutput, "shader output", 5, IntrinsicReturnKind::Void, 0, 0)                                                       \
	X(ShaderInput, "shader input", 2, IntrinsicReturnKind::Float, 0, 0)                                                        \
	X(ShaderUniform, "shader uniform", 2, IntrinsicReturnKind::Float, 0, 0)                                                    \
	X(ExtractElement, "extract element", 3, IntrinsicReturnKind::Float, 0, 0)                                                  \
	X(Function, "function", 2, IntrinsicReturnKind::Custom, 1, 1)                                                              \
	X(AddressOf, "address of", 2, IntrinsicReturnKind::Custom, 0, 0)                                                           \
	X(Dereference, "dereference", 2, IntrinsicReturnKind::Custom, 0, 0)                                                        \
	X(LoadAt, "load at", 3, IntrinsicReturnKind::Custom, 0, 0)                                                                 \
	X(Property, "property", 3, IntrinsicReturnKind::Custom, 2, 2)                                                              \
	X(Cast, "cast", 3, IntrinsicReturnKind::Custom, 2, 2)                                                                      \
	X(TypeOf, "type of", 2, IntrinsicReturnKind::Custom, 0, 0)                                                                 \
	X(SizeOf, "size of", 2, IntrinsicReturnKind::Custom, 1, 1)                                                                 \
	X(BuildInfo, "build info", 2, IntrinsicReturnKind::Custom, 1, 1)                                                           \
	X(TargetIs, "target is", 2, IntrinsicReturnKind::Custom, 1, 1)                                                             \
	X(ShaderStageIs, "shader stage is", 2, IntrinsicReturnKind::Custom, 1, 1)                                                  \
	X(Select, "select", 4, IntrinsicReturnKind::Custom, 0, 0)                                                                  \
	X(AddPointerDepth, "add pointer depth", 2, IntrinsicReturnKind::Custom, 1, 1)

#define DYNLEX_INTRINSIC_RANGED_TABLE(X)                                                                                       \
	X(Construct, "construct", 2, -1, IntrinsicReturnKind::Custom, 1, 1)                                                        \
	X(Return, "return", 1, 2, IntrinsicReturnKind::Void, 0, 0)                                                                 \
	X(Call, "call", 4, -1, IntrinsicReturnKind::Custom, 3, 3)                                                                  \
	X(Type, "type", 2, 3, IntrinsicReturnKind::Custom, 1, -1)                                                                  \
	X(Array, "array", 2, 3, IntrinsicReturnKind::Custom, 1, -1)                                                                \
	X(Vector, "vector", 2, 3, IntrinsicReturnKind::Custom, 1, -1)                                                              \
	X(Matrix, "matrix", 3, 4, IntrinsicReturnKind::Custom, 1, -1)

enum class IntrinsicKind {
	Unknown,
#define DYNLEX_INTRINSIC_KIND_ENUM_FIXED(kind, name, minArgCount, returnKind, compileTimeArgMin, compileTimeArgMax) kind,
#define DYNLEX_INTRINSIC_KIND_ENUM_RANGED(                                                                                     \
	kind, name, minArgCount, maxArgCount, returnKind, compileTimeArgMin, compileTimeArgMax                                     \
)                                                                                                                              \
	kind,
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
	int compileTimeArgMin; // 0 = none, otherwise first compile-time-only argument index
	int compileTimeArgMax; // inclusive, -1 = unbounded from compileTimeArgMin

	constexpr IntrinsicInfo(
		int minArgCount, IntrinsicReturnKind returnKind, IntrinsicKind kind, int compileTimeArgMin = 0,
		int compileTimeArgMax = 0
	)
		: minArgCount(minArgCount), maxArgCount(minArgCount), returnKind(returnKind), kind(kind),
		  compileTimeArgMin(compileTimeArgMin), compileTimeArgMax(compileTimeArgMax) {}

	constexpr IntrinsicInfo(
		int minArgCount, int maxArgCount, IntrinsicReturnKind returnKind, IntrinsicKind kind, int compileTimeArgMin = 0,
		int compileTimeArgMax = 0
	)
		: minArgCount(minArgCount), maxArgCount(maxArgCount), returnKind(returnKind), kind(kind),
		  compileTimeArgMin(compileTimeArgMin), compileTimeArgMax(compileTimeArgMax) {}
};

enum class ArithmeticIntrinsicKind { None, Add, Subtract, Multiply, Divide, Modulo };

// Central registry of all intrinsic signatures.
// Argument count includes the intrinsic name argument (e.g. @intrinsic("add", a, b) -> count=3).
// Intrinsic argument indexing is always:
//   arguments[0] = intrinsic name literal (e.g. "add")
//   arguments[1..] = user-supplied arguments
inline const std::unordered_map<std::string, IntrinsicInfo> &intrinsicRegistry() {
	static const std::unordered_map<std::string, IntrinsicInfo> registry = {
#define DYNLEX_INTRINSIC_REG_ENTRY_FIXED(kind, name, minArgCount, returnKind, compileTimeArgMin, compileTimeArgMax)            \
	{name, {minArgCount, returnKind, IntrinsicKind::kind, compileTimeArgMin, compileTimeArgMax}},
#define DYNLEX_INTRINSIC_REG_ENTRY_RANGED(                                                                                     \
	kind, name, minArgCount, maxArgCount, returnKind, compileTimeArgMin, compileTimeArgMax                                     \
)                                                                                                                              \
	{name, {minArgCount, maxArgCount, returnKind, IntrinsicKind::kind, compileTimeArgMin, compileTimeArgMax}},
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
	const IntrinsicInfo *info = findIntrinsic(name);
	if (!info || info->compileTimeArgMin == 0 || argIndex < info->compileTimeArgMin)
		return false;
	return info->compileTimeArgMax < 0 || argIndex <= info->compileTimeArgMax;
}

#undef DYNLEX_INTRINSIC_FIXED_TABLE
#undef DYNLEX_INTRINSIC_RANGED_TABLE
