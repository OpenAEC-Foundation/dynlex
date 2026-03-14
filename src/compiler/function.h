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

struct Function {
	enum class Kind {
		Literal,
		ArrayLiteral,
		Variable,
		PatternCall,
		IntrinsicCall,
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
	std::vector<Function *> arguments;

	// True if this node was created from a subMatch in expandMatch.
	// Only subMatch-originated PatternCalls participate in operand reordering.
	bool isSubMatch = false;

	// True if this function came from explicit parentheses in the source.
	// Grouped functions are inferred independently and should not be flattened
	// back into surrounding operator regrouping.
	bool isExplicitGroup = false;
};

// Utility: Sort function arguments by their source position
inline std::vector<Function *> sortArgumentsByPosition(const std::vector<Function *> &args) {
	std::vector<Function *> sortedArgs = args;
	std::sort(sortedArgs.begin(), sortedArgs.end(), [](Function *a, Function *b) {
		return a->range.start() < b->range.start();
	});
	return sortedArgs;
}
