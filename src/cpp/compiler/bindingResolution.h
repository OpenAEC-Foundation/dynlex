#pragma once

#include "compilerUtils.h"
#include "expression.h"
#include "variableReference.h"
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

	template <typename FindBindingFn>
	Expression *lookupWithCallerScopeImpl(
		const Expression *ignoredExpression, BindingFrameStack &callerScope, FindBindingFn findBinding
	) const {
		for (size_t frameIndex = frames.size(); frameIndex > 0; frameIndex--) {
			Expression *boundExpression = findBinding(frames[frameIndex - 1]);
			if (!boundExpression || boundExpression == ignoredExpression)
				continue;
			callerScope.frames.assign(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(frameIndex - 1));
			return boundExpression;
		}
		return nullptr;
	}

  public:
	void pushFrame(BindingMap frameBindings) { frames.emplace_back(std::move(frameBindings)); }
	void pushFrame(BindingFrame frame) { frames.push_back(std::move(frame)); }

	void popFrame() {
		requireCompilerInvariant(!frames.empty(), "Cannot pop an empty binding frame stack");
		frames.pop_back();
	}

	bool empty() const { return frames.empty(); }
	size_t depth() const { return frames.size(); }

	BindingFrame &topFrame() {
		requireCompilerInvariant(!frames.empty(), "Cannot access top frame of empty binding frame stack");
		return frames.back();
	}

	const BindingFrame &topFrame() const {
		requireCompilerInvariant(!frames.empty(), "Cannot access top frame of empty binding frame stack");
		return frames.back();
	}

	Expression *lookupWithCallerScope(
		VariableReference *bindingReference, const Expression *ignoredExpression, BindingFrameStack &callerScope
	) const {
		VariableReference *bindingKey = normalizeBindingReference(bindingReference);
		if (!bindingKey)
			return nullptr;
		return lookupWithCallerScopeImpl(ignoredExpression, callerScope, [&](const BindingFrame &frame) -> Expression * {
			auto binding = frame.parameterBindings.find(bindingKey);
			return binding == frame.parameterBindings.end() ? nullptr : binding->second;
		});
	}

	Expression *lookupWithCallerScope(
		const std::string &bindingName, const Expression *ignoredExpression, BindingFrameStack &callerScope
	) const {
		return lookupWithCallerScopeImpl(ignoredExpression, callerScope, [&](const BindingFrame &frame) -> Expression * {
			auto binding = frame.bindings.find(bindingName);
			return binding == frame.bindings.end() ? nullptr : binding->second;
		});
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

inline bool popBindingScope(BindingFrameStack &bindingFrameStack) {
	if (bindingFrameStack.depth() <= 1)
		return false;
	bindingFrameStack.popFrame();
	return true;
}

inline void popBindingScopeOrFail(BindingFrameStack &bindingFrameStack, const char *failureMessage) {
	if (popBindingScope(bindingFrameStack))
		return;
	crashCompilerBug(failureMessage);
}

inline void pushBindingScope(BindingFrameStack &bindingFrameStack, BindingMap nextBindings) {
	bindingFrameStack.pushFrame(std::move(nextBindings));
}

inline void pushBindingScope(BindingFrameStack &bindingFrameStack, BindingFrame nextFrame) {
	bindingFrameStack.pushFrame(std::move(nextFrame));
}

struct ResolvedBindingLayers {
	Expression *expression{};
	BindingFrameStack bindingFrameStack;
};

inline ResolvedBindingLayers resolveBindingReferenceWithCallerScope(
	VariableReference *bindingReference, Expression *ignoredExpression, const BindingFrameStack &bindingFrameStack
) {
	if (!bindingReference)
		return {ignoredExpression, bindingFrameStack};
	BindingFrameStack callerScope;
	Expression *boundExpression = bindingFrameStack.lookupWithCallerScope(bindingReference, ignoredExpression, callerScope);
	return boundExpression ? ResolvedBindingLayers{boundExpression, std::move(callerScope)}
						   : ResolvedBindingLayers{ignoredExpression, bindingFrameStack};
}

inline ResolvedBindingLayers
resolveVariableBindingWithCallerScope(Expression *expression, const BindingFrameStack &bindingFrameStack) {
	if (!expression || expression->kind != Expression::Kind::Variable || !expression->variable)
		return {expression, bindingFrameStack};
	return resolveBindingReferenceWithCallerScope(expression->variable, expression, bindingFrameStack);
}

inline ResolvedBindingLayers resolveNamedBindingWithCallerScope(
	const std::string &name, Expression *expression, const BindingFrameStack &bindingFrameStack
) {
	BindingFrameStack callerScope;
	Expression *boundExpression = bindingFrameStack.lookupWithCallerScope(name, expression, callerScope);
	return boundExpression ? ResolvedBindingLayers{boundExpression, std::move(callerScope)}
						   : ResolvedBindingLayers{expression, bindingFrameStack};
}
