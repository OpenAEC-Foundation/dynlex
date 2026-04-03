#pragma once

#include "expression.h"
#include "variableReference.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

inline VariableReference *normalizeBindingReference(VariableReference *reference) {
	return reference && reference->definition ? reference->definition : reference;
}

inline const VariableReference *normalizeBindingReference(const VariableReference *reference) {
	return reference && reference->definition ? reference->definition : reference;
}

struct BindingFrame {
	BindingMap bindings;
	ParameterBindingMap parameterBindings;

	BindingFrame() = default;
	explicit BindingFrame(BindingMap frameBindings) : bindings(std::move(frameBindings)) {}
	explicit BindingFrame(ParameterBindingMap frameParameterBindings) : parameterBindings(std::move(frameParameterBindings)) {}
	BindingFrame(BindingMap frameBindings, ParameterBindingMap frameParameterBindings)
		: bindings(std::move(frameBindings)), parameterBindings(std::move(frameParameterBindings)) {}

	bool empty() const { return bindings.empty() && parameterBindings.empty(); }
};

struct BindingFrameStack {
  private:
	std::vector<BindingFrame> frames;

  public:
	void pushFrame(BindingMap frameBindings) { frames.emplace_back(std::move(frameBindings)); }
	void pushFrame(BindingFrame frame) { frames.push_back(std::move(frame)); }

	void popFrame() {
		assert(!frames.empty() && "Cannot pop an empty binding frame stack");
		frames.pop_back();
	}

	bool empty() const { return frames.empty(); }
	size_t depth() const { return frames.size(); }
	bool hasParentScope() const { return frames.size() > 1; }

	BindingFrame &topFrame() {
		assert(!frames.empty() && "Cannot access top frame of empty binding frame stack");
		return frames.back();
	}

	const BindingFrame &topFrame() const {
		assert(!frames.empty() && "Cannot access top frame of empty binding frame stack");
		return frames.back();
	}

	BindingMap &topBindings() {
		assert(!frames.empty() && "Cannot access top bindings of empty binding frame stack");
		return frames.back().bindings;
	}

	const BindingMap &topBindings() const {
		assert(!frames.empty() && "Cannot access top bindings of empty binding frame stack");
		return frames.back().bindings;
	}

	void replaceTopBindings(BindingMap frameBindings) {
		assert(!frames.empty() && "Cannot replace top bindings of empty binding frame stack");
		frames.back().bindings = std::move(frameBindings);
	}

	Expression *lookup(VariableReference *bindingReference) const {
		VariableReference *bindingKey = normalizeBindingReference(bindingReference);
		if (!bindingKey)
			return nullptr;
		for (auto frameIt = frames.rbegin(); frameIt != frames.rend(); ++frameIt) {
			auto bindingIt = frameIt->parameterBindings.find(bindingKey);
			if (bindingIt != frameIt->parameterBindings.end())
				return bindingIt->second;
		}
		return nullptr;
	}

	Expression *lookup(const std::string &bindingName) const {
		for (auto frameIt = frames.rbegin(); frameIt != frames.rend(); ++frameIt) {
			auto bindingIt = frameIt->bindings.find(bindingName);
			if (bindingIt != frameIt->bindings.end())
				return bindingIt->second;
		}
		return nullptr;
	}

	Expression *lookupSkippingExpression(VariableReference *bindingReference, const Expression *ignoredExpression) const {
		VariableReference *bindingKey = normalizeBindingReference(bindingReference);
		if (!bindingKey)
			return nullptr;
		for (auto frameIt = frames.rbegin(); frameIt != frames.rend(); ++frameIt) {
			auto bindingIt = frameIt->parameterBindings.find(bindingKey);
			if (bindingIt == frameIt->parameterBindings.end())
				continue;
			if (bindingIt->second == ignoredExpression)
				continue;
			return bindingIt->second;
		}
		return nullptr;
	}

	Expression *lookupSkippingExpression(const std::string &bindingName, const Expression *ignoredExpression) const {
		for (auto frameIt = frames.rbegin(); frameIt != frames.rend(); ++frameIt) {
			auto bindingIt = frameIt->bindings.find(bindingName);
			if (bindingIt == frameIt->bindings.end())
				continue;
			if (bindingIt->second == ignoredExpression)
				continue;
			return bindingIt->second;
		}
		return nullptr;
	}

	template <typename Visitor> void forEachFrame(Visitor &&visitor) const {
		for (const BindingFrame &frame : frames)
			visitor(frame);
	}

	bool hasBindings() const {
		for (const auto &frame : frames) {
			if (!frame.empty())
				return true;
		}
		return false;
	}
};

struct BindingScopeTrail {
	std::stack<BindingFrame> poppedFrames;

	void record(BindingFrame frame) { poppedFrames.push(std::move(frame)); }
	bool empty() const { return poppedFrames.empty(); }
};

inline BindingFrameStack makeBindingFrameStack(const BindingMap &bindings) {
	BindingFrameStack bindingFrameStack;
	bindingFrameStack.pushFrame(bindings);
	return bindingFrameStack;
}

inline BindingFrameStack makeBindingFrameStack(const BindingFrame &bindingFrame) {
	BindingFrameStack bindingFrameStack;
	bindingFrameStack.pushFrame(bindingFrame);
	return bindingFrameStack;
}

inline Expression *resolveVariableBindingOneStep(Expression *expr, const BindingMap &bindings) {
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return expr;
	auto it = bindings.find(expr->variable->name);
	if (it != bindings.end() && it->second != expr)
		return it->second;
	return expr;
}

inline Expression *
resolveVariableBindingChain(Expression *expr, const BindingMap &bindings, size_t maxBindingResolutionDepth = 256) {
	size_t bindingResolutionDepth = 0;
	while (expr && expr->kind == Expression::Kind::Variable && expr->variable) {
		Expression *resolvedExpression = resolveVariableBindingOneStep(expr, bindings);
		if (resolvedExpression == expr)
			return expr;
		expr = resolvedExpression;
		bindingResolutionDepth++;
		if (bindingResolutionDepth > maxBindingResolutionDepth)
			return expr;
	}
	return expr;
}

inline Expression *resolveVariableBindingAcrossFrames(
	Expression *expr, const BindingFrameStack &bindingFrameStack, size_t maxBindingResolutionDepth = 256
) {
	(void)maxBindingResolutionDepth;
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return expr;
	Expression *boundExpression = bindingFrameStack.lookupSkippingExpression(expr->variable, expr);
	if (!boundExpression)
		return expr;
	return boundExpression;
}

inline bool popBindingScope(BindingFrameStack &bindingFrameStack, BindingScopeTrail *scopeTrail = nullptr) {
	if (bindingFrameStack.depth() <= 1)
		return false;
	if (scopeTrail)
		scopeTrail->record(bindingFrameStack.topFrame());
	bindingFrameStack.popFrame();
	return true;
}

inline void popBindingScopeOrFail(
	BindingFrameStack &bindingFrameStack, const char *failureMessage, BindingScopeTrail *scopeTrail = nullptr
) {
	if (popBindingScope(bindingFrameStack, scopeTrail))
		return;
	std::fputs(failureMessage, stderr);
	std::fputc('\n', stderr);
	std::abort();
}

inline void pushBindingScope(BindingFrameStack &bindingFrameStack, BindingMap nextBindings) {
	bindingFrameStack.pushFrame(std::move(nextBindings));
}

inline void pushBindingScope(BindingFrameStack &bindingFrameStack, BindingFrame nextFrame) {
	bindingFrameStack.pushFrame(std::move(nextFrame));
}

inline void pushClearedBindingScope(BindingFrameStack &bindingFrameStack) { bindingFrameStack.pushFrame(BindingFrame{}); }

inline void restoreBindingScopes(BindingFrameStack &bindingFrameStack, BindingScopeTrail &scopeTrail) {
	while (!scopeTrail.poppedFrames.empty()) {
		bindingFrameStack.pushFrame(std::move(scopeTrail.poppedFrames.top()));
		scopeTrail.poppedFrames.pop();
	}
}

inline Expression *resolveVariableBindingAcrossScopes(
	Expression *expr, BindingFrameStack &bindingFrameStack, BindingScopeTrail *scopeTrail = nullptr
) {
	while (expr && expr->kind == Expression::Kind::Variable && expr->variable) {
		Expression *resolvedExpression = resolveVariableBindingAcrossFrames(expr, bindingFrameStack);
		if (resolvedExpression == expr)
			return expr;
		expr = resolvedExpression;
		popBindingScopeOrFail(bindingFrameStack, "Variable binding crossed scope without a parent binding frame", scopeTrail);
	}
	return expr;
}

template <typename ExpandFlexPatternCallFn>
inline void resolveThroughBindingLayers(
	Expression *&expr, BindingFrameStack &bindingFrameStack, ExpandFlexPatternCallFn &&expandFlexPatternCall
) {
	while (expr) {
		if (expr->kind == Expression::Kind::Variable && expr->variable) {
			Expression *resolvedExpression = resolveVariableBindingAcrossScopes(expr, bindingFrameStack);
			if (resolvedExpression != expr) {
				expr = resolvedExpression;
				continue;
			}
		}

		BindingFrame innerBindings;
		Expression *bodyExpression = expandFlexPatternCall(expr, innerBindings);
		if (bodyExpression) {
			pushBindingScope(bindingFrameStack, std::move(innerBindings));
			expr = bodyExpression;
			continue;
		}

		break;
	}
}
