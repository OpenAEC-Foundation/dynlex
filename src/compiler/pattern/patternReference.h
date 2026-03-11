#pragma once
#include "function.h"
#include "patternMatch.h"
#include "range.h"
#include "sectionType.h"
#include "transformedPattern.h"
struct PatternReference {
	Range sourceRange;
	TransformedPattern pattern;
	SectionType patternType;
	std::vector<PatternElement> patternElements{};
	PatternMatch *match{};
	// for extracting arguments
	Function *function;
	bool resolved{};
	PatternReference(Function *function, SectionType patternType);
	void resolve(PatternMatch *matchResult = nullptr);
	const Range &range() const { return sourceRange; }
};
