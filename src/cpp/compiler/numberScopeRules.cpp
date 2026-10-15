#include "numberScopeRules.h"
#include "codeLine.h"
#include "expression.h"
#include "parseContext.h"
#include "typeReferenceValue.h"
#include "variable.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// The section whose rules govern code on `line`. A definition header belongs to the definition it opens, so a
// function's own rules also apply to its signature.
const Section *governingSection(const CodeLine *line) {
	if (line->sectionOpening && !line->sectionOpening->runsInEnclosingBody() &&
		line->sectionOpening->type != SectionType::ScopeRules)
		return line->sectionOpening;
	return line->section;
}

void mergeRule(EffectiveNumberRule &effective, const NumberScopeRule &rule) {
	const TypeConstraint &properties = rule.properties;
	if (!effective.integerKind) {
		if (properties.kind == DataType::Kind::Float) {
			effective.integerKind = false;
			effective.kindSource = &rule.source;
		} else if (properties.requiresInteger || properties.kind == DataType::Kind::Int ||
				   properties.kind == DataType::Kind::UInt) {
			effective.integerKind = true;
			effective.kindSource = &rule.source;
		}
	}
	if (!effective.sign && (properties.kind == DataType::Kind::Int || properties.kind == DataType::Kind::UInt)) {
		effective.sign = properties.kind;
		effective.signSource = &rule.source;
	}
	if (!effective.byteWidth && properties.numericSize) {
		effective.byteWidth = properties.numericSize;
		effective.widthSource = &rule.source;
	}
}

bool isFloatWidth(int byteWidth) { return byteWidth == 4 || byteWidth == 8; }

bool absoluteSource(const ScopeRuleSource *source) { return source && source->absolute; }

std::optional<std::uint64_t> integerLiteralMagnitude(const NumericLiteralValue &value) {
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return static_cast<std::uint64_t>(*integer);
	if (const auto *integer = std::get_if<std::uint64_t>(&value))
		return *integer;
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return std::uint64_t{1} << 63;
	return std::nullopt;
}

bool floatHoldsInteger(std::uint64_t magnitude, int byteWidth) {
	const double rounded = byteWidth == 4 ? static_cast<double>(static_cast<float>(magnitude)) : static_cast<double>(magnitude);
	return rounded < 18446744073709551616.0 && static_cast<std::uint64_t>(rounded) == magnitude;
}

bool integerHolds(std::uint64_t magnitude, DataType::Kind sign, int byteWidth) {
	const int bits = byteWidth * 8;
	if (sign == DataType::Kind::UInt)
		return bits >= 64 || magnitude < (std::uint64_t{1} << bits);
	return magnitude < (std::uint64_t{1} << (bits - 1));
}

CompileTimeValue integerLiteralValue(const NumericLiteralValue &value, const DataType &type) {
	if (type.kind == DataType::Kind::Float)
		return static_cast<double>(*integerLiteralMagnitude(value));
	return numericLiteralCompileTimeValue(value);
}

} // namespace

std::optional<NumericLiteralValue> numericLiteralValueOf(const Expression &expression) {
	if (expression.kind != Expression::Kind::Literal)
		return std::nullopt;
	if (const auto *integer = std::get_if<std::int64_t>(&expression.literalValue))
		return NumericLiteralValue{*integer};
	if (const auto *integer = std::get_if<std::uint64_t>(&expression.literalValue))
		return NumericLiteralValue{*integer};
	if (const auto *magnitude = std::get_if<MinimumSignedIntegerMagnitude>(&expression.literalValue))
		return NumericLiteralValue{*magnitude};
	if (const auto *floatingPoint = std::get_if<double>(&expression.literalValue))
		return NumericLiteralValue{*floatingPoint};
	return std::nullopt;
}

std::optional<EffectiveNumberRule> effectiveNumberRule(const ParseContext &context, const CodeLine *line, bool *pending) {
	if (pending)
		*pending = false;
	if (!line || !line->section || line->section->type == SectionType::ScopeRules)
		return std::nullopt;
	EffectiveNumberRule effective;
	bool found = false;
	auto visit = [&](const ScopeRules &rules) {
		if (rules.numberRulePending && pending)
			*pending = true;
		if (!rules.numbers)
			return;
		mergeRule(effective, *rules.numbers);
		found = true;
	};
	for (const Section *scope = governingSection(line); scope && scope != context.mainSection; scope = scope->parent)
		visit(scope->scopeRules);
	auto fileRules = context.fileScopeRules.find(line->sourceFile);
	if (fileRules != context.fileScopeRules.end())
		visit(fileRules->second);
	if (!found)
		return std::nullopt;
	return effective;
}

TypeConstraint applyNumberRule(
	const TypeConstraint &constraint, const EffectiveNumberRule &rule, std::vector<const ScopeRuleSource *> *contradictions
) {
	TypeConstraint result = constraint;
	if (result.elementConstraint)
		result.elementConstraint =
			std::make_shared<TypeConstraint>(applyNumberRule(*result.elementConstraint, rule, contradictions));
	if (!result.isResolved() || (result.pointerDepth && *result.pointerDepth != 0))
		return result;
	const bool floatingPoint = result.kind == DataType::Kind::Float;
	const bool signedness = result.kind == DataType::Kind::Int || result.kind == DataType::Kind::UInt;
	const bool integer = result.requiresInteger || signedness;
	if (!result.requiresNumeric && !floatingPoint && !integer)
		return result;

	auto contradict = [&](const ScopeRuleSource *source) {
		if (contradictions && absoluteSource(source))
			contradictions->push_back(source);
	};
	if (rule.integerKind) {
		if (floatingPoint && *rule.integerKind)
			contradict(rule.kindSource);
		else if (integer && !*rule.integerKind)
			contradict(rule.kindSource);
	}
	if (rule.sign && signedness && result.kind != rule.sign)
		contradict(rule.signSource);
	if (rule.byteWidth && result.numericSize && result.numericSize != rule.byteWidth)
		contradict(rule.widthSource);

	if (!floatingPoint && !integer && rule.integerKind) {
		if (*rule.integerKind)
			result.requiresInteger = true;
		else
			result.kind = DataType::Kind::Float;
	}
	if (result.requiresInteger && !result.kind && rule.sign) {
		result.kind = rule.sign;
		result.requiresInteger = false;
	}
	if (!result.numericSize && rule.byteWidth && (result.kind != DataType::Kind::Float || isFloatWidth(*rule.byteWidth)))
		result.numericSize = rule.byteWidth;
	if (result.kind || result.requiresInteger) {
		result.requiresNumeric = false;
		result.pointerDepth = 0;
	}
	return result;
}

NumericLiteralTyping typeNumericLiteral(const ParseContext &context, const CodeLine *line, const NumericLiteralValue &value) {
	const bool emitSPIRV = context.options.emitSPIRV;
	NumericLiteralTyping result{numericLiteralType(value, emitSPIRV), numericLiteralCompileTimeValue(value)};
	std::optional<EffectiveNumberRule> rule = effectiveNumberRule(context, line);
	if (!rule)
		return result;

	if (std::holds_alternative<double>(value)) {
		// The decimal point specifies a floating-point number; only the width is open.
		if (rule->integerKind && *rule->integerKind && absoluteSource(rule->kindSource))
			result.contradicted = rule->kindSource;
		if (rule->byteWidth && isFloatWidth(*rule->byteWidth))
			result.type.numericSize = *rule->byteWidth;
	} else {
		const std::uint64_t magnitude = *integerLiteralMagnitude(value);
		const bool floatingPoint = rule->integerKind && !*rule->integerKind;
		// The magnitude of the signed 64 bit minimum only exists to be negated into that signed value.
		if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value) && !floatingPoint &&
			rule->sign.value_or(DataType::Kind::Int) == DataType::Kind::Int)
			return result;
		if (floatingPoint) {
			int byteWidth =
				rule->byteWidth && isFloatWidth(*rule->byteWidth) ? *rule->byteWidth : defaultFloatByteSize(emitSPIRV);
			if (!floatHoldsInteger(magnitude, byteWidth)) {
				if (byteWidth == 8 || !floatHoldsInteger(magnitude, 8)) {
					result.unrepresentableBy = rule->kindSource;
					return result;
				}
				result.widenedBy = rule->widthSource ? rule->widthSource : rule->kindSource;
				byteWidth = 8;
			}
			result.type = {DataType::Kind::Float, byteWidth};
		} else if (rule->sign || rule->byteWidth) {
			DataType::Kind sign = rule->sign.value_or(result.type.kind);
			int byteWidth = rule->byteWidth.value_or(sign == result.type.kind ? result.type.numericSize : 4);
			if (!integerHolds(magnitude, sign, byteWidth)) {
				if (byteWidth == 8 || !integerHolds(magnitude, sign, 8)) {
					result.unrepresentableBy = rule->sign ? rule->signSource : rule->widthSource;
					return result;
				}
				if (rule->byteWidth)
					result.widenedBy = rule->widthSource;
				byteWidth = 8;
			}
			result.type = {sign, byteWidth};
		}
		result.value = integerLiteralValue(value, result.type);
	}
	result.ruleTyped = result.type != numericLiteralType(value, emitSPIRV);
	return result;
}

std::optional<NumericLiteralTyping> numericLiteralTypingForParameter(
	const ParseContext &context, const NumericLiteralValue &value, const TypeConstraint &parameterConstraint
) {
	const bool emitSPIRV = context.options.emitSPIRV;
	DataType builtinType = numericLiteralType(value, emitSPIRV);
	std::optional<std::uint64_t> magnitude = integerLiteralMagnitude(value);
	if (std::optional<TypeReferenceValue> parameterType = TypeReferenceValue::forConstraint(parameterConstraint, emitSPIRV)) {
		DataType type = parameterType->type.toReferencedType();
		bool holds = false;
		if (type.pointerDepth == 0 && type.kind == DataType::Kind::Float)
			holds = !magnitude || floatHoldsInteger(*magnitude, type.numericSize);
		else if (type.pointerDepth == 0 && magnitude && (type.kind == DataType::Kind::Int || type.kind == DataType::Kind::UInt))
			holds = integerHolds(*magnitude, type.kind, type.numericSize);
		if (holds && parameterConstraint.accepts(type, true)) {
			CompileTimeValue literalValue =
				magnitude ? integerLiteralValue(value, type) : numericLiteralCompileTimeValue(value);
			return NumericLiteralTyping{type, literalValue, false};
		}
	}
	if (parameterConstraint.accepts(builtinType, true))
		return NumericLiteralTyping{builtinType, numericLiteralCompileTimeValue(value), false};
	return std::nullopt;
}

namespace {

bool sameRange(const Range &left, const Range &right) {
	return left.line == right.line && left.subString.data() == right.subString.data() &&
		   left.subString.size() == right.subString.size();
}

bool rangeContains(const Range &outer, const Range &inner) {
	return outer.line == inner.line && outer.subString.data() <= inner.subString.data() &&
		   inner.subString.data() + inner.subString.size() <= outer.subString.data() + outer.subString.size();
}

// The rules that gave a literal its type, if the rules changed it.
void collectLiteralRuleSources(
	const ParseContext &context, const CodeLine *line, const NumericLiteralValue &value,
	std::vector<const ScopeRuleSource *> &sources
) {
	if (!typeNumericLiteral(context, line, value).ruleTyped)
		return;
	std::optional<EffectiveNumberRule> rule = effectiveNumberRule(context, line);
	for (const ScopeRuleSource *source : {rule->kindSource, rule->signSource, rule->widthSource}) {
		if (source && std::find(sources.begin(), sources.end(), source) == sources.end())
			sources.push_back(source);
	}
}

} // namespace

void appendNumberRuleOrigins(const ParseContext &context, Expression *root, std::vector<RelatedInfo> &relatedInfo) {
	std::vector<const ScopeRuleSource *> sources;
	visitExpressionTree(root, [&](Expression *expression) {
		if (std::optional<NumericLiteralValue> value = numericLiteralValueOf(*expression)) {
			collectLiteralRuleSources(context, expression->range.line, *value, sources);
		} else if (expression->kind == Expression::Kind::Variable && expression->variable && expression->range.line &&
				   expression->range.line->section) {
			const Variable *variable = expression->range.line->section->findVariable(expression->variable);
			if (variable && variable->typeOriginRange.line) {
				NumericLiteralParseResult parsed = parseNumericLiteral(variable->typeOriginRange.subString);
				if (parsed)
					collectLiteralRuleSources(context, variable->typeOriginRange.line, parsed.value, sources);
			}
		}
		return false;
	});
	for (const ScopeRuleSource *source : sources) {
		const SyntaxConfig &syntax = syntaxConfigForRange(context, source->range);
		relatedInfo.push_back(
			{renderConfiguredMessage(syntax, "number rule origin", "message", {{"rule", source->range.subString}}),
			 source->range}
		);
	}
}

void emitScopeRuleWarnings(ParseContext &context) {
	std::vector<ParseContext::ScopeRuleWarning> warnings = std::move(context.scopeRuleWarnings);
	context.scopeRuleWarnings.clear();
	std::vector<const ParseContext::ScopeRuleWarning *> kept;
	for (const ParseContext::ScopeRuleWarning &warning : warnings) {
		bool redundant = std::any_of(warnings.begin(), warnings.end(), [&](const ParseContext::ScopeRuleWarning &other) {
			return &other != &warning && other.key == warning.key && sameRange(other.ruleRange, warning.ruleRange) &&
				   rangeContains(other.range, warning.range) && !sameRange(other.range, warning.range);
		});
		bool repeated = std::any_of(kept.begin(), kept.end(), [&](const ParseContext::ScopeRuleWarning *other) {
			return other->key == warning.key && sameRange(other->range, warning.range) &&
				   sameRange(other->ruleRange, warning.ruleRange);
		});
		if (!redundant && !repeated)
			kept.push_back(&warning);
	}
	std::ranges::stable_sort(kept, [](const ParseContext::ScopeRuleWarning *left, const ParseContext::ScopeRuleWarning *right) {
		if (left->range.line->mergedLineIndex != right->range.line->mergedLineIndex)
			return left->range.line->mergedLineIndex < right->range.line->mergedLineIndex;
		return left->range.subString.data() < right->range.subString.data();
	});
	for (const ParseContext::ScopeRuleWarning *warning : kept) {
		const SyntaxConfig &syntax = syntaxConfigForRange(context, warning->range);
		Diagnostic diagnostic;
		diagnostic.level = Diagnostic::Level::Warning;
		diagnostic.range = warning->range;
		diagnostic.message = renderConfiguredMessage(syntax, warning->key, "message", warning->placeholders);
		diagnostic.relatedInfo.push_back({renderConfiguredMessage(syntax, warning->key, "related rule"), warning->ruleRange});
		context.diagnostics.push_back(std::move(diagnostic));
	}
}
