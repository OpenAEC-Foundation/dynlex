#pragma once

#include <unordered_map>
#include <vector>

struct Expression;

struct GroupingSnapshot {
	Expression *root{};
	std::unordered_map<Expression *, std::vector<Expression *>> argumentsByExpression;
	std::unordered_map<Expression *, bool> explicitGroupByExpression;
};
