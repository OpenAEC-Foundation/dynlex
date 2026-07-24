#pragma once

#include "expression.h"
#include <vector>

struct ExpressionInvocationIdentity {
	// Each component identifies one call in the active expansion path. Template
	// identity keeps the path stable when inference recreates reusable bodies.
	std::vector<const Expression *> path;

	bool operator==(const ExpressionInvocationIdentity &) const = default;
};

inline const Expression *stableExpressionIdentity(const Expression *expression) {
	return expression && expression->reusableTemplateExpression ? expression->reusableTemplateExpression : expression;
}

inline ExpressionInvocationIdentity expressionInvocationIdentity(const std::vector<Expression *> &invocationPath) {
	ExpressionInvocationIdentity identity;
	identity.path.reserve(invocationPath.size());
	for (Expression *expression : invocationPath)
		identity.path.push_back(stableExpressionIdentity(expression));
	return identity;
}
