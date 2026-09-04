#pragma once
#include "expression.h"
#include "patternMatch.h"
#include "range.h"
#include "sectionType.h"
#include "transformedPattern.h"
#include <optional>
struct Section;
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
	// Lexical scope used for matching explicit variable names. Synthetic references,
	// such as dependent type constraints, can use a source range owned by a parent section.
	Section *matchingScope{};
	bool resolved{};
	Purpose purpose = Purpose::Expression;
	PatternReference(Expression *expression, SectionType patternType);
	~PatternReference();
	PatternReference(const PatternReference &) = delete;
	PatternReference &operator=(const PatternReference &) = delete;
	void resolve(PatternMatch *matchResult = nullptr);
	const Range &range() const { return sourceRange; }
};

std::optional<std::string> numericPatternArgumentSpelling(const PatternReference *reference, size_t argumentIndex);
