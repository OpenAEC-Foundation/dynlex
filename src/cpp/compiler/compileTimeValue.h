#pragma once
#include "bindingResolution.h"
#include "compileTimeInfo.h"
#include "compilerUtils.h"
#include <cstdint>
#include <optional>
#include <string_view>

struct Expression;
struct ParseContext;
struct Instantiation;

bool isCompileTimeKnown(const CompileTimeValue &value);
std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value);
std::optional<std::int64_t> getCompileTimeIntegerValue(const CompileTimeValue &value);
std::optional<DataType> buildInfoValueType(std::string_view key);
CompileTimeValue currentBuildInfoValue(const ParseContext &context, std::string_view key);
std::optional<bool> evaluateTargetIs(const ParseContext &context, std::string_view targetName);
std::optional<bool> evaluateShaderStageIs(const ParseContext &context, std::string_view shaderStageName);
Expression *resolveCompileTimeBinding(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack *outBindingFrameStack = nullptr
);
CompileTimeValue resolveImmediateCompileTimeValue(const Expression *expr);
CompileTimeValue getExpressionCompileTimeValue(
	const ParseContext &context, const Expression *expr, const Instantiation *instantiation = nullptr
);
void setExpressionCompileTimeValue(
	ParseContext &context, Expression *expr, const CompileTimeValue &value, Instantiation *instantiation = nullptr
);
CompileTimeValue resolveStoredCompileTimeValue(
	const ParseContext &context, Expression *expr, const BindingFrameStack &bindingFrameStack = {},
	const Instantiation *instantiation = nullptr
);
bool resolveStoredCompileTimeInteger(
	const ParseContext &context, Expression *expr, const BindingFrameStack &bindingFrameStack, int &outValue,
	const Instantiation *instantiation = nullptr
);

template <typename ReadKnownValueFn>
CompileTimeValue resolveCompileTimeValueFromKnownState(
	Expression *expr, const BindingFrameStack &bindingFrameStack, ReadKnownValueFn &&readKnownValue
) {
	if (!expr)
		crashCompilerBug("compile-time value resolution received null expression");
	Expression *currentExpression = expr;
	BindingFrameStack currentBindingFrameStack = bindingFrameStack;
	constexpr size_t maxResolutionDepth = 256;
	for (size_t depth = 0; currentExpression && depth < maxResolutionDepth; depth++) {
		CompileTimeValue storedValue = readKnownValue(currentExpression);
		if (isCompileTimeKnown(storedValue))
			return storedValue;
		CompileTimeValue immediateValue = resolveImmediateCompileTimeValue(currentExpression);
		if (isCompileTimeKnown(immediateValue))
			return immediateValue;
		BindingFrameStack resolvedBindingFrameStack;
		Expression *resolvedExpression =
			resolveCompileTimeBinding(currentExpression, currentBindingFrameStack, &resolvedBindingFrameStack);
		if (!resolvedExpression || resolvedExpression == currentExpression)
			break;
		currentExpression = resolvedExpression;
		currentBindingFrameStack = std::move(resolvedBindingFrameStack);
	}
	return {};
}
