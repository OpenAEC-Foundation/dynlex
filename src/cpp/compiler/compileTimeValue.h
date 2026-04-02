#pragma once
#include "bindingResolution.h"
#include "compileTimeInfo.h"
#include <optional>
#include <string_view>

struct Expression;
struct ParseContext;
struct Instantiation;

bool isCompileTimeKnown(const CompileTimeValue &value);
std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value);
std::optional<DataType> buildInfoValueType(std::string_view key);
CompileTimeValue currentBuildInfoValue(const ParseContext &context, std::string_view key);
std::optional<bool> evaluateTargetIs(const ParseContext &context, std::string_view targetName);
std::optional<bool> evaluateShaderStageIs(const ParseContext &context, std::string_view shaderStageName);
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
