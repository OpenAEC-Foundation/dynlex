#pragma once
#include <cstddef>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

struct CodeLine;
struct ParseContext;
struct InferenceContext;
struct Instantiation;
struct VariableStorageScope;
using InferenceInstanceSet =
	std::set<std::shared_ptr<const VariableStorageScope>, std::owner_less<std::shared_ptr<const VariableStorageScope>>>;

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
struct GroupingProduct {
	std::unordered_map<CodeLine *, GroupingTransition> groupings;
	InferenceInstanceSet requiredInstances, providedInstances;
};

struct GroupingTrace {
	enum class Kind { Grouping, RequiredInstance, ProvidedInstance };
	std::shared_ptr<const GroupingTrace> previous;
	CodeLine *line = nullptr;
	GroupingTransition transition;
	std::shared_ptr<const VariableStorageScope> instance{};
	Kind kind = Kind::Grouping;
};

bool applyGroupingProduct(ParseContext &, const std::shared_ptr<const GroupingProduct> &, InferenceContext *);
bool validateGroupingProduct(ParseContext &, const std::shared_ptr<const GroupingProduct> &, InferenceContext *);
bool supportsGroupingQuery(const Instantiation &, const InferenceContext *);
bool inferenceInstanceAvailable(ParseContext &, const std::shared_ptr<const VariableStorageScope> &, const InferenceContext *);
void recordInferenceInstance(InferenceContext &, const std::shared_ptr<const VariableStorageScope> &, bool provided);
