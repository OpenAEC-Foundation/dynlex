#pragma once

#include "function.h"
#include "variableReference.h"
#include <cassert>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

struct BindingFrameStack {
	std::vector<std::unordered_map<std::string, Function *>> frames;

	void pushFrame(std::unordered_map<std::string, Function *> frame) { frames.push_back(std::move(frame)); }

	void popFrame() {
		assert(!frames.empty() && "Cannot pop an empty binding frame stack");
		frames.pop_back();
	}

	Function *lookup(const std::string &bindingName) const {
		for (auto frameIt = frames.rbegin(); frameIt != frames.rend(); ++frameIt) {
			auto bindingIt = frameIt->find(bindingName);
			if (bindingIt != frameIt->end())
				return bindingIt->second;
		}
		return nullptr;
	}

	std::unordered_map<std::string, Function *> flattenBindings() const {
		std::unordered_map<std::string, Function *> flattenedBindings;
		for (const auto &frame : frames) {
			for (const auto &[bindingName, functionExpression] : frame)
				flattenedBindings[bindingName] = functionExpression;
		}
		return flattenedBindings;
	}
};

inline Function *resolveVariableBindingOneStep(Function *expr, const std::unordered_map<std::string, Function *> &bindings) {
	if (!expr || expr->kind != Function::Kind::Variable || !expr->variable)
		return expr;
	auto it = bindings.find(expr->variable->name);
	if (it != bindings.end() && it->second != expr)
		return it->second;
	return expr;
}

inline Function *resolveVariableBindingChain(
	Function *expr, const std::unordered_map<std::string, Function *> &bindings, size_t maxBindingResolutionDepth = 256
) {
	size_t bindingResolutionDepth = 0;
	while (expr && expr->kind == Function::Kind::Variable && expr->variable) {
		Function *resolvedExpression = resolveVariableBindingOneStep(expr, bindings);
		if (resolvedExpression == expr)
			return expr;
		expr = resolvedExpression;
		bindingResolutionDepth++;
		if (bindingResolutionDepth > maxBindingResolutionDepth)
			return expr;
	}
	return expr;
}

inline Function *resolveVariableBindingAcrossFrames(
	Function *expr, const BindingFrameStack &bindingFrameStack, size_t maxBindingResolutionDepth = 256
) {
	size_t bindingResolutionDepth = 0;
	while (expr && expr->kind == Function::Kind::Variable && expr->variable) {
		Function *boundExpression = bindingFrameStack.lookup(expr->variable->name);
		if (!boundExpression || boundExpression == expr)
			return expr;
		expr = boundExpression;
		bindingResolutionDepth++;
		if (bindingResolutionDepth > maxBindingResolutionDepth)
			return expr;
	}
	return expr;
}

inline bool popBindingScope(
	std::unordered_map<std::string, Function *> &currentBindings,
	std::stack<std::unordered_map<std::string, Function *>> &parentBindingStack,
	std::vector<std::unordered_map<std::string, Function *>> *poppedBindingScopes = nullptr
) {
	if (parentBindingStack.empty())
		return false;
	if (poppedBindingScopes)
		poppedBindingScopes->push_back(currentBindings);
	currentBindings = parentBindingStack.top();
	parentBindingStack.pop();
	return true;
}

inline void pushBindingScope(
	std::unordered_map<std::string, Function *> &currentBindings,
	std::stack<std::unordered_map<std::string, Function *>> &parentBindingStack,
	std::unordered_map<std::string, Function *> nextBindings
) {
	parentBindingStack.push(currentBindings);
	currentBindings = std::move(nextBindings);
}

inline void pushClearedBindingScope(
	std::unordered_map<std::string, Function *> &currentBindings,
	std::stack<std::unordered_map<std::string, Function *>> &parentBindingStack
) {
	parentBindingStack.push(currentBindings);
	currentBindings.clear();
}

inline void restoreBindingScopes(
	std::unordered_map<std::string, Function *> &currentBindings,
	std::stack<std::unordered_map<std::string, Function *>> &parentBindingStack,
	const std::vector<std::unordered_map<std::string, Function *>> &poppedBindingScopes
) {
	for (auto poppedScopeIt = poppedBindingScopes.rbegin(); poppedScopeIt != poppedBindingScopes.rend(); ++poppedScopeIt) {
		parentBindingStack.push(currentBindings);
		currentBindings = *poppedScopeIt;
	}
}

inline Function *resolveVariableBindingAcrossScopes(
	Function *expr, std::unordered_map<std::string, Function *> &currentBindings,
	std::stack<std::unordered_map<std::string, Function *>> &parentBindingStack,
	std::vector<std::unordered_map<std::string, Function *>> *poppedBindingScopes = nullptr
) {
	while (expr && expr->kind == Function::Kind::Variable && expr->variable) {
		Function *resolvedExpression = resolveVariableBindingOneStep(expr, currentBindings);
		if (resolvedExpression == expr)
			return expr;
		expr = resolvedExpression;
		bool poppedScope = popBindingScope(currentBindings, parentBindingStack, poppedBindingScopes);
		assert(poppedScope && "Variable binding crossed scope without a parent binding frame");
	}
	return expr;
}

template <typename ExpandMacroPatternCallFn>
inline void resolveThroughBindingLayers(
	Function *&expr, std::unordered_map<std::string, Function *> &currentBindings,
	std::stack<std::unordered_map<std::string, Function *>> &parentBindingStack,
	ExpandMacroPatternCallFn &&expandMacroPatternCall
) {
	while (expr) {
		if (expr->kind == Function::Kind::Variable && expr->variable) {
			Function *resolvedExpression = resolveVariableBindingAcrossScopes(expr, currentBindings, parentBindingStack);
			if (resolvedExpression != expr) {
				expr = resolvedExpression;
				continue;
			}
		}

		std::unordered_map<std::string, Function *> innerBindings;
		Function *bodyExpression = expandMacroPatternCall(expr, innerBindings);
		if (bodyExpression) {
			pushBindingScope(currentBindings, parentBindingStack, std::move(innerBindings));
			expr = bodyExpression;
			continue;
		}

		break;
	}
}
