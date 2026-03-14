#include "compileTimeValue.h"
#include "bindingResolution.h"
#include "compilerUtils.h"
#include "function.h"
#include "intrinsicInfo.h"
#include "parseContext.h"
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
	Function *expr, ParseContext &context, const std::unordered_map<std::string, Function *> &bindings,
	const Instantiation *instantiation
);

static thread_local std::unordered_set<const Function *> activeCompileTimeFunctions;

static Function *resolveCompileTimeBinding(
	Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	std::unordered_map<std::string, Function *> *outBindings = nullptr
) {
	if (outBindings)
		*outBindings = bindings;
	return resolveVariableBindingChain(expr, bindings);
}

static std::string currentBuildInfo(ParseContext &context, std::string_view key) {
	if (key == "platform")
		return context.options.emitSPIRV ? "gpu" : "cpu";
	if (key == "shader stage") {
		if (!context.options.emitSPIRV)
			return "";
		return context.options.shaderStage == ParseContext::ShaderStage::Vertex ? "vertex" : "fragment";
	}
	return {};
}

static std::optional<double> currentBuildInfoNumber(ParseContext &context, std::string_view key) {
	if (key == "word size")
		return context.options.emitSPIRV ? 32.0 : static_cast<double>(sizeof(void *) * 8);
	if (key == "optimization level")
		return static_cast<double>(context.options.optimizationLevel);
	return std::nullopt;
}

static CompileTimeValue evaluateIntrinsic(
	Function *expr, ParseContext &context, const std::unordered_map<std::string, Function *> &bindings,
	const Instantiation *instantiation
) {
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	if (kind == IntrinsicKind::BuildInfo) {
		CompileTimeValue keyValue = evaluateCompileTimeValueImpl(expr->arguments[1], context, bindings, instantiation);
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
		Function *typeExpr = resolveCompileTimeBinding(expr->arguments[1], bindings);
		if (!typeExpr)
			return {};
		DataType typeRef = typeExpr->type;
		if (typeRef.kind != DataType::Kind::Type)
			return {};
		return static_cast<double>(typeRef.toReferencedType().getByteSize());
	}

	if (kind == IntrinsicKind::Select) {
		CompileTimeValue conditionValue = evaluateCompileTimeValueImpl(expr->arguments[1], context, bindings, instantiation);
		std::optional<bool> condition = compileTimeTruthiness(conditionValue);
		if (!condition.has_value())
			return {};
		return evaluateCompileTimeValueImpl(expr->arguments[*condition ? 2 : 3], context, bindings, instantiation);
	}

	if (kind == IntrinsicKind::Return && expr->arguments.size() > 1)
		return evaluateCompileTimeValueImpl(expr->arguments[1], context, bindings, instantiation);

	auto lhs = [&]() -> CompileTimeValue {
		return expr->arguments.size() >= 2 ? evaluateCompileTimeValueImpl(expr->arguments[1], context, bindings, instantiation)
										   : CompileTimeValue{};
	};
	auto rhs = [&]() -> CompileTimeValue {
		return expr->arguments.size() >= 3 ? evaluateCompileTimeValueImpl(expr->arguments[2], context, bindings, instantiation)
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

static Function *getSingleCompileTimeBody(Section *section) {
	if (!section)
		return nullptr;
	Function *result = nullptr;
	for (Section *child : section->children) {
		for (CodeLine *line : child->codeLines) {
			if (!line->function)
				continue;
			if (result)
				return nullptr;
			result = line->function;
		}
	}
	return result;
}

static CompileTimeValue evaluatePatternCall(
	Function *expr, ParseContext &context, const std::unordered_map<std::string, Function *> &bindings,
	const Instantiation *instantiation
) {
	if (!expr->patternMatch || !expr->patternMatch->matchedEndNode ||
		expr->patternMatch->matchedEndNode->matchingDefinitions.empty())
		return {};

	PatternDefinition *def = expr->patternMatch->matchedEndNode->matchingDefinitions.front();
	if (!def || !def->section)
		return {};

	std::unordered_map<std::string, Function *> callBindings = bindings;
	collectPatternCallBindings(expr, def, callBindings);

	if (def->section->isMacro) {
		std::unordered_map<std::string, Function *> innerBindings;
		Function *bodyExpr = expandMacroPatternCall(expr, innerBindings);
		if (!bodyExpr)
			return {};
		for (const auto &[name, argExpr] : innerBindings)
			callBindings[name] = argExpr;
		return evaluateCompileTimeValueImpl(bodyExpr, context, callBindings, instantiation);
	}

	Function *bodyExpr = getSingleCompileTimeBody(def->section);
	if (!bodyExpr)
		return {};
	return evaluateCompileTimeValueImpl(bodyExpr, context, callBindings, instantiation);
}

static CompileTimeValue evaluateCompileTimeValueImpl(
	Function *expr, ParseContext &context, const std::unordered_map<std::string, Function *> &bindings,
	const Instantiation *instantiation
) {
	std::unordered_map<std::string, Function *> effectiveBindings;
	expr = resolveCompileTimeBinding(expr, bindings, &effectiveBindings);
	if (!expr)
		return {};
	if (activeCompileTimeFunctions.contains(expr))
		return {};

	struct ActiveCompileTimeGuard {
		const Function *expr;

		explicit ActiveCompileTimeGuard(const Function *function) : expr(function) { activeCompileTimeFunctions.insert(expr); }
		~ActiveCompileTimeGuard() { activeCompileTimeFunctions.erase(expr); }
	} guard(expr);

	switch (expr->kind) {
	case Function::Kind::Literal:
		if (auto *number = std::get_if<double>(&expr->literalValue))
			return *number;
		if (auto *text = std::get_if<std::string>(&expr->literalValue))
			return *text;
		return {};
	case Function::Kind::Variable:
		if (expr->variable && instantiation) {
			if (!instantiation->requiredCompileTimeParameters.contains(expr->variable->name))
				return {};
			auto it = instantiation->constantParameterValues.find(expr->variable->name);
			if (it != instantiation->constantParameterValues.end())
				return it->second;
		}
		return {};
	case Function::Kind::ArrayLiteral:
	case Function::Kind::Pending:
		return {};
	case Function::Kind::IntrinsicCall:
		return evaluateIntrinsic(expr, context, effectiveBindings, instantiation);
	case Function::Kind::PatternCall:
		return evaluatePatternCall(expr, context, effectiveBindings, instantiation);
	}
	return {};
}

CompileTimeValue evaluateCompileTimeValue(
	Function *expr, ParseContext &context, const std::unordered_map<std::string, Function *> &bindings,
	const Instantiation *instantiation
) {
	return evaluateCompileTimeValueImpl(expr, context, bindings, instantiation);
}
