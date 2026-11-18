#pragma once
#include "compileTimeValue.h"
#include "expression.h"
#include "section.h"

namespace lsp {

// A nested definition has its own inference instances, even when it has no calls.
inline Section *sourceInstantiationOwner(Section *section) {
	for (Section *current = section; current; current = current->parent) {
		if (!current->patternDefinitions.empty() || !current->instantiations.empty())
			return current;
	}
	return nullptr;
}

inline const Expression *inferredSourceExpression(const Expression *source, const Instantiation *instance) {
	if (!instance)
		return source;
	return instance->body ? instance->body->findCloneOf(source) : nullptr;
}

template <typename ExpressionType>
inline ExpressionType *sourceVariableExpressionAt(ExpressionType *root, const SourceLocation &position) {
	if (!root)
		return nullptr;
	for (Expression *argument : root->arguments) {
		if (ExpressionType *found = sourceVariableExpressionAt<ExpressionType>(argument, position))
			return found;
	}
	if (root->kind != Expression::Kind::Variable || !root->range.line)
		return nullptr;
	SourceLocation start = root->range.sourceStart(), end = root->range.sourceEnd();
	if (start.sourceFile != position.sourceFile || end.sourceFile != position.sourceFile)
		return nullptr;
	if (std::pair(start.sourceFileLineIndex, start.column) <= std::pair(position.sourceFileLineIndex, position.column) &&
		std::pair(position.sourceFileLineIndex, position.column) < std::pair(end.sourceFileLineIndex, end.column))
		return root;
	return nullptr;
}

inline bool sourceExpressionHasConstant(const Expression *source) {
	Section *owner = sourceInstantiationOwner(source->range.line ? source->range.line->section : nullptr);
	if (!owner || owner->instantiations.empty())
		return isCompileTimeKnown(getExpressionCompileTimeValue(source));
	for (const auto &[key, instance] : owner->instantiations) {
		if (const Expression *expression = inferredSourceExpression(source, &instance)) {
			if (isCompileTimeKnown(getExpressionCompileTimeValue(expression)))
				return true;
		}
	}
	return false;
}

} // namespace lsp
