#pragma once
#include "patternMatch.h"
#include "range.h"
#include "sectionType.h"
#include "transformedPattern.h"
struct PatternReference {
	Range range;
	TransformedPattern pattern;
	SectionType patternType;
	std::vector<PatternElement> patternElements{};
	PatternMatch *match{};
	bool resolved{};
	PatternReference(Range range, SectionType patternType);
	void resolve(PatternMatch *matchResult = nullptr);
};