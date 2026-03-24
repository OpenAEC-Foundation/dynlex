#pragma once
#include "bindingMap.h"
#include "range.h"
#include "type.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

struct PatternMatch;
struct PatternDefinition;
struct PatternReference;
struct VariableReference;

struct Expression {
	enum class Kind {
		Literal,
		ArrayLiteral,
		Variable,
		PatternCall,
		IntrinsicCall,
		TypedPlaceholder,
		Pending // not yet resolved - could become PatternCall or Variable
	};

	Kind kind = Kind::Pending;
	DataType type;
	Range range;

	// For Literal: the actual value
	std::variant<std::monostate, double, std::string> literalValue;

	// For Variable: reference to the variable
	VariableReference *variable{};

	// For PatternCall: the matched pattern (filled after resolution)
	PatternMatch *patternMatch{};
	// For PatternCall: overload selected during type inference.
	PatternDefinition *selectedPatternDefinition{};

	// For Pending: the pattern reference (used during resolution)
	PatternReference *patternReference{};

	// For IntrinsicCall: the intrinsic name
	std::string intrinsicName;

	// Arguments (for PatternCall and IntrinsicCall)
	std::vector<Expression *> arguments;

	// True if this node was created from a subMatch in expandMatch.
	// Only subMatch-originated PatternCalls participate in operand reordering.
	bool isSubMatch = false;

	// True if this expression came from explicit parentheses in the source.
	// Grouped expressions are inferred independently and should not be flattened
	// back into surrounding operator regrouping.
	bool isExplicitGroup = false;

	// For expanded macro roots: actual argument indices that correspond to the
	// source pattern's parameter slots, in source order.
	std::vector<int> groupingArgumentIndices;
	// For each source-order grouping argument slot above, whether it had an
	// adjacent sibling parameter slot in the original matched pattern.
	std::vector<bool> groupingArgumentHasAdjacentSiblingSlot;
	// For expanded macro roots: whether the original matched pattern started or
	// ended with an argument slot. This lets operand regrouping keep working
	// after macro expansion removes the original PatternCall node.
	bool groupingStartsWithArgument = false;
	bool groupingEndsWithArgument = false;
	// Precedence of the source pattern that expanded into this expression root.
	int groupingPrecedence = 0;
};

// Utility: Sort expression arguments by their source position
inline std::vector<Expression *> sortArgumentsByPosition(const std::vector<Expression *> &args) {
	std::vector<Expression *> sortedArgs = args;
	bool allArgumentsHaveSourceRanges = std::all_of(sortedArgs.begin(), sortedArgs.end(), [](Expression *arg) {
		return arg && arg->range.line && !arg->range.subString.empty();
	});
	if (!allArgumentsHaveSourceRanges)
		return sortedArgs;
	std::stable_sort(sortedArgs.begin(), sortedArgs.end(), [](Expression *a, Expression *b) {
		return a->range.start() < b->range.start();
	});
	return sortedArgs;
}
