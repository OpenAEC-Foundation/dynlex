#pragma once
#include "bindingResolution.h"
#include "compileTimeInfo.h"
#include <optional>

struct Expression;
struct ParseContext;
struct Instantiation;

bool isCompileTimeKnown(const CompileTimeValue &value);
std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value);
CompileTimeValue getExpressionCompileTimeValue(
	const ParseContext &context, const Expression *expr, const Instantiation *instantiation = nullptr
);
void setExpressionCompileTimeValue(
	ParseContext &context, Expression *expr, const CompileTimeValue &value, Instantiation *instantiation = nullptr
);
CompileTimeValue evaluateCompileTimeValue(
	Expression *expr, ParseContext &context, const BindingFrameStack &bindingFrameStack = {},
	const Instantiation *instantiation = nullptr
);
