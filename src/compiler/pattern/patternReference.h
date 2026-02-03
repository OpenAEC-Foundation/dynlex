#pragma once
#include "expression.h"
#include "patternMatch.h"
#include "range.h"
#include "sectionType.h"
#include "transformedPattern.h"
struct PatternReference {
	TransformedPattern pattern;
	SectionType patternType;
	std::vector<PatternElement> patternElements{};
	PatternMatch *match{};
	// for extracting arguments
	Expression *expression;
	bool resolved{};
	PatternReference(Expression *expression, SectionType patternType);
	void resolve(PatternMatch *matchResult = nullptr);
	const Range &range() const { return expression->range; }
};