#pragma once
#include "bindingResolution.h"
#include "compileTimeInfo.h"
#include "compilerUtils.h"
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

struct Expression;
struct ParseContext;
struct Instantiation;

bool isCompileTimeKnown(const CompileTimeValue &value);
std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value);
std::optional<std::int64_t> getCompileTimeIntegerValue(const CompileTimeValue &value);
std::optional<double> getCompileTimeNumericValue(const CompileTimeValue &value);
std::optional<TypeReferenceValue> getCompileTimeTypeReferenceValue(const CompileTimeValue &value);
std::optional<TypeConstraint> getCompileTimeConstraintValue(const CompileTimeValue &value);
bool readTypeConstraintValue(
	const CompileTimeValue &value, const DataType &expressionType, TypeConstraint &outConstraint, DataType &outParameterType
);
std::optional<DataType> buildInfoValueType(std::string_view key);
CompileTimeValue currentBuildInfoValue(const ParseContext &context, std::string_view key);
std::optional<bool> evaluateTargetIs(const ParseContext &context, std::string_view targetName);
std::optional<bool> evaluateShaderStageIs(const ParseContext &context, std::string_view shaderStageName);
CompileTimeValue resolveImmediateCompileTimeValue(const Expression *expr);
CompileTimeValue getExpressionCompileTimeValue(const Expression *expr);
void setExpressionCompileTimeValue(Expression *expr, const CompileTimeValue &value);

template <typename ResolveBindingLayersFn, typename LookupValueFn>
CompileTimeValue resolveStoredCompileTimeValueWith(
	Expression *expression, ResolveBindingLayersFn resolveBindingLayers, LookupValueFn lookupValue
) {
	requireCompilerInvariant(expression != nullptr, "compile-time value resolution received null expression");
	CompileTimeValue knownValue;
	ResolvedBindingLayers resolved = resolveBindingLayers([&](Expression *currentExpression) {
		knownValue = lookupValue(currentExpression);
		return isCompileTimeKnown(knownValue);
	});
	if (isCompileTimeKnown(knownValue))
		return knownValue;
	return resolved.expression ? resolveImmediateCompileTimeValue(resolved.expression) : CompileTimeValue{};
}

bool narrowCompileTimeInteger(const CompileTimeValue &value, int &outValue);
CompileTimeValue resolveStoredCompileTimeValue(Expression *expr, const BindingFrameStack &bindingFrameStack = {});
bool resolveStoredCompileTimeInteger(Expression *expr, const BindingFrameStack &bindingFrameStack, int &outValue);
