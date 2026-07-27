#pragma once

#include <unordered_map>
#include <vector>

struct Expression;

struct GroupingSnapshot {
	struct NodeState {
		std::vector<Expression *> arguments;
		bool explicitGroup;
	};

	Expression *root{};
	std::unordered_map<Expression *, NodeState> nodes;
};
