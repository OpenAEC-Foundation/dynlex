#include "compileTimeValue.h"
#include "bindingResolution.h"
#include "compilerUtils.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "parseContext.h"
#include "pattern/pattern_tree/patternElement.h"
#include "section.h"
#include <cmath>
#include <unordered_set>

bool isCompileTimeKnown(const CompileTimeValue &value) { return !std::holds_alternative<std::monostate>(value); }

std::optional<bool> compileTimeTruthiness(const CompileTimeValue &value) {
	if (auto *boolean = std::get_if<bool>(&value))
		return *boolean;
	if (auto *number = std::get_if<double>(&value))
		return *number != 0.0;
	if (auto *text = std::get_if<std::string>(&value))
		return !text->empty();
	return std::nullopt;
}

static CompileTimeValue evaluateCompileTimeValueImpl(
	Expression *expr, ParseContext &context, const BindingFrameStack &bindingFrameStack, const Instantiation *instantiation
);

static thread_local std::unordered_set<const Expression *> activeCompileTimeFunctions;

static Expression *resolveCompileTimeBinding(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack *outBindingFrameStack = nullptr
) {
	if (outBindingFrameStack)
		*outBindingFrameStack = bindingFrameStack;
	if (expr && expr->kind == Expression::Kind::Pending && expr->patternReference) {
		auto &elements = expr->patternReference->patternElements;
		if (elements.empty())
			elements = getPatternElements(expr->patternReference->pattern.text);
		if (elements.size() == 1 &&
			(elements[0].type == PatternElement::Type::Variable || elements[0].type == PatternElement::Type::VariableLike)) {
			if (Expression *boundExpression = bindingFrameStack.lookup(elements[0].text))
				return boundExpression;
		}
	}
	return resolveVariableBindingAcrossFrames(expr, bindingFrameStack);
}

static std::string currentBuildInfo(ParseContext &context, std::string_view key) {
	if (key == "platform")
		return context.options.emitSPIRV ? "gpu" : context.options.emitWASM ? "wasm" : "cpu";
	if (key == "shader stage") {
		if (!context.options.emitSPIRV)
			return "";
		return context.options.shaderStage == ParseContext::ShaderStage::Vertex ? "vertex" : "fragment";
	}
	return {};
}

static std::optional<double> currentBuildInfoNumber(ParseContext &context, std::string_view key) {
	if (key == "word size")
		return (context.options.emitSPIRV || context.options.emitWASM) ? 32.0 : static_cast<double>(sizeof(void *) * 8);
	if (key == "optimization level")
		return static_cast<double>(context.options.optimizationLevel);
	return std::nullopt;
}

static CompileTimeValue
evaluateCompileTimeCast(const CompileTimeValue &value, Expression *typeExpr, const BindingFrameStack &bindingFrameStack) {
	if (!typeExpr)
		return {};
	typeExpr = resolveCompileTimeBinding(typeExpr, bindingFrameStack);
	if (!typeExpr || typeExpr->type.kind != DataType::Kind::Type)
		return {};
	DataType targetType = typeExpr->type.toReferencedType();
	if (targetType.kind == DataType::Kind::Bool) {
		std::optional<bool> truthy = compileTimeTruthiness(value);
		return truthy.has_value() ? CompileTimeValue(*truthy) : CompileTimeValue{};
	}
	if (!targetType.isNumeric())
		return {};
	if (const auto *number = std::get_if<double>(&value))
		return *number;
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? 1.0 : 0.0;
	return {};
}

static CompileTimeValue evaluateIntrinsic(
	Expression *expr, ParseContext &context, const BindingFrameStack &bindingFrameStack, const Instantiation *instantiation
) {
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	if (kind == IntrinsicKind::BuildInfo) {
		CompileTimeValue keyValue = evaluateCompileTimeValueImpl(expr->arguments[1], context, bindingFrameStack, instantiation);
		if (auto *key = std::get_if<std::string>(&keyValue)) {
			if (std::optional<double> number = currentBuildInfoNumber(context, *key))
				return *number;
			std::string text = currentBuildInfo(context, *key);
			if (!text.empty() || *key == "shader stage")
				return text;
		}
		return {};
	}
	if (kind == IntrinsicKind::SizeOf) {
		Expression *typeExpr = resolveCompileTimeBinding(expr->arguments[1], bindingFrameStack);
		if (!typeExpr)
			return {};
		DataType typeRef = typeExpr->type;
		if (typeRef.kind != DataType::Kind::Type)
			return {};
		return static_cast<double>(typeRef.toReferencedType().getByteSize());
	}

	if (kind == IntrinsicKind::Select) {
		CompileTimeValue conditionValue =
			evaluateCompileTimeValueImpl(expr->arguments[1], context, bindingFrameStack, instantiation);
		std::optional<bool> condition = compileTimeTruthiness(conditionValue);
		if (!condition.has_value())
			return {};
		return evaluateCompileTimeValueImpl(expr->arguments[*condition ? 2 : 3], context, bindingFrameStack, instantiation);
	}

	if (kind == IntrinsicKind::Return && expr->arguments.size() > 1)
		return evaluateCompileTimeValueImpl(expr->arguments[1], context, bindingFrameStack, instantiation);
	if (kind == IntrinsicKind::Cast && expr->arguments.size() > 2) {
		CompileTimeValue value = evaluateCompileTimeValueImpl(expr->arguments[1], context, bindingFrameStack, instantiation);
		if (!isCompileTimeKnown(value))
			return {};
		return evaluateCompileTimeCast(value, expr->arguments[2], bindingFrameStack);
	}

	auto lhs = [&]() -> CompileTimeValue {
		return expr->arguments.size() >= 2
				   ? evaluateCompileTimeValueImpl(expr->arguments[1], context, bindingFrameStack, instantiation)
				   : CompileTimeValue{};
	};
	auto rhs = [&]() -> CompileTimeValue {
		return expr->arguments.size() >= 3
				   ? evaluateCompileTimeValueImpl(expr->arguments[2], context, bindingFrameStack, instantiation)
				   : CompileTimeValue{};
	};

	if (kind == IntrinsicKind::Not) {
		std::optional<bool> value = compileTimeTruthiness(lhs());
		return value.has_value() ? CompileTimeValue(!*value) : CompileTimeValue{};
	}
	if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
		std::optional<bool> left = compileTimeTruthiness(lhs());
		std::optional<bool> right = compileTimeTruthiness(rhs());
		if (!left.has_value() || !right.has_value())
			return {};
		return kind == IntrinsicKind::And ? CompileTimeValue(*left && *right) : CompileTimeValue(*left || *right);
	}

	CompileTimeValue leftValue = lhs();
	CompileTimeValue rightValue = rhs();
	if (!isCompileTimeKnown(leftValue) || (expr->arguments.size() >= 3 && !isCompileTimeKnown(rightValue)))
		return {};

	if (kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) {
		bool result = false;
		if (auto *leftText = std::get_if<std::string>(&leftValue)) {
			if (auto *rightText = std::get_if<std::string>(&rightValue))
				result = *leftText == *rightText;
			else
				return {};
		} else if (auto *leftBool = std::get_if<bool>(&leftValue)) {
			if (auto *rightBool = std::get_if<bool>(&rightValue))
				result = *leftBool == *rightBool;
			else
				return {};
		} else {
			auto *leftNumber = std::get_if<double>(&leftValue);
			auto *rightNumber = std::get_if<double>(&rightValue);
			if (!leftNumber || !rightNumber)
				return {};
			result = *leftNumber == *rightNumber;
		}
		return kind == IntrinsicKind::Equal ? CompileTimeValue(result) : CompileTimeValue(!result);
	}

	auto *leftNumber = std::get_if<double>(&leftValue);
	if (!leftNumber)
		return {};
	if (kind == IntrinsicKind::Negate)
		return -*leftNumber;
	auto *rightNumber = std::get_if<double>(&rightValue);
	if (!rightNumber)
		return {};

	if (kind == IntrinsicKind::Add)
		return *leftNumber + *rightNumber;
	if (kind == IntrinsicKind::Subtract)
		return *leftNumber - *rightNumber;
	if (kind == IntrinsicKind::Multiply)
		return *leftNumber * *rightNumber;
	if (kind == IntrinsicKind::Divide)
		return *rightNumber == 0.0 ? CompileTimeValue{} : CompileTimeValue(*leftNumber / *rightNumber);
	if (kind == IntrinsicKind::Modulo)
		return *rightNumber == 0.0 ? CompileTimeValue{} : CompileTimeValue(std::fmod(*leftNumber, *rightNumber));
	if (kind == IntrinsicKind::LessThan)
		return *leftNumber < *rightNumber;
	if (kind == IntrinsicKind::GreaterThan)
		return *leftNumber > *rightNumber;
	if (kind == IntrinsicKind::LessThanOrEqual)
		return *leftNumber <= *rightNumber;
	if (kind == IntrinsicKind::GreaterThanOrEqual)
		return *leftNumber >= *rightNumber;
	return {};
}

static Expression *getSingleCompileTimeBody(Section *section) {
	if (!section)
		return nullptr;
	Expression *result = nullptr;
	for (Section *child : section->children) {
		for (CodeLine *line : child->codeLines) {
			if (!line->expression)
				continue;
			if (result)
				return nullptr;
			result = line->expression;
		}
	}
	return result;
}

static CompileTimeValue evaluatePatternCall(
	Expression *expr, ParseContext &context, const BindingFrameStack &bindingFrameStack, const Instantiation *instantiation
) {
	if (!expr->patternMatch || !expr->patternMatch->matchedEndNode ||
		expr->patternMatch->matchedEndNode->matchingDefinitions.empty())
		return {};

	PatternDefinition *def = expr->patternMatch->matchedEndNode->matchingDefinitions.front();
	if (!def || !def->section)
		return {};

	BindingMap callBindings;
	bindingFrameStack.forEachFrame([&callBindings](const BindingFrame &frame) {
		for (const auto &[bindingName, expression] : frame.bindings)
			callBindings[bindingName] = expression;
	});
	collectPatternCallBindings(expr, def, callBindings);

	if (def->section->isMacro) {
		BindingMap innerBindings;
		Expression *bodyExpr = expandMacroPatternCall(context, expr, innerBindings);
		if (!bodyExpr)
			return {};
		for (const auto &[name, argExpr] : innerBindings)
			callBindings[name] = argExpr;
		BindingFrameStack callBindingFrameStack;
		callBindingFrameStack.pushFrame(std::move(callBindings));
		return evaluateCompileTimeValueImpl(bodyExpr, context, callBindingFrameStack, instantiation);
	}

	Expression *bodyExpr = getSingleCompileTimeBody(def->section);
	if (!bodyExpr)
		return {};
	BindingFrameStack callBindingFrameStack;
	callBindingFrameStack.pushFrame(std::move(callBindings));
	return evaluateCompileTimeValueImpl(bodyExpr, context, callBindingFrameStack, instantiation);
}

static CompileTimeValue evaluateCompileTimeValueImpl(
	Expression *expr, ParseContext &context, const BindingFrameStack &bindingFrameStack, const Instantiation *instantiation
) {
	BindingFrameStack effectiveBindingFrameStack;
	expr = resolveCompileTimeBinding(expr, bindingFrameStack, &effectiveBindingFrameStack);
	if (!expr)
		return {};
	if (expr->type.kind == DataType::Kind::Type)
		return expr->type;
	if (activeCompileTimeFunctions.contains(expr))
		return {};

	struct ActiveCompileTimeGuard {
		const Expression *expr;

		explicit ActiveCompileTimeGuard(const Expression *expression) : expr(expression) {
			activeCompileTimeFunctions.insert(expr);
		}
		~ActiveCompileTimeGuard() { activeCompileTimeFunctions.erase(expr); }
	} guard(expr);

	switch (expr->kind) {
	case Expression::Kind::Literal:
		if (auto *number = std::get_if<double>(&expr->literalValue))
			return *number;
		if (auto *text = std::get_if<std::string>(&expr->literalValue))
			return *text;
		return {};
	case Expression::Kind::Variable:
		if (expr->variable && instantiation) {
			if (!instantiation->requiredCompileTimeParameters.contains(expr->variable->name))
				return {};
			auto it = instantiation->constantParameterValues.find(expr->variable->name);
			if (it != instantiation->constantParameterValues.end())
				return it->second;
		}
		return {};
	case Expression::Kind::ArrayLiteral:
	case Expression::Kind::TypedPlaceholder:
	case Expression::Kind::Pending:
		return {};
	case Expression::Kind::IntrinsicCall:
		return evaluateIntrinsic(expr, context, effectiveBindingFrameStack, instantiation);
	case Expression::Kind::PatternCall:
		return evaluatePatternCall(expr, context, effectiveBindingFrameStack, instantiation);
	}
	return {};
}

CompileTimeValue evaluateCompileTimeValue(
	Expression *expr, ParseContext &context, const BindingFrameStack &bindingFrameStack, const Instantiation *instantiation
) {
	return evaluateCompileTimeValueImpl(expr, context, bindingFrameStack, instantiation);
}
