#pragma once
#include "compileTimeInfo.h"
#include <optional>
#include <unordered_map>

struct Function;
struct ParseContext;
struct Instantiation;

bool isCompileTimeKnown(const CompileTimeValue &value);
std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value);
CompileTimeValue evaluateCompileTimeValue(
	Function *expr, ParseContext &context, const std::unordered_map<std::string, Function *> &bindings = {},
	const Instantiation *instantiation = nullptr
);
