#include "patternReference.h"

PatternReference::PatternReference(Range range, SectionType patternType)
	: range(range), pattern((std::string)range.subString), patternType(patternType)
{
}