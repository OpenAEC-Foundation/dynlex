#pragma once
#include "expression.h"
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
	Expression *expression;
	bool resolved{};
	PatternReference(Expression *expression, SectionType patternType);
	~PatternReference();
	PatternReference(const PatternReference &) = delete;
	PatternReference &operator=(const PatternReference &) = delete;
	void resolve(PatternMatch *matchResult = nullptr);
	const Range &range() const { return sourceRange; }
};
