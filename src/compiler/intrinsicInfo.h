#pragma once
#include <string>
#include <unordered_map>

// Describes how an intrinsic's return type relates to its arguments
enum class IntrinsicReturnKind {
	SameAsArgs, // return type = promoted type of arguments (arithmetic, math)
	Bool,		// returns boolean
	Void,		// no return value
	Float,		// always returns float (shader I/O)
	Custom,		// special handling required
};

struct IntrinsicInfo {
	int argCount; // expected argument count including name (-1 = variadic)
	IntrinsicReturnKind returnKind;
};

// Central registry of all intrinsic signatures.
// argCount includes the name argument (e.g. @intrinsic("add", a, b) → argCount=3).
// Adding a new math function: add here + add LLVM mapping in codegen.cpp.
// clang-format off
inline const std::unordered_map<std::string, IntrinsicInfo> &intrinsicRegistry() {
	static const std::unordered_map<std::string, IntrinsicInfo> registry = {
		// Binary arithmetic: @intrinsic("op", left, right)
		{"add",                    {3, IntrinsicReturnKind::SameAsArgs}},
		{"subtract",               {3, IntrinsicReturnKind::SameAsArgs}},
		{"multiply",               {3, IntrinsicReturnKind::SameAsArgs}},
		{"divide",                 {3, IntrinsicReturnKind::SameAsArgs}},
		{"modulo",                 {3, IntrinsicReturnKind::SameAsArgs}},
		// Unary arithmetic: @intrinsic("op", val)
		{"negate",                 {2, IntrinsicReturnKind::SameAsArgs}},
		// Math functions (unary): @intrinsic("fn", val)
		{"sin",                    {2, IntrinsicReturnKind::SameAsArgs}},
		{"cos",                    {2, IntrinsicReturnKind::SameAsArgs}},
		{"sqrt",                   {2, IntrinsicReturnKind::SameAsArgs}},
		{"abs",                    {2, IntrinsicReturnKind::SameAsArgs}},
		{"floor",                  {2, IntrinsicReturnKind::SameAsArgs}},
		{"ceil",                   {2, IntrinsicReturnKind::SameAsArgs}},
		{"round",                  {2, IntrinsicReturnKind::SameAsArgs}},
		{"exp",                    {2, IntrinsicReturnKind::SameAsArgs}},
		{"log",                    {2, IntrinsicReturnKind::SameAsArgs}},
		// Math functions (binary): @intrinsic("fn", a, b)
		{"pow",                    {3, IntrinsicReturnKind::SameAsArgs}},
		{"atan2",                  {3, IntrinsicReturnKind::SameAsArgs}},
		{"min",                    {3, IntrinsicReturnKind::SameAsArgs}},
		{"max",                    {3, IntrinsicReturnKind::SameAsArgs}},
		// Comparisons: @intrinsic("op", left, right)
		{"less than",              {3, IntrinsicReturnKind::Bool}},
		{"greater than",           {3, IntrinsicReturnKind::Bool}},
		{"equal",                  {3, IntrinsicReturnKind::Bool}},
		{"not equal",              {3, IntrinsicReturnKind::Bool}},
		{"less than or equal",     {3, IntrinsicReturnKind::Bool}},
		{"greater than or equal",  {3, IntrinsicReturnKind::Bool}},
		// Logical: @intrinsic("op", left, right) or @intrinsic("not", val)
		{"and",                    {3, IntrinsicReturnKind::Bool}},
		{"or",                     {3, IntrinsicReturnKind::Bool}},
		{"not",                    {2, IntrinsicReturnKind::Bool}},
		// Side effects
		{"discard",                {2, IntrinsicReturnKind::Void}},       // @intrinsic("discard", val)
		{"store",                  {3, IntrinsicReturnKind::Void}},       // @intrinsic("store", dest, val)
		{"store at",               {4, IntrinsicReturnKind::Void}},       // @intrinsic("store at", ptr, index, val)
		{"loop while",             {2, IntrinsicReturnKind::Void}},       // @intrinsic("loop while", cond)
		{"if",                     {2, IntrinsicReturnKind::Void}},       // @intrinsic("if", cond)
		{"else if",                {2, IntrinsicReturnKind::Void}},       // @intrinsic("else if", cond)
		{"else",                   {1, IntrinsicReturnKind::Void}},       // @intrinsic("else")
		{"switch",                 {2, IntrinsicReturnKind::Void}},       // @intrinsic("switch", val)
		{"case",                   {2, IntrinsicReturnKind::Void}},       // @intrinsic("case", val)
		{"shader output",          {5, IntrinsicReturnKind::Void}},       // @intrinsic("shader output", r, g, b, a)
		// Shader I/O
		{"shader input",           {2, IntrinsicReturnKind::Float}},      // @intrinsic("shader input", name)
		{"shader uniform",         {2, IntrinsicReturnKind::Float}},      // @intrinsic("shader uniform", name)
		{"extract element",        {3, IntrinsicReturnKind::Float}},      // @intrinsic("extract element", vec, idx)
		// Custom return type logic
		{"address of",             {2, IntrinsicReturnKind::Custom}},     // @intrinsic("address of", var)
		{"dereference",            {2, IntrinsicReturnKind::Custom}},     // @intrinsic("dereference", ptr)
		{"load at",                {3, IntrinsicReturnKind::Custom}},     // @intrinsic("load at", ptr, index)
		{"construct",              {-1, IntrinsicReturnKind::Custom}},    // @intrinsic("construct", type, fields...)
		{"property",               {3, IntrinsicReturnKind::Custom}},     // @intrinsic("property", instance, fieldname)
		{"return",                 {-1, IntrinsicReturnKind::Custom}},    // @intrinsic("return"[, val])
		{"call",                   {-1, IntrinsicReturnKind::Custom}},    // @intrinsic("call", lib, func, rettype, args...)
		{"cast",                   {3, IntrinsicReturnKind::Custom}},     // @intrinsic("cast", val, type)
		{"type",                   {-1, IntrinsicReturnKind::Custom}},    // @intrinsic("type", kind[, bits])
		{"type of",                {2, IntrinsicReturnKind::Custom}},     // @intrinsic("type of", value)
		{"build info",             {2, IntrinsicReturnKind::Custom}},     // @intrinsic("build info", key)
		{"select",                 {4, IntrinsicReturnKind::Custom}},     // @intrinsic("select", condition, when_true, when_false)
		{"array",                  {-1, IntrinsicReturnKind::Custom}},    // @intrinsic("array", size[, type])
		{"vector",                 {-1, IntrinsicReturnKind::Custom}},    // @intrinsic("vector", size[, type])
		{"matrix",                 {-1, IntrinsicReturnKind::Custom}},    // @intrinsic("matrix", rows, cols[, type])
		{"add pointer depth",      {2, IntrinsicReturnKind::Custom}},     // @intrinsic("add pointer depth", type)
	};
	return registry;
}
// clang-format on

inline const IntrinsicInfo *findIntrinsic(const std::string &name) {
	auto it = intrinsicRegistry().find(name);
	return it != intrinsicRegistry().end() ? &it->second : nullptr;
}

inline bool intrinsicArgumentIsCompileTimeOnly(const std::string &name, int argIndex) {
	if (name == "construct")
		return argIndex == 1;
	if (name == "cast")
		return argIndex == 2;
	if (name == "type")
		return argIndex >= 1;
	if (name == "array")
		return argIndex >= 1;
	if (name == "vector")
		return argIndex >= 1;
	if (name == "matrix")
		return argIndex >= 1;
	if (name == "add pointer depth")
		return argIndex == 1;
	if (name == "call")
		return argIndex == 3;
	return false;
}
