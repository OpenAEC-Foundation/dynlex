#pragma once
#include "compileTimeInfo.h"

struct Expression;
struct ParseContext;
struct BindingFrameStack;

// A value observed during inference can depend on mutable runtime storage.
// Only calls with invariant runtime inputs can omit their executable callee.
CompileTimeValue
runtimeConstantCallValue(const ParseContext &context, Expression *expression, const BindingFrameStack &bindings);
