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

static Expression *lookupInferenceFlexExpansion(InferenceContext *inferenceContext, Expression *expression);

static thread_local ParseContext *activeTypeResolutionParseContext = nullptr;

static std::optional<FlexBindingExpansion>
selectInferenceFlexBindingExpansion(InferenceContext *inferenceContext, Expression *expression) {
	Expression *bodyExpression = lookupInferenceFlexExpansion(inferenceContext, expression);
	if (!bodyExpression)
		return std::nullopt;
	return FlexBindingExpansion{expression->selectedPatternDefinition, bodyExpression};
}

template <typename StopFn>
static ResolvedBindingLayers resolveInferenceBindingLayers(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext, StopFn &&stop
) {
	return resolveThroughBindingLayers(expression, bindingFrameStack, [&](Expression *currentExpression) {
		return selectInferenceFlexBindingExpansion(inferenceContext, currentExpression);
	}, std::forward<StopFn>(stop));
}

static ResolvedBindingLayers resolveInferenceBindingLayers(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext
) {
	return resolveInferenceBindingLayers(expression, bindingFrameStack, inferenceContext, [](Expression *) {
		return false;
	});
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
