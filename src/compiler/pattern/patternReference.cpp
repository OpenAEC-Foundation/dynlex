#include "patternReference.h"
#include "codeLine.h"
#include "section.h"

PatternReference::PatternReference(Expression *expression, SectionType patternType)
	: pattern(std::string(expression->range.subString)), patternType(patternType), expression(expression) {}

void PatternReference::resolve(PatternMatch *matchResult) {
	match = matchResult;
	resolved = true;
	range().section()->decrementUnresolved();
}