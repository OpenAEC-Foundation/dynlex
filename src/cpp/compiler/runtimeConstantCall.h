#pragma once
#include "compileTimeInfo.h"

struct Expression;
struct BindingFrameStack;

// A value observed during inference can depend on mutable runtime storage.
// Only calls with invariant runtime inputs can omit their executable callee.
CompileTimeValue runtimeConstantCallValue(Expression *expression, const BindingFrameStack &bindings);
