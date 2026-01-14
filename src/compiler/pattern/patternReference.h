#pragma once
#include "range.h"
#include "transformedPattern.h"
#include "sectionType.h"
struct PatternReference
{
	Range range;
	TransformedPattern pattern;
	SectionType patternType;
	std::vector<PatternElement> patternElements{};
	bool resolved{};
	PatternReference(Range range, SectionType patternType);
};