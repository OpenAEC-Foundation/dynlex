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
	int argCount; // expected argument count (-1 = variadic)
	IntrinsicReturnKind returnKind;
};

// Central registry of all intrinsic signatures.
// Adding a new math function: add here + add LLVM mapping in codegen.cpp.
// clang-format off
inline const std::unordered_map<std::string, IntrinsicInfo> &intrinsicRegistry() {
	static const std::unordered_map<std::string, IntrinsicInfo> registry = {
		// Binary arithmetic
		{"add",                    {2, IntrinsicReturnKind::SameAsArgs}},
		{"subtract",               {2, IntrinsicReturnKind::SameAsArgs}},
		{"multiply",               {2, IntrinsicReturnKind::SameAsArgs}},
		{"divide",                 {2, IntrinsicReturnKind::SameAsArgs}},
		{"modulo",                 {2, IntrinsicReturnKind::SameAsArgs}},
		// Unary arithmetic
		{"negate",                 {1, IntrinsicReturnKind::SameAsArgs}},
		// Math functions
		{"sin",                    {1, IntrinsicReturnKind::SameAsArgs}},
		{"cos",                    {1, IntrinsicReturnKind::SameAsArgs}},
		{"sqrt",                   {1, IntrinsicReturnKind::SameAsArgs}},
		{"abs",                    {1, IntrinsicReturnKind::SameAsArgs}},
		{"floor",                  {1, IntrinsicReturnKind::SameAsArgs}},
		{"ceil",                   {1, IntrinsicReturnKind::SameAsArgs}},
		{"round",                  {1, IntrinsicReturnKind::SameAsArgs}},
		{"exp",                    {1, IntrinsicReturnKind::SameAsArgs}},
		{"log",                    {1, IntrinsicReturnKind::SameAsArgs}},
		{"pow",                    {2, IntrinsicReturnKind::SameAsArgs}},
		{"atan2",                  {2, IntrinsicReturnKind::SameAsArgs}},
		{"min",                    {2, IntrinsicReturnKind::SameAsArgs}},
		{"max",                    {2, IntrinsicReturnKind::SameAsArgs}},
		// Comparisons
		{"less than",              {2, IntrinsicReturnKind::Bool}},
		{"greater than",           {2, IntrinsicReturnKind::Bool}},
		{"equal",                  {2, IntrinsicReturnKind::Bool}},
		{"not equal",              {2, IntrinsicReturnKind::Bool}},
		{"less than or equal",     {2, IntrinsicReturnKind::Bool}},
		{"greater than or equal",  {2, IntrinsicReturnKind::Bool}},
		// Logical
		{"and",                    {2, IntrinsicReturnKind::Bool}},
		{"or",                     {2, IntrinsicReturnKind::Bool}},
		{"not",                    {1, IntrinsicReturnKind::Bool}},
		// Side effects
		{"store",                  {2, IntrinsicReturnKind::Void}},
		{"store at",               {2, IntrinsicReturnKind::Void}},
		{"loop while",             {1, IntrinsicReturnKind::Void}},
		{"if",                     {1, IntrinsicReturnKind::Void}},
		{"else if",                {1, IntrinsicReturnKind::Void}},
		{"else",                   {0, IntrinsicReturnKind::Void}},
		{"switch",                 {1, IntrinsicReturnKind::Void}},
		{"case",                   {1, IntrinsicReturnKind::Void}},
		{"shader_output",          {4, IntrinsicReturnKind::Void}},
		// Shader I/O
		{"shader_input",           {1, IntrinsicReturnKind::Float}},
		{"extract_element",        {2, IntrinsicReturnKind::Float}},
		// Custom return type logic
		{"address of",             {1, IntrinsicReturnKind::Custom}},
		{"dereference",            {1, IntrinsicReturnKind::Custom}},
		{"load at",                {1, IntrinsicReturnKind::Custom}},
		{"return",                 {-1, IntrinsicReturnKind::Custom}},
		{"call",                   {-1, IntrinsicReturnKind::Custom}},
		{"cast",                   {-1, IntrinsicReturnKind::Custom}},
	};
	return registry;
}
// clang-format on

inline const IntrinsicInfo *findIntrinsic(const std::string &name) {
	auto it = intrinsicRegistry().find(name);
	return it != intrinsicRegistry().end() ? &it->second : nullptr;
}
