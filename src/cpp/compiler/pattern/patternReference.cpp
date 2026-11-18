#include "patternReference.h"
#include "codeLine.h"
#include "section.h"

PatternReference::PatternReference(Expression *expression, SectionType patternType)
	: PatternReference(SourceExpressionSite(expression), patternType) {}

PatternReference::PatternReference(SourceExpressionSite expression, SectionType patternType)
	: sourceRange(expression.range()), pattern(std::string(sourceRange.subString)), patternType(patternType),
	  expression(expression) {}

PatternReference::~PatternReference() { delete match; }

std::optional<std::string> numericPatternArgumentSpelling(const PatternReference *reference, size_t argumentIndex) {
	if (!reference || !reference->expression || argumentIndex >= reference->expression.argumentCount())
		return std::nullopt;
	return reference->expression.argument(argumentIndex).numericSpelling();
}

void PatternReference::resolve(PatternMatch *matchResult) {
	match = matchResult;
	resolved = true;
	range().section()->decrementUnresolved();
}
