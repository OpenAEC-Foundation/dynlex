#include "patternReference.h"
#include "codeLine.h"
#include "section.h"

PatternReference::PatternReference(Range range, SectionType patternType)
	: range(range), pattern((std::string)range.subString), patternType(patternType) {}

void PatternReference::resolve(PatternMatch *matchResult) {
	match = matchResult;
	resolved = true;
	range.line->section->decrementUnresolved();
}