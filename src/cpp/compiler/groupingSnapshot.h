#pragma once

#include <unordered_map>
#include <vector>

struct Expression;

struct GroupingSnapshot {
	struct NodeState {
		std::vector<Expression *> arguments;
		bool explicitGroup;

		bool operator==(const NodeState &) const = default;
	};

	Expression *root{};
	std::unordered_map<Expression *, NodeState> nodes;

	bool operator==(const GroupingSnapshot &) const = default;
};
