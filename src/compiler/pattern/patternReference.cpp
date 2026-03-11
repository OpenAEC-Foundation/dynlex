#include "patternReference.h"
#include "codeLine.h"
#include "section.h"

PatternReference::PatternReference(Function *function, SectionType patternType)
	: sourceRange(function->range), pattern(std::string(function->range.subString)), patternType(patternType),
	  function(function) {}

void PatternReference::resolve(PatternMatch *matchResult) {
	match = matchResult;
	resolved = true;
	range().section()->decrementUnresolved();
}
