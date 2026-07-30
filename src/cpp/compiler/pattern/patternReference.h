#pragma once
#include "expression.h"
#include "patternMatch.h"
#include "range.h"
#include "sectionType.h"
#include "transformedPattern.h"
struct PatternReference {
	enum class Purpose {
		Expression,
		TypeConstraint,
	};

	Range sourceRange;
	TransformedPattern pattern;
	SectionType patternType;
	std::vector<PatternElement> patternElements{};
	PatternMatch *match{};
	// for extracting arguments
	Expression *expression;
	bool resolved{};
	Purpose purpose = Purpose::Expression;
	PatternReference(Expression *expression, SectionType patternType);
	~PatternReference();
	PatternReference(const PatternReference &) = delete;
	PatternReference &operator=(const PatternReference &) = delete;
	void resolve(PatternMatch *matchResult = nullptr);
	const Range &range() const { return sourceRange; }
};
