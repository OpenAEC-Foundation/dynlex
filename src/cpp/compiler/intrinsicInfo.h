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

enum class IntrinsicPurityKind {
	Pure,
	Impure,
	Custom,
};

#define DYNLEX_INTRINSIC_FIXED_TABLE(X)                                                                                        \
	X(Add, "add", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Subtract, "subtract", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                               \
	X(Multiply, "multiply", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                               \
	X(Divide, "divide", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                   \
	X(Modulo, "modulo", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                   \
	X(BitwiseAnd, "bitwise and", 3, IntrinsicReturnKind::SameAsInts, 0, 0, IntrinsicPurityKind::Pure)                          \
	X(BitwiseOr, "bitwise or", 3, IntrinsicReturnKind::SameAsInts, 0, 0, IntrinsicPurityKind::Pure)                            \
	X(BitwiseXor, "bitwise xor", 3, IntrinsicReturnKind::SameAsInts, 0, 0, IntrinsicPurityKind::Pure)                          \
	X(ShiftLeft, "shift left", 3, IntrinsicReturnKind::SameAsInts, 0, 0, IntrinsicPurityKind::Pure)                            \
	X(ShiftRight, "shift right", 3, IntrinsicReturnKind::SameAsInts, 0, 0, IntrinsicPurityKind::Pure)                          \
	X(BitwiseNot, "bitwise not", 2, IntrinsicReturnKind::SameAsInts, 0, 0, IntrinsicPurityKind::Pure)                          \
	X(Negate, "negate", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                   \
	X(Sin, "sin", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Cos, "cos", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Sqrt, "sqrt", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                       \
	X(Abs, "abs", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Floor, "floor", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                     \
	X(Ceil, "ceil", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                       \
	X(Round, "round", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                     \
	X(Exp, "exp", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Log, "log", 2, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Pow, "pow", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Atan2, "atan2", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                     \
	X(Min, "min", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Max, "max", 3, IntrinsicReturnKind::SameAsArgs, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(LessThan, "less than", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                                    \
	X(GreaterThan, "greater than", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                              \
	X(Equal, "equal", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                                           \
	X(NotEqual, "not equal", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                                    \
	X(LessThanOrEqual, "less than or equal", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                    \
	X(GreaterThanOrEqual, "greater than or equal", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)              \
	X(And, "and", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                                               \
	X(Or, "or", 3, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                                                 \
	X(Not, "not", 2, IntrinsicReturnKind::Bool, 0, 0, IntrinsicPurityKind::Pure)                                               \
	X(SetSubject, "set subject", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Impure)                              \
	X(Subject, "subject", 1, IntrinsicReturnKind::Custom, 0, 0, IntrinsicPurityKind::Pure)                                     \
	X(LifecycleValue, "lifecycle value", 1, IntrinsicReturnKind::Custom, 0, 0, IntrinsicPurityKind::Pure)                      \
	X(Discard, "discard", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                       \
	X(Store, "store", 3, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Custom)                                         \
	X(StoreAt, "store at", 3, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Impure)                                    \
	X(InitializeAt, "initialize at", 3, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Impure)                          \
	X(DestroyAt, "destroy at", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Impure)                                \
	X(LoopWhile, "loop while", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                  \
	X(ExecuteBody, "execute body", 1, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                              \
	X(If, "if", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                                 \
	X(ElseIf, "else if", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                        \
	X(Else, "else", 1, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                             \
	X(Switch, "switch", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                         \
	X(Case, "case", 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                             \
	X(DefaultCase, "default case", 1, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                              \
	X(ShaderOutput, "shader output", 5, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Impure)                          \
	X(ShaderInput, "shader input", 2, IntrinsicReturnKind::Float, 0, 0, IntrinsicPurityKind::Impure)                           \
	X(ShaderUniform, "shader uniform", 2, IntrinsicReturnKind::Float, 0, 0, IntrinsicPurityKind::Impure)                       \
	X(ExtractElement, "extract element", 3, IntrinsicReturnKind::Float, 0, 0, IntrinsicPurityKind::Pure)                       \
	X(Function, "function", 2, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)                                   \
	X(AddressOf, "address of", 2, IntrinsicReturnKind::Custom, 0, 0, IntrinsicPurityKind::Impure)                              \
	X(Dereference, "dereference", 2, IntrinsicReturnKind::Custom, 0, 0, IntrinsicPurityKind::Impure)                           \
	X(CommandLineArgumentCount, "command line argument count", 1, IntrinsicReturnKind::Custom, 0, 0,                           \
	  IntrinsicPurityKind::Impure)                                                                                             \
	X(CommandLineArgumentValues, "command line argument values", 1, IntrinsicReturnKind::Custom, 0, 0,                         \
	  IntrinsicPurityKind::Impure)                                                                                             \
	X(Property, "property", 3, IntrinsicReturnKind::Custom, 2, 2, IntrinsicPurityKind::Custom)                                 \
	X(Cast, "cast", 3, IntrinsicReturnKind::Custom, 2, 2, IntrinsicPurityKind::Pure)                                           \
	X(TypeOf, "type of", 2, IntrinsicReturnKind::Custom, 0, 0, IntrinsicPurityKind::Pure)                                      \
	X(SizeOf, "size of", 2, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)                                      \
	X(BuildInfo, "build info", 2, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)                                \
	X(TargetIs, "target is", 2, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)                                  \
	X(ShaderStageIs, "shader stage is", 2, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)                       \
	X(Select, "select", 4, IntrinsicReturnKind::Custom, 0, 0, IntrinsicPurityKind::Pure)                                       \
	X(AddPointerDepth, "add pointer depth", 2, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)                   \
	X(Fix, "fix", 2, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)

#define DYNLEX_INTRINSIC_RANGED_TABLE(X)                                                                                       \
	X(Construct, "construct", 2, -1, IntrinsicReturnKind::Custom, 1, 1, IntrinsicPurityKind::Pure)                             \
	X(Return, "return", 1, 2, IntrinsicReturnKind::Void, 0, 0, IntrinsicPurityKind::Pure)                                      \
	X(Call, "call", 4, -1, IntrinsicReturnKind::Custom, 1, 3, IntrinsicPurityKind::Impure)                                     \
	X(VariadicCall, "variadic call", 5, -1, IntrinsicReturnKind::Custom, 1, 4, IntrinsicPurityKind::Impure)                    \
	X(Type, "type", 2, 3, IntrinsicReturnKind::Custom, 1, -1, IntrinsicPurityKind::Pure)                                       \
	X(Array, "array", 2, 3, IntrinsicReturnKind::Custom, 1, -1, IntrinsicPurityKind::Pure)                                     \
	X(Vector, "vector", 2, 3, IntrinsicReturnKind::Custom, 1, -1, IntrinsicPurityKind::Pure)                                   \
	X(Matrix, "matrix", 3, 4, IntrinsicReturnKind::Custom, 1, -1, IntrinsicPurityKind::Pure)

enum class IntrinsicKind {
	Unknown,
#define DYNLEX_INTRINSIC_KIND_ENUM_FIXED(kind, name, minArgCount, returnKind, compileTimeArgMin, compileTimeArgMax, purity)    \
	kind,
#define DYNLEX_INTRINSIC_KIND_ENUM_RANGED(                                                                                     \
	kind, name, minArgCount, maxArgCount, returnKind, compileTimeArgMin, compileTimeArgMax, purity                             \
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
	IntrinsicPurityKind purity;

	constexpr IntrinsicInfo(
		int minArgCount, IntrinsicReturnKind returnKind, IntrinsicKind kind, int compileTimeArgMin = 0,
		int compileTimeArgMax = 0, IntrinsicPurityKind purity = IntrinsicPurityKind::Impure
	)
		: minArgCount(minArgCount), maxArgCount(minArgCount), returnKind(returnKind), kind(kind),
		  compileTimeArgMin(compileTimeArgMin), compileTimeArgMax(compileTimeArgMax), purity(purity) {}

	constexpr IntrinsicInfo(
		int minArgCount, int maxArgCount, IntrinsicReturnKind returnKind, IntrinsicKind kind, int compileTimeArgMin = 0,
		int compileTimeArgMax = 0, IntrinsicPurityKind purity = IntrinsicPurityKind::Impure
	)
		: minArgCount(minArgCount), maxArgCount(maxArgCount), returnKind(returnKind), kind(kind),
		  compileTimeArgMin(compileTimeArgMin), compileTimeArgMax(compileTimeArgMax), purity(purity) {}
};

enum class ArithmeticIntrinsicKind { None, Add, Subtract, Multiply, Divide, Modulo };

// Central registry of all intrinsic signatures.
// Argument count includes the intrinsic name argument (e.g. @intrinsic("add", a, b) -> count=3).
// Intrinsic argument indexing is always:
//   arguments[0] = intrinsic name literal (e.g. "add")
//   arguments[1..] = user-supplied arguments
inline const std::unordered_map<std::string, IntrinsicInfo> &intrinsicRegistry() {
	static const std::unordered_map<std::string, IntrinsicInfo> registry = {
#define DYNLEX_INTRINSIC_REG_ENTRY_FIXED(kind, name, minArgCount, returnKind, compileTimeArgMin, compileTimeArgMax, purity)    \
	{name, {minArgCount, returnKind, IntrinsicKind::kind, compileTimeArgMin, compileTimeArgMax, purity}},
#define DYNLEX_INTRINSIC_REG_ENTRY_RANGED(                                                                                     \
	kind, name, minArgCount, maxArgCount, returnKind, compileTimeArgMin, compileTimeArgMax, purity                             \
)                                                                                                                              \
	{name, {minArgCount, maxArgCount, returnKind, IntrinsicKind::kind, compileTimeArgMin, compileTimeArgMax, purity}},
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

constexpr bool isExternalCallIntrinsicKind(IntrinsicKind kind) {
	return kind == IntrinsicKind::Call || kind == IntrinsicKind::VariadicCall;
}

constexpr size_t externalCallRuntimeArgumentStart(IntrinsicKind kind) { return kind == IntrinsicKind::VariadicCall ? 5 : 4; }

inline bool intrinsicArgumentIsCompileTimeOnly(const std::string &name, int argIndex) {
	const IntrinsicInfo *info = findIntrinsic(name);
	if (!info || info->compileTimeArgMin == 0 || argIndex < info->compileTimeArgMin)
		return false;
	return info->compileTimeArgMax < 0 || argIndex <= info->compileTimeArgMax;
}

constexpr IntrinsicPurityKind intrinsicPurityKind(IntrinsicKind kind) {
	switch (kind) {
#define DYNLEX_INTRINSIC_PURE_SWITCH_FIXED(kind, name, minArgCount, returnKind, compileTimeArgMin, compileTimeArgMax, purity)  \
	case IntrinsicKind::kind:                                                                                                  \
		return purity;
#define DYNLEX_INTRINSIC_PURE_SWITCH_RANGED(                                                                                   \
	kind, name, minArgCount, maxArgCount, returnKind, compileTimeArgMin, compileTimeArgMax, purity                             \
)                                                                                                                              \
	case IntrinsicKind::kind:                                                                                                  \
		return purity;
		DYNLEX_INTRINSIC_FIXED_TABLE(DYNLEX_INTRINSIC_PURE_SWITCH_FIXED)
		DYNLEX_INTRINSIC_RANGED_TABLE(DYNLEX_INTRINSIC_PURE_SWITCH_RANGED)
#undef DYNLEX_INTRINSIC_PURE_SWITCH_FIXED
#undef DYNLEX_INTRINSIC_PURE_SWITCH_RANGED
	default:
		return IntrinsicPurityKind::Impure;
	}
}

constexpr bool isAlwaysPureIntrinsicKind(IntrinsicKind kind) { return intrinsicPurityKind(kind) == IntrinsicPurityKind::Pure; }

static_assert(isAlwaysPureIntrinsicKind(IntrinsicKind::Multiply));
static_assert(isAlwaysPureIntrinsicKind(IntrinsicKind::Return));
static_assert(intrinsicPurityKind(IntrinsicKind::Store) == IntrinsicPurityKind::Custom);
static_assert(intrinsicPurityKind(IntrinsicKind::Property) == IntrinsicPurityKind::Custom);
static_assert(intrinsicPurityKind(IntrinsicKind::Call) == IntrinsicPurityKind::Impure);

#undef DYNLEX_INTRINSIC_FIXED_TABLE
#undef DYNLEX_INTRINSIC_RANGED_TABLE
