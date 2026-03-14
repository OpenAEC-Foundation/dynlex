#pragma once

#include "function.h"
#include "variableReference.h"
#include <cassert>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

struct BindingFrame {
	BindingMap bindings;

	explicit BindingFrame(BindingMap frameBindings = {}) : bindings(std::move(frameBindings)) {}
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

	Function *lookup(const std::string &bindingName) const {
		for (auto frameIt = frames.rbegin(); frameIt != frames.rend(); ++frameIt) {
			auto bindingIt = frameIt->bindings.find(bindingName);
			if (bindingIt != frameIt->bindings.end())
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
			if (!frame.bindings.empty())
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

inline Function *resolveVariableBindingOneStep(Function *expr, const BindingMap &bindings) {
	if (!expr || expr->kind != Function::Kind::Variable || !expr->variable)
		return expr;
	auto it = bindings.find(expr->variable->name);
	if (it != bindings.end() && it->second != expr)
		return it->second;
	return expr;
}

inline Function *
resolveVariableBindingChain(Function *expr, const BindingMap &bindings, size_t maxBindingResolutionDepth = 256) {
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

inline bool popBindingScope(BindingFrameStack &bindingFrameStack, BindingScopeTrail *scopeTrail = nullptr) {
	if (bindingFrameStack.depth() <= 1)
		return false;
	if (scopeTrail)
		scopeTrail->record(BindingFrame(bindingFrameStack.topBindings()));
	bindingFrameStack.popFrame();
	return true;
}

inline void pushBindingScope(BindingFrameStack &bindingFrameStack, BindingMap nextBindings) {
	bindingFrameStack.pushFrame(std::move(nextBindings));
}

inline void pushClearedBindingScope(BindingFrameStack &bindingFrameStack) { bindingFrameStack.pushFrame({}); }

inline void restoreBindingScopes(BindingFrameStack &bindingFrameStack, BindingScopeTrail &scopeTrail) {
	while (!scopeTrail.poppedFrames.empty()) {
		bindingFrameStack.pushFrame(std::move(scopeTrail.poppedFrames.top()));
		scopeTrail.poppedFrames.pop();
	}
}

inline Function *resolveVariableBindingAcrossScopes(
	Function *expr, BindingFrameStack &bindingFrameStack, BindingScopeTrail *scopeTrail = nullptr
) {
	while (expr && expr->kind == Function::Kind::Variable && expr->variable) {
		Function *resolvedExpression = resolveVariableBindingAcrossFrames(expr, bindingFrameStack);
		if (resolvedExpression == expr)
			return expr;
		expr = resolvedExpression;
		bool poppedScope = popBindingScope(bindingFrameStack, scopeTrail);
		assert(poppedScope && "Variable binding crossed scope without a parent binding frame");
	}
	return expr;
}

template <typename ExpandMacroPatternCallFn>
inline void resolveThroughBindingLayers(
	Function *&expr, BindingFrameStack &bindingFrameStack, ExpandMacroPatternCallFn &&expandMacroPatternCall
) {
	while (expr) {
		if (expr->kind == Function::Kind::Variable && expr->variable) {
			Function *resolvedExpression = resolveVariableBindingAcrossScopes(expr, bindingFrameStack);
			if (resolvedExpression != expr) {
				expr = resolvedExpression;
				continue;
			}
		}

		BindingMap innerBindings;
		Function *bodyExpression = expandMacroPatternCall(expr, innerBindings);
		if (bodyExpression) {
			pushBindingScope(bindingFrameStack, std::move(innerBindings));
			expr = bodyExpression;
			continue;
		}

		break;
	}
}
