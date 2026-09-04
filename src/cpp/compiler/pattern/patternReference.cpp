#include "patternReference.h"
#include "codeLine.h"
#include "section.h"

PatternReference::PatternReference(Expression *expression, SectionType patternType)
	: sourceRange(expression->range), pattern(std::string(expression->range.subString)), patternType(patternType),
	  expression(expression) {}

PatternReference::~PatternReference() { delete match; }

std::optional<std::string> numericPatternArgumentSpelling(const PatternReference *reference, size_t argumentIndex) {
	if (!reference || !reference->expression || argumentIndex >= reference->expression->arguments.size())
		return std::nullopt;
	const Expression *argument = reference->expression->arguments[argumentIndex];
	if (!argument || argument->kind != Expression::Kind::Literal || !argument->range.line)
		return std::nullopt;
	const bool numeric = std::holds_alternative<std::int64_t>(argument->literalValue) ||
						 std::holds_alternative<MinimumSignedIntegerMagnitude>(argument->literalValue) ||
						 std::holds_alternative<double>(argument->literalValue);
	return numeric ? std::optional<std::string>(argument->range.subString) : std::nullopt;
}

void PatternReference::resolve(PatternMatch *matchResult) {
	match = matchResult;
	resolved = true;
	range().section()->decrementUnresolved();
}
