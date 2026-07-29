#include "compileTimeValue.h"
#include "expression.h"
#include "numericLiteral.h"
#include "parseContext.h"
#include "pattern/pattern_tree/patternElement.h"
#include <cmath>
#include <cstdint>
#include <limits>

bool isCompileTimeKnown(const CompileTimeValue &value) { return !std::holds_alternative<std::monostate>(value); }

std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value) {
	if (auto *boolean = std::get_if<bool>(&value))
		return *boolean;
	if (auto *integer = std::get_if<std::int64_t>(&value))
		return *integer != 0;
	if (auto *number = std::get_if<double>(&value))
		return *number != 0.0;
	if (auto *text = std::get_if<std::string>(&value))
		return !text->empty();
	return std::nullopt;
}

std::optional<std::int64_t> getCompileTimeIntegerValue(const CompileTimeValue &value) {
	if (auto *integer = std::get_if<std::int64_t>(&value))
		return *integer;
	auto *number = std::get_if<double>(&value);
	if (!number || !std::isfinite(*number))
		return std::nullopt;
	double truncated = std::trunc(*number);
	if (*number != truncated)
		return std::nullopt;
	constexpr std::uint64_t maxExactMagnitude = std::uint64_t{1} << std::numeric_limits<double>::digits;
	if (truncated < -static_cast<double>(maxExactMagnitude) || truncated > static_cast<double>(maxExactMagnitude))
		return std::nullopt;
	if (truncated < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
		truncated > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
		return std::nullopt;
	}
	return static_cast<std::int64_t>(truncated);
}

std::optional<double> getCompileTimeNumericValue(const CompileTimeValue &value) {
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return static_cast<double>(*integer);
	if (const auto *floatingPoint = std::get_if<double>(&value))
		return *floatingPoint;
	return std::nullopt;
}

std::optional<TypeReferenceValue> getCompileTimeTypeReferenceValue(const CompileTimeValue &value) {
	if (const auto *typeReference = std::get_if<TypeReferenceValue>(&value))
		return typeReference->type.kind == DataType::Kind::Type ? std::optional<TypeReferenceValue>(*typeReference)
																: std::nullopt;
	return std::nullopt;
}

std::optional<TypeConstraint> getCompileTimeConstraintValue(const CompileTimeValue &value) {
	if (const auto *constraint = std::get_if<TypeConstraint>(&value))
		return constraint->isResolved() ? std::optional<TypeConstraint>(*constraint) : std::nullopt;
	if (const auto *typeReference = std::get_if<TypeReferenceValue>(&value))
		return typeReference->constraint.isResolved() ? std::optional<TypeConstraint>(typeReference->constraint) : std::nullopt;
	return std::nullopt;
}

bool readTypeConstraintValue(
	const CompileTimeValue &value, const DataType &expressionType, TypeConstraint &outConstraint,
	DataType &outParameterType
) {
	outParameterType = {};
	if (const auto *constraint = std::get_if<TypeConstraint>(&value)) {
		if (!constraint->isResolved())
			return false;
		outConstraint = *constraint;
		outParameterType = constraint->exactValueType().value_or(DataType{});
		return true;
	}
	if (const auto *typeReference = std::get_if<TypeReferenceValue>(&value)) {
		if (!typeReference->constraint.isResolved())
			return false;
		outConstraint = typeReference->constraint;
		if (typeReference->type.kind == DataType::Kind::Type)
			outParameterType = typeReference->type.toReferencedType();
		return true;
	}
	if (expressionType.kind == DataType::Kind::Type &&
		expressionType.referencedKind != DataType::Kind::Unresolved) {
		outConstraint = TypeConstraint::fromTypeReference(expressionType);
		outParameterType = expressionType.toReferencedType();
		return true;
	}
	return false;
}

static std::optional<CompileTimeValue> parseCompileTimeNumericToken(std::string_view token) {
	NumericLiteralParseResult parsed = parseNumericLiteral(token);
	if (!parsed)
		return std::nullopt;
	return numericLiteralCompileTimeValue(parsed.value);
}

CompileTimeValue getExpressionCompileTimeValue(const Expression *expr) {
	requireCompilerInvariant(expr != nullptr, "compile-time value lookup received a null expression");
	return expr->compileTimeValue;
}

void setExpressionCompileTimeValue(Expression *expr, const CompileTimeValue &value) {
	requireCompilerInvariant(expr != nullptr, "compile-time value assignment received a null expression");
	expr->compileTimeValue = value;
}

CompileTimeValue resolveImmediateCompileTimeValue(const Expression *expr) {
	switch (expr->kind) {
	case Expression::Kind::Literal:
		if (const auto *integer = std::get_if<std::int64_t>(&expr->literalValue))
			return *integer;
		if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&expr->literalValue))
			return *minimumMagnitude;
		if (const auto *number = std::get_if<double>(&expr->literalValue)) {
			if (expr->type.kind == DataType::Kind::Bool)
				return *number != 0.0;
			return *number;
		}
		if (const auto *text = std::get_if<std::string>(&expr->literalValue))
			return *text;
		return {};
	case Expression::Kind::Variable:
		if (expr->variable) {
			if (std::optional<CompileTimeValue> numericLiteral = parseCompileTimeNumericToken(expr->variable->name))
				return *numericLiteral;
		}
		if (expr->type.kind == DataType::Kind::Type)
			return TypeReferenceValue::exact(expr->type);
		return {};
	case Expression::Kind::TypedPlaceholder:
		if (expr->type.kind == DataType::Kind::Type)
			return TypeReferenceValue::exact(expr->type);
		return {};
	case Expression::Kind::Pending:
		if (expr->patternReference) {
			auto &elements = expr->patternReference->patternElements;
			if (elements.empty())
				elements = getPatternElements(expr->patternReference->pattern.text);
			if (elements.size() == 1 && (elements[0].type == PatternElement::Type::Variable ||
										 elements[0].type == PatternElement::Type::VariableLike)) {
				if (std::optional<CompileTimeValue> numericLiteral = parseCompileTimeNumericToken(elements[0].text))
					return *numericLiteral;
			}
		}
		if (expr->type.kind == DataType::Kind::Type)
			return TypeReferenceValue::exact(expr->type);
		return {};
	case Expression::Kind::ArrayLiteral:
	case Expression::Kind::IntrinsicCall:
	case Expression::Kind::PatternCall:
		if (expr->type.kind == DataType::Kind::Type)
			return TypeReferenceValue::exact(expr->type);
		return {};
	}
	return {};
}

Expression *resolveCompileTimeBinding(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack *outBindingFrameStack
) {
	if (outBindingFrameStack)
		*outBindingFrameStack = bindingFrameStack;
	if (expr && expr->kind == Expression::Kind::Pending && expr->patternReference) {
		auto &elements = expr->patternReference->patternElements;
		if (elements.empty())
			elements = getPatternElements(expr->patternReference->pattern.text);
		if (elements.size() == 1 &&
			(elements[0].type == PatternElement::Type::Variable || elements[0].type == PatternElement::Type::VariableLike)) {
			BindingFrameStack callerScope;
			if (Expression *boundExpression = bindingFrameStack.lookupWithCallerScope(elements[0].text, expr, callerScope)) {
				if (outBindingFrameStack)
					*outBindingFrameStack = std::move(callerScope);
				return boundExpression;
			}
		}
	}
	if (expr && expr->kind == Expression::Kind::Variable && expr->variable) {
		BindingFrameStack callerScope;
		if (Expression *boundExpression = bindingFrameStack.lookupWithCallerScope(expr->variable, expr, callerScope)) {
			if (outBindingFrameStack)
				*outBindingFrameStack = std::move(callerScope);
			return boundExpression;
		}
	}
	if (expr && expr->inferredFlexExpansion) {
		PatternDefinition *definition = expr->selectedPatternDefinition;
		requireCompilerInvariant(definition, "inferred flex expansion is missing its selected definition");
		if (definition && definition->section && definition->section->isFlex) {
			BindingFrame innerBindings;
			collectPatternCallBindings(expr, definition, innerBindings);
			if (outBindingFrameStack) {
				*outBindingFrameStack = bindingFrameStack;
				pushBindingScope(*outBindingFrameStack, std::move(innerBindings));
			}
		}
		return expr->inferredFlexExpansion;
	}
	return expr;
}

CompileTimeValue resolveStoredCompileTimeValue(Expression *expr, const BindingFrameStack &bindingFrameStack) {
	return resolveCompileTimeValueFromKnownState(expr, bindingFrameStack, [&](Expression *currentExpression) {
		return getExpressionCompileTimeValue(currentExpression);
	});
}

bool resolveStoredCompileTimeInteger(Expression *expr, const BindingFrameStack &bindingFrameStack, int &outValue) {
	std::optional<std::int64_t> integerValue =
		getCompileTimeIntegerValue(resolveStoredCompileTimeValue(expr, bindingFrameStack));
	if (!integerValue.has_value() || *integerValue < std::numeric_limits<int>::min() ||
		*integerValue > std::numeric_limits<int>::max()) {
		return false;
	}
	outValue = static_cast<int>(*integerValue);
	return true;
}

static std::string_view currentBuildTargetName(const ParseContext &context) {
	return context.options.emitSPIRV ? "gpu" : context.options.emitWASM ? "wasm" : "cpu";
}

std::optional<DataType> buildInfoValueType(std::string_view key) {
	if (key == "word size" || key == "c long size" || key == "optimization level")
		return DataType{DataType::Kind::Int, 4};
	return std::nullopt;
}

CompileTimeValue currentBuildInfoValue(const ParseContext &context, std::string_view key) {
	if (key == "word size")
		return (context.options.emitSPIRV || context.options.emitWASM) ? std::int64_t{32}
																	   : static_cast<std::int64_t>(sizeof(void *) * 8);
	if (key == "c long size")
		return (context.options.emitSPIRV || context.options.emitWASM) ? std::int64_t{32}
																	   : static_cast<std::int64_t>(sizeof(long) * 8);
	if (key == "optimization level")
		return static_cast<std::int64_t>(context.options.optimizationLevel);
	return {};
}

std::optional<bool> evaluateTargetIs(const ParseContext &context, std::string_view targetName) {
	if (targetName != "cpu" && targetName != "wasm" && targetName != "gpu")
		return std::nullopt;
	return currentBuildTargetName(context) == targetName;
}

std::optional<bool> evaluateShaderStageIs(const ParseContext &context, std::string_view shaderStageName) {
	if (shaderStageName != "vertex" && shaderStageName != "fragment")
		return std::nullopt;
	if (!context.options.emitSPIRV)
		return false;
	return (context.options.shaderStage == ParseContext::ShaderStage::Vertex ? "vertex" : "fragment") == shaderStageName;
}
