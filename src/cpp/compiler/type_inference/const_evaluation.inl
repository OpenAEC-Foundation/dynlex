#pragma once

#include "bindingResolution.h"
#include "classDefinition.h"
#include "classSection.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "definitionSection.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "type.h"
#include "variable.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <vector>

static Expression *resolveThroughBindings(Expression *expr, const BindingFrameStack &bindingFrameStack) {
	return resolveVariableBindingAcrossFrames(expr, bindingFrameStack);
}

static Expression *resolveThroughBindingsDeep(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack
);

static thread_local ParseContext *activeTypeResolutionParseContext = nullptr;

static void materializeFlexBindingsInCallerScope(BindingFrame &bindings, const BindingFrameStack &callerBindingFrameStack) {
	for (auto &[name, argumentExpression] : bindings.bindings) {
		(void)name;
		Expression *resolvedArgumentExpression = resolveThroughBindings(argumentExpression, callerBindingFrameStack);
		argumentExpression = resolvedArgumentExpression ? resolvedArgumentExpression : argumentExpression;
	}
	for (auto &[parameterDefinition, argumentExpression] : bindings.parameterBindings) {
		(void)parameterDefinition;
		Expression *resolvedArgumentExpression = resolveThroughBindings(argumentExpression, callerBindingFrameStack);
		argumentExpression = resolvedArgumentExpression ? resolvedArgumentExpression : argumentExpression;
	}
}

// Like resolveThroughBindings, but also expands flex PatternCalls to find the
// underlying expression. Outputs the final active bindings in outBindings so the
// caller can resolve arguments of the returned expression. Use when inspecting
// expression kind matters (e.g., detecting a property intrinsic inside a store
// destination). See also: resolveThroughFlexLayers (codegen, codegenTypes.cpp)
// for the codegen equivalent that uses the context's binding stack.
static Expression *resolveThroughBindingsDeepImpl(
	Expression *expr, BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack,
	std::unordered_set<Expression *> &visited
) {
	expr = resolveThroughBindings(expr, bindingFrameStack);
	outBindingFrameStack = bindingFrameStack;
	if (!expr)
		return expr;
	if (visited.contains(expr))
		return expr;
	visited.insert(expr);
	BindingFrame innerBindings;
	Expression *bodyExpr = activeTypeResolutionParseContext
							   ? expandFlexPatternCall(*activeTypeResolutionParseContext, expr, innerBindings)
							   : nullptr;
	if (bodyExpr) {
		materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
		bindingFrameStack.pushFrame(std::move(innerBindings));
		Expression *resolved = resolveThroughBindingsDeepImpl(bodyExpr, bindingFrameStack, outBindingFrameStack, visited);
		bindingFrameStack.popFrame();
		visited.erase(expr);
		return resolved;
	}
	visited.erase(expr);
	return expr;
}

static Expression *resolveThroughBindingsDeep(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack
) {
	BindingFrameStack localBindingFrameStack = bindingFrameStack;
	std::unordered_set<Expression *> visited;
	return resolveThroughBindingsDeepImpl(expr, localBindingFrameStack, outBindingFrameStack, visited);
}

// Convenience: resolve an expression through bindings, then return its type.
static DataType concretizeClassType(DataType type);
static std::string extractFieldName(Expression *expr);
static DataType resolveBuiltInPropertyType(const DataType &ownerType, const std::string &fieldName);
static DataType resolveKnownExpressionType(Expression *expr, const BindingFrameStack &bindingFrameStack);
static bool mergeArrayElementType(const DataType &current, const DataType &next, DataType &merged);
static DataType instantiateBoundClassType(
	ParseContext &parseContext, ClassDefinition *classDef, const BindingFrameStack &bindingFrameStack,
	InferenceContext *inferenceContext = nullptr
);
static bool instantiateClassFromArgumentTypes(
	ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef, int baseClassInstIndex = -1
);

struct BindingContext {
	std::unordered_map<std::string, const Expression *> bindingEntries;
	std::unordered_map<const VariableReference *, const Expression *> parameterBindingEntries;
	size_t fingerprint = 0;

	bool operator==(const BindingContext &other) const {
		return fingerprint == other.fingerprint && bindingEntries == other.bindingEntries &&
			   parameterBindingEntries == other.parameterBindingEntries;
	}
};

struct TypeResolutionKey {
	const Expression *expression = nullptr;
	BindingContext bindingContext;

	bool operator==(const TypeResolutionKey &other) const {
		return expression == other.expression && bindingContext == other.bindingContext;
	}
};

struct TypeResolutionKeyHasher {
	size_t operator()(const TypeResolutionKey &typeResolutionKey) const {
		size_t hashValue = std::hash<const Expression *>{}(typeResolutionKey.expression);
		hashValue ^= typeResolutionKey.bindingContext.fingerprint + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
		return hashValue;
	}
};

static thread_local std::unordered_set<TypeResolutionKey, TypeResolutionKeyHasher> activeTypeResolutionKeys;

static bool containsActiveTypeResolutionKey(const TypeResolutionKey &typeResolutionKey) {
	return activeTypeResolutionKeys.contains(typeResolutionKey);
}

static void pushActiveTypeResolutionKey(const TypeResolutionKey &typeResolutionKey) {
	activeTypeResolutionKeys.insert(typeResolutionKey);
}

static void popActiveTypeResolutionKey(const TypeResolutionKey &typeResolutionKey) {
	activeTypeResolutionKeys.erase(typeResolutionKey);
}

struct ActiveTypeResolutionParseContextGuard {
	ParseContext *previous;

	explicit ActiveTypeResolutionParseContextGuard(ParseContext &parseContext) : previous(activeTypeResolutionParseContext) {
		activeTypeResolutionParseContext = &parseContext;
	}

	~ActiveTypeResolutionParseContextGuard() { activeTypeResolutionParseContext = previous; }
};

static bool markCompileTimeParameterRequirements(
	Expression *expr, const BindingFrameStack &bindingFrameStack, Instantiation *instantiation
) {
	if (!expr || !instantiation)
		return false;

	bool changed = false;
	std::unordered_set<Expression *> visited;
	std::function<void(Expression *)> visit = [&](Expression *current) {
		if (!current || visited.contains(current))
			return;
		visited.insert(current);
		if (current->kind == Expression::Kind::Variable && current->variable) {
			if (bindingFrameStack.lookup(current->variable)) {
				auto [it, inserted] = instantiation->requiredCompileTimeParameters.insert(current->variable->name);
				(void)it;
				if (inserted)
					changed = true;
			}
			return;
		}
		for (Expression *arg : current->arguments)
			visit(arg);
	};
	visit(expr);
	return changed;
}

template <typename ReadCompileTimeValueFn>
static void seedInstantiationCompileTimeParameters(
	Instantiation &instantiation, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, ReadCompileTimeValueFn &&readCompileTimeValue
) {
	size_t bindingCount = std::min(paramBindings.size(), argTypes.size());
	for (size_t i = 0; i < bindingCount; i++) {
		const auto &[name, argExpr] = paramBindings[i];
		if (!argExpr)
			crashCompilerBug("missing instantiation parameter expression while seeding compile-time values");
		if (argTypes[i].kind == DataType::Kind::Type) {
			instantiation.constantParameterValues[name] = argTypes[i];
			continue;
		}
		CompileTimeValue value = readCompileTimeValue(argExpr);
		if (isCompileTimeKnown(value))
			instantiation.constantParameterValues[name] = value;
		else
			instantiation.constantParameterValues.erase(name);
	}
}
