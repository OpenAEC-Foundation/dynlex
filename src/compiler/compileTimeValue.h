#pragma once
#include "bindingResolution.h"
#include "compileTimeInfo.h"
#include <optional>

struct Function;
struct ParseContext;
struct Instantiation;

bool isCompileTimeKnown(const CompileTimeValue &value);
std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value);
CompileTimeValue evaluateCompileTimeValue(
	Function *expr, ParseContext &context, const BindingFrameStack &bindingFrameStack = {},
	const Instantiation *instantiation = nullptr
);
