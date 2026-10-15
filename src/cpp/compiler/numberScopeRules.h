#pragma once
#include "compileTimeInfo.h"
#include "diagnostic.h"
#include "numericLiteral.h"
#include "scopeRules.h"
#include <optional>
#include <vector>

struct ParseContext;
struct CodeLine;
struct Expression;

// The numeric value of a literal expression, or nullopt for other literals.
std::optional<NumericLiteralValue> numericLiteralValueOf(const Expression &expression);

// The number properties the rules in effect at a line specify. Inner scopes override outer scopes per property.
struct EffectiveNumberRule {
	// true: integer, false: floating-point.
	std::optional<bool> integerKind;
	const ScopeRuleSource *kindSource{};
	// Int or UInt.
	std::optional<DataType::Kind> sign;
	const ScopeRuleSource *signSource{};
	std::optional<int> byteWidth;
	const ScopeRuleSource *widthSource{};
};

// The rules in effect for code written on `line`, or nullopt when none apply. Lines inside a rule block are never
// subject to rules. `pending` reports a rule on the lexical path whose properties are not inferred yet.
std::optional<EffectiveNumberRule>
effectiveNumberRule(const ParseContext &context, const CodeLine *line, bool *pending = nullptr);

// Fills the unspecified number properties of a number constraint. Explicit properties contradicting an absolute rule
// are reported in `contradictions`.
TypeConstraint applyNumberRule(
	const TypeConstraint &constraint, const EffectiveNumberRule &rule,
	std::vector<const ScopeRuleSource *> *contradictions = nullptr
);

struct NumericLiteralTyping {
	DataType type;
	CompileTimeValue value;
	// Whether the rules gave the literal another type than the built-in literal typing.
	bool ruleTyped = false;
	// The rule whose width could not hold the literal, so a wider type was used.
	const ScopeRuleSource *widenedBy{};
	// An absolute rule the literal's own form contradicts.
	const ScopeRuleSource *contradicted{};
	// The rule whose properties cannot represent the literal at any width.
	const ScopeRuleSource *unrepresentableBy{};
};

NumericLiteralTyping typeNumericLiteral(const ParseContext &context, const CodeLine *line, const NumericLiteralValue &value);

// Notes pointing at the number rules that typed the literals in `root`, directly or through the variables they were
// stored in, for an error about that expression.
void appendNumberRuleOrigins(const ParseContext &context, Expression *root, std::vector<RelatedInfo> &relatedInfo);

// Reports the collected scope rule warnings, keeping the outermost of nested phrases.
void emitScopeRuleWarnings(ParseContext &context);

// The type a rule-typed integer literal takes when an explicitly typed parameter rejects its rule type: the
// parameter's own type when it holds the value exactly, otherwise the built-in literal type.
std::optional<NumericLiteralTyping> numericLiteralTypingForParameter(
	const ParseContext &context, const NumericLiteralValue &value, const TypeConstraint &parameterConstraint
);
