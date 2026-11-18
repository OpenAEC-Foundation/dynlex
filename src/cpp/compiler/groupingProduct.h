#pragma once
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

struct CodeLine;
struct ParseContext;
struct InferenceContext;

// Node numbers refer to the source tree before inference changes its grouping.
// Keeping the layout separate lets a cached query publish it without demanding
// an otherwise unused source body.
struct GroupingLayout {
	struct Node {
		std::vector<size_t> arguments;
		bool explicitGroup;
		bool operator==(const Node &) const = default;
	};
	size_t root;
	std::vector<Node> nodes;
	bool ambiguityChecked;
	bool operator==(const GroupingLayout &) const = default;
};
using GroupingState = std::shared_ptr<const GroupingLayout>;

struct GroupingTransition {
	GroupingState input, output;
};
using GroupingProduct = std::unordered_map<CodeLine *, GroupingTransition>;

struct GroupingTrace {
	std::shared_ptr<const GroupingTrace> previous;
	CodeLine *line;
	GroupingTransition transition;
};

bool applyGroupingProduct(ParseContext &, const std::shared_ptr<const GroupingProduct> &, InferenceContext *);
bool validateGroupingProduct(const std::shared_ptr<const GroupingProduct> &, InferenceContext *);
