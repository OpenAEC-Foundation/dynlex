#pragma once
#include "expression.h"
#include "patternMatch.h"
#include "range.h"
#include "sectionType.h"
#include "sourceExpressionSite.h"
#include "transformedPattern.h"
#include <memory>
#include <optional>
#include <utility>
struct Section;
// Parsed spellings stay fixed while the matcher revisits a reference.
struct MatcherKeyLiterals {
	std::vector<std::optional<std::string>> numericSpellings;
	std::vector<std::string> possibleStartLiterals;
};
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
	// Exact source span selected when a lookup resolves directly to a variable.
	std::optional<std::pair<int, int>> variableResolutionOffsets;
	// Dependency key of the last lookup, including failed variable lookups.
	std::string incrementalMatcherKey;
	// Match key before precedence regrouping, for replay after definitions are indexed.
	std::string initialMatcherKey;
	std::string prevalidatedMatcherKey;
	std::unique_ptr<MatcherKeyLiterals> matcherKeyLiterals;
	// for extracting arguments
	SourceExpressionSite expression;
	// Lexical scope used for matching explicit variable names. Synthetic references,
	// such as dependent type constraints, can use a source range owned by a parent section.
	Section *matchingScope{};
	bool resolved{};
	Purpose purpose = Purpose::Expression;
	PatternReference(Expression *expression, SectionType patternType);
	PatternReference(SourceExpressionSite expression, SectionType patternType);
	~PatternReference();
	PatternReference(const PatternReference &) = delete;
	PatternReference &operator=(const PatternReference &) = delete;
	bool indexDataDeclarationNames(const PatternMatch &match);
	void resolve(PatternMatch *matchResult = nullptr);
	const Range &range() const { return sourceRange; }
};

std::optional<std::string> numericPatternArgumentSpelling(const PatternReference *reference, size_t argumentIndex);
