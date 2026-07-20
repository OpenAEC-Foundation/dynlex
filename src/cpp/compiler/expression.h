#pragma once
#include "bindingMap.h"
#include "compileTimeInfo.h"
#include "range.h"
#include "type.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

struct PatternMatch;
struct PatternDefinition;
struct PatternReference;
struct VariableReference;
struct Instantiation;
struct InstantiatedSectionBody;

struct Expression {
	struct BranchSelection {
		bool known = false;
		int selectedBranchIndex = -1;
	};

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
	CompileTimeValue compileTimeValue{};
	Range range;

	// For Literal: the actual value
	std::variant<std::monostate, double, std::string> literalValue;

	// For Variable: reference to the variable
	VariableReference *variable{};

	// For PatternCall: the matched pattern (filled after resolution)
	PatternMatch *patternMatch{};
	// For PatternCall: overload selected during type inference.
	PatternDefinition *selectedPatternDefinition{};
	// For the function intrinsic: the exact callable definition selected during inference.
	PatternDefinition *selectedCallableDefinition{};
	// For non-flex PatternCalls: the exact monomorphized callee selected during
	// type inference. Later stages consume this instance without rebuilding its key.
	Instantiation *selectedInstantiation{};
	// Trial summaries provide complete inference results without retaining a
	// rolled-back monomorphized body. Pure evaluation must consume the summary's
	// stored value instead of traversing that unavailable body.
	bool usesTrialInstantiationSummary = false;
	// For flex PatternCalls: the call-site-specific expanded expression selected
	// during type inference. Function-flex codegen clones this complete body
	// expression. Section-flex control-flow inspection reuses it as the inferred
	// header while structural codegen traverses the complete replacement section.
	Expression *inferredFlexExpansion{};
	// Section flexes own the complete call-site-specific replacement structure.
	std::shared_ptr<InstantiatedSectionBody> inferredFlexBody;
	std::optional<BranchSelection> branchSelection;
	// Reusable body clones point to the corresponding immutable template node.
	// Directly-owned expressions leave this null.
	Expression *reusableTemplateExpression{};

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

	// For expanded flex roots: actual argument indices that correspond to the
	// source pattern's parameter slots, in source order.
	std::vector<int> groupingArgumentIndices;
	// For each source-order grouping argument slot above, whether it had an
	// adjacent sibling parameter slot in the original matched pattern.
	std::vector<bool> groupingArgumentHasAdjacentSiblingSlot;
	// For expanded flex roots: whether the original matched pattern started or
	// ended with an argument slot. This lets operand regrouping keep working
	// after flex expansion removes the original PatternCall node.
	bool groupingStartsWithArgument = false;
	bool groupingEndsWithArgument = false;
	// Precedence of the source pattern that expanded into this expression root.
	int groupingPrecedence = 0;
};

template <typename Visitor>
inline bool visitExpressionTree(Expression *expression, Visitor &&visitor, std::unordered_set<Expression *> &visited) {
	if (!expression || !visited.insert(expression).second)
		return false;
	if (visitor(expression))
		return true;
	for (Expression *argument : expression->arguments) {
		if (visitExpressionTree(argument, visitor, visited))
			return true;
	}
	return visitExpressionTree(expression->inferredFlexExpansion, visitor, visited);
}

template <typename Visitor> inline bool visitExpressionTree(Expression *expression, Visitor &&visitor) {
	std::unordered_set<Expression *> visited;
	return visitExpressionTree(expression, std::forward<Visitor>(visitor), visited);
}

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
