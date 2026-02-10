#pragma once
#include "range.h"
#include "type.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

struct PatternMatch;
struct PatternReference;
struct VariableReference;

struct Expression {
	enum class Kind {
		Literal,
		Variable,
		PatternCall,
		IntrinsicCall,
		Pending // not yet resolved - could become PatternCall or Variable
	};

	Kind kind = Kind::Pending;
	Type type;
	Range range;

	// For Literal: the actual value
	std::variant<std::monostate, int64_t, double, std::string> literalValue;

	// For Variable: reference to the variable
	VariableReference *variable{};

	// For PatternCall: the matched pattern (filled after resolution)
	PatternMatch *patternMatch{};

	// For Pending: the pattern reference (used during resolution)
	PatternReference *patternReference{};

	// For IntrinsicCall: the intrinsic name
	std::string intrinsicName;

	// Arguments (for PatternCall and IntrinsicCall)
	std::vector<Expression *> arguments;
};

// Utility: Sort expression arguments by their source position
inline std::vector<Expression *> sortArgumentsByPosition(const std::vector<Expression *> &args) {
	std::vector<Expression *> sortedArgs = args;
	std::sort(sortedArgs.begin(), sortedArgs.end(), [](Expression *a, Expression *b) {
		return a->range.start() < b->range.start();
	});
	return sortedArgs;
}
