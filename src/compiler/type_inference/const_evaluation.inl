#pragma once

#include "bindingResolution.h"
#include "classDefinition.h"
#include "classSection.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "definitionSection.h"
#include "function.h"
#include "intrinsicInfo.h"
#include "type.h"
#include "variable.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <vector>

static Function *resolveThroughBindings(Function *expr, const BindingFrameStack &bindingFrameStack) {
	return resolveVariableBindingAcrossFrames(expr, bindingFrameStack);
}

static Function *
resolveThroughBindingsDeep(Function *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack);

static bool
expressionReferencesAnyBindingName(Function *expr, const BindingMap &bindingNames, std::unordered_set<Function *> &visited) {
	if (!expr || visited.contains(expr))
		return false;
	visited.insert(expr);
	if (expr->kind == Function::Kind::Variable && expr->variable && bindingNames.contains(expr->variable->name))
		return true;
	for (Function *arg : expr->arguments) {
		if (expressionReferencesAnyBindingName(arg, bindingNames, visited))
			return true;
	}
	return false;
}

static bool expressionReferencesAnyBindingName(Function *expr, const BindingMap &bindingNames) {
	std::unordered_set<Function *> visited;
	return expressionReferencesAnyBindingName(expr, bindingNames, visited);
}

// Like resolveThroughBindings, but also expands macro PatternCalls to find the
// underlying function. Outputs the final active bindings in outBindings so the
// caller can resolve arguments of the returned function. Use when inspecting
// function kind matters (e.g., detecting a property intrinsic inside a store
// destination). See also: resolveThroughMacroLayers (codegen, codegenTypes.cpp)
// for the codegen equivalent that uses the context's binding stack.
static Function *resolveThroughBindingsDeepImpl(
	Function *expr, BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack,
	std::unordered_set<Function *> &visited
) {
	expr = resolveThroughBindings(expr, bindingFrameStack);
	outBindingFrameStack = bindingFrameStack;
	if (!expr)
		return expr;
	if (visited.contains(expr))
		return expr;
	visited.insert(expr);
	BindingMap innerBindings;
	Function *bodyExpr = expandMacroPatternCall(expr, innerBindings);
	if (bodyExpr) {
		BindingMap scopedMacroBindings;
		scopedMacroBindings.reserve(innerBindings.size());
		for (auto &[name, argExpr] : innerBindings) {
			Function *directArg = resolveThroughBindings(argExpr, bindingFrameStack);
			Function *bindingArg = directArg ? directArg : argExpr;
			if (bindingArg && expressionReferencesAnyBindingName(bindingArg, innerBindings))
				bindingArg = directArg ? directArg : argExpr;
			scopedMacroBindings[name] = bindingArg;
		}
		bindingFrameStack.pushFrame(std::move(scopedMacroBindings));
		Function *resolved = resolveThroughBindingsDeepImpl(bodyExpr, bindingFrameStack, outBindingFrameStack, visited);
		bindingFrameStack.popFrame();
		visited.erase(expr);
		return resolved;
	}
	visited.erase(expr);
	return expr;
}

static Function *resolveThroughBindingsDeep(
	Function *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &outBindingFrameStack
) {
	BindingFrameStack localBindingFrameStack = bindingFrameStack;
	std::unordered_set<Function *> visited;
	return resolveThroughBindingsDeepImpl(expr, localBindingFrameStack, outBindingFrameStack, visited);
}

static bool evaluateCompileTimeInteger(
	ParseContext &parseContext, Function *expr, const BindingFrameStack &bindingFrameStack, int &outValue
) {
	CompileTimeValue value = evaluateCompileTimeValue(expr, parseContext, bindingFrameStack);
	auto *number = std::get_if<double>(&value);
	if (!number)
		return false;
	outValue = static_cast<int>(*number);
	return *number == static_cast<double>(outValue);
}

// Convenience: resolve an function through bindings, then return its type.
static DataType concretizeClassType(DataType type);
static std::string extractFieldName(Function *expr);
static DataType resolveBuiltInPropertyType(const DataType &ownerType, const std::string &fieldName);
static DataType resolveTypeThroughBindings(Function *expr, const BindingFrameStack &bindingFrameStack);
static bool mergeArrayElementType(const DataType &current, const DataType &next, DataType &merged);
static bool expandsToSelectIntrinsic(Function *function);
static DataType
instantiateBoundClassType(ParseContext &parseContext, ClassDefinition *classDef, const BindingFrameStack &bindingFrameStack);
static bool instantiateClassFromArgumentTypes(
	ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef, int baseClassInstIndex = -1
);

struct BindingContext {
	std::unordered_map<std::string, const Function *> bindingEntries;
	size_t fingerprint = 0;

	bool operator==(const BindingContext &other) const {
		return fingerprint == other.fingerprint && bindingEntries == other.bindingEntries;
	}
};

struct TypeResolutionKey {
	const Function *functionExpression = nullptr;
	BindingContext bindingContext;

	bool operator==(const TypeResolutionKey &other) const {
		return functionExpression == other.functionExpression && bindingContext == other.bindingContext;
	}
};

static thread_local ParseContext *activeTypeResolutionParseContext = nullptr;
struct TypeResolutionKeyHasher {
	size_t operator()(const TypeResolutionKey &typeResolutionKey) const {
		size_t hashValue = std::hash<const Function *>{}(typeResolutionKey.functionExpression);
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

static Function *selectCompileTimeBranch(
	Function *selectExpr, ParseContext &parseContext, const BindingFrameStack &bindingFrameStack,
	const Instantiation *instantiation = nullptr
) {
	if (!selectExpr || selectExpr->intrinsicName != "select" || selectExpr->arguments.size() < 4)
		return nullptr;
	CompileTimeValue conditionValue =
		evaluateCompileTimeValue(selectExpr->arguments[1], parseContext, bindingFrameStack, instantiation);
	std::optional<bool> condition = compileTimeTruthiness(conditionValue);
	if (!condition.has_value())
		return nullptr;
	return selectExpr->arguments[*condition ? 2 : 3];
}

static void
markCompileTimeParameterRequirements(Function *expr, const BindingFrameStack &bindingFrameStack, Instantiation *instantiation) {
	if (!expr || !instantiation)
		return;

	std::unordered_set<Function *> visited;
	std::function<void(Function *)> visit = [&](Function *current) {
		if (!current || visited.contains(current))
			return;
		visited.insert(current);
		if (current->kind == Function::Kind::Variable && current->variable) {
			if (bindingFrameStack.lookup(current->variable->name))
				instantiation->requiredCompileTimeParameters.insert(current->variable->name);
			return;
		}
		for (Function *arg : current->arguments)
			visit(arg);
	};
	visit(expr);
}

static void seedInstantiationCompileTimeParameters(
	ParseContext &parseContext, Instantiation &instantiation,
	const std::vector<std::pair<std::string, Function *>> &paramBindings, const BindingFrameStack &callerBindingFrameStack,
	const Instantiation *callerInstantiation
) {
	for (const auto &[name, argExpr] : paramBindings) {
		CompileTimeValue value = evaluateCompileTimeValue(argExpr, parseContext, callerBindingFrameStack, callerInstantiation);
		if (isCompileTimeKnown(value))
			instantiation.constantParameterValues[name] = value;
		else
			instantiation.constantParameterValues.erase(name);
	}
}

static std::unordered_set<size_t> compileTimeOnlyArgumentIndices(Function *expr) {
	std::unordered_set<size_t> indices;
	if (!expr || expr->kind != Function::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return indices;

	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return indices;
	std::vector<DataType> argTypesForOverload;
	for (Function *arg : expr->arguments)
		argTypesForOverload.push_back(resolveTypeThroughBindings(arg, {}));
	PatternDefinition *def = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload);
	if (!def) {
		if (defs.size() == 1)
			def = defs.front();
		else
			return indices;
	}

	if (!def->section || !def->section->isMacro)
		return indices;
	Function *bodyExpr = nullptr;
	for (Section *child : def->section->children) {
		for (CodeLine *line : child->codeLines) {
			if (line->function)
				bodyExpr = line->function;
		}
	}
	if (!bodyExpr || bodyExpr->kind != Function::Kind::IntrinsicCall)
		return indices;

	std::unordered_map<std::string, size_t> paramIndices;
	std::vector<std::pair<std::string, Function *>> orderedBindings;
	collectPatternCallBindingPairs(expr, def, orderedBindings);
	for (size_t argumentIndex = 0; argumentIndex < orderedBindings.size(); argumentIndex++)
		paramIndices[orderedBindings[argumentIndex].first] = argumentIndex;

	for (size_t i = 1; i < bodyExpr->arguments.size(); i++) {
		if (!intrinsicArgumentIsCompileTimeOnly(bodyExpr->intrinsicName, static_cast<int>(i)))
			continue;
		Function *arg = bodyExpr->arguments[i];
		if (arg && arg->kind == Function::Kind::Variable && arg->variable) {
			auto it = paramIndices.find(arg->variable->name);
			if (it != paramIndices.end())
				indices.insert(it->second);
		}
	}
	return indices;
}
