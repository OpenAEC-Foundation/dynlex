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
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack,
	InferenceContext *inferenceContext = nullptr
);

static Expression *lookupInferenceFlexExpansion(InferenceContext *inferenceContext, Expression *expression);

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

// Follow bindings and flex expansions that inference has already finalized.
// Outputs the final active bindings so callers can inspect the selected body
// without performing another overload selection or expansion.
static Expression *resolveThroughBindingsDeepImpl(
	Expression *expr, BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack,
	std::unordered_set<Expression *> &visited, InferenceContext *inferenceContext
) {
	while (expr && expr->kind == Expression::Kind::Variable && expr->variable) {
		BindingFrameStack callerScope;
		Expression *boundExpression = bindingFrameStack.lookupWithCallerScope(expr->variable, expr, callerScope);
		if (!boundExpression)
			break;
		expr = boundExpression;
		bindingFrameStack = std::move(callerScope);
	}
	outBindingFrameStack = bindingFrameStack;
	if (!expr)
		return expr;
	if (visited.contains(expr))
		return expr;
	visited.insert(expr);
	BindingFrame innerBindings;
	Expression *bodyExpr = lookupInferenceFlexExpansion(inferenceContext, expr);
	if (bodyExpr) {
		PatternDefinition *definition = expr->selectedPatternDefinition;
		if (!definition || !definition->section || !definition->section->isFlex)
			crashCompilerBug("inferred flex expansion has no selected flex definition");
		collectPatternCallBindings(expr, definition, innerBindings);
	}
	if (bodyExpr) {
		materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
		bindingFrameStack.pushFrame(std::move(innerBindings));
		Expression *resolved =
			resolveThroughBindingsDeepImpl(bodyExpr, bindingFrameStack, outBindingFrameStack, visited, inferenceContext);
		bindingFrameStack.popFrame();
		visited.erase(expr);
		return resolved;
	}
	visited.erase(expr);
	return expr;
}

static Expression *resolveThroughBindingsDeep(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack,
	InferenceContext *inferenceContext
) {
	BindingFrameStack localBindingFrameStack = bindingFrameStack;
	std::unordered_set<Expression *> visited;
	return resolveThroughBindingsDeepImpl(expr, localBindingFrameStack, outBindingFrameStack, visited, inferenceContext);
}

// Convenience: resolve an expression through bindings, then return its type.
static std::string extractFieldName(Expression *expr);
static DataType resolveBuiltInPropertyType(const DataType &ownerType, const std::string &fieldName);
static DataType resolveKnownExpressionType(
	Expression *expr, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext = nullptr
);
static bool mergeArrayElementType(const DataType &current, const DataType &next, DataType &merged);
static DataType instantiateBoundClassType(
	ParseContext &parseContext, ClassDefinition *classDef, const BindingFrameStack &bindingFrameStack,
	InferenceContext *inferenceContext = nullptr, const std::vector<DataType> *constructionArgumentTypes = nullptr
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

static void seedInstantiationParameterTypes(
	Instantiation &instantiation, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes
) {
	instantiation.parameterTypesByName.clear();
	size_t bindingCount = std::min(paramBindings.size(), argTypes.size());
	for (size_t i = 0; i < bindingCount; i++)
		instantiation.parameterTypesByName[paramBindings[i].first] = argTypes[i];
}

template <typename ReadCompileTimeValueFn>
static void seedInstantiationCompileTimeParameters(
	Instantiation &instantiation, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, const std::unordered_set<std::string> &requiredCompileTimeParameters,
	ReadCompileTimeValueFn &&readCompileTimeValue
) {
	size_t bindingCount = std::min(paramBindings.size(), argTypes.size());
	for (size_t i = 0; i < bindingCount; i++) {
		const auto &[name, argExpr] = paramBindings[i];
		if (!argExpr)
			crashCompilerBug("missing instantiation parameter expression while seeding compile-time values");
		if (!parameterRequiresCompileTimeInstantiationValue(requiredCompileTimeParameters, name, argTypes[i])) {
			instantiation.constantParameterValues.erase(name);
			continue;
		}
		CompileTimeValue value = readCompileTimeValue(argExpr);
		if (!isCompileTimeKnown(value) && argTypes[i].kind == DataType::Kind::Type)
			value = TypeReferenceValue::exact(argTypes[i]);
		if (isCompileTimeKnown(value))
			instantiation.constantParameterValues[name] = value;
		else
			instantiation.constantParameterValues.erase(name);
	}
}
