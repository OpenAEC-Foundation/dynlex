#pragma once

#include "classDefinition.h"
#include "classSection.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "definitionSection.h"
#include "function.h"
#include "intrinsicInfo.h"
#include "type.h"
#include "variable.h"
#include <cctype>
#include <cmath>
#include <optional>
#include <unordered_set>

// Resolve a Variable function through macro bindings to find the bound function.
// Only follows Variable → Variable chains; stops at non-Variable functions (PatternCall,
// IntrinsicCall, Literal, etc.). The caller handles those function kinds separately.
// See also: resolveThroughMacroLayers (codegen, codegenTypes.cpp) which additionally
// expands macro PatternCalls and operates on the context's binding stack.
static Function *resolveThroughBindings(Function *expr, const std::unordered_map<std::string, Function *> &bindings) {
	if (!expr || expr->kind != Function::Kind::Variable || !expr->variable)
		return expr;
	auto it = bindings.find(expr->variable->name);
	if (it != bindings.end() && it->second != expr)
		return resolveThroughBindings(it->second, bindings);
	return expr;
}

// Like resolveThroughBindings, but also expands macro PatternCalls to find the
// underlying function. Outputs the final active bindings in outBindings so the
// caller can resolve arguments of the returned function. Use when inspecting
// function kind matters (e.g., detecting a property intrinsic inside a store
// destination). See also: resolveThroughMacroLayers (codegen, codegenTypes.cpp)
// for the codegen equivalent that uses the context's binding stack.
static Function *resolveThroughBindingsDeepImpl(
	Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	std::unordered_map<std::string, Function *> &outBindings, std::unordered_set<Function *> &visited
) {
	expr = resolveThroughBindings(expr, bindings);
	outBindings = bindings;
	if (!expr)
		return expr;
	if (visited.contains(expr))
		return expr;
	visited.insert(expr);
	std::unordered_map<std::string, Function *> innerBindings;
	Function *bodyExpr = expandMacroPatternCall(expr, innerBindings);
	if (bodyExpr) {
		std::unordered_map<std::string, Function *> mergedBindings = bindings;
		for (auto &[name, argExpr] : innerBindings) {
			std::unordered_map<std::string, Function *> ignoredBindings;
			mergedBindings[name] = resolveThroughBindingsDeepImpl(argExpr, bindings, ignoredBindings, visited);
		}
		Function *resolved = resolveThroughBindingsDeepImpl(bodyExpr, mergedBindings, outBindings, visited);
		visited.erase(expr);
		return resolved;
	}
	visited.erase(expr);
	return expr;
}

static Function *resolveThroughBindingsDeep(
	Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	std::unordered_map<std::string, Function *> &outBindings
) {
	std::unordered_set<Function *> visited;
	return resolveThroughBindingsDeepImpl(expr, bindings, outBindings, visited);
}

static bool evaluateCompileTimeInteger(
	ParseContext &parseContext, Function *expr, const std::unordered_map<std::string, Function *> &bindings, int &outValue
) {
	CompileTimeValue value = evaluateCompileTimeValue(expr, parseContext, bindings);
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
static DataType resolveTypeThroughBindings(Function *expr, const std::unordered_map<std::string, Function *> &bindings);
static bool mergeArrayElementType(const DataType &current, const DataType &next, DataType &merged);
static bool expandsToSelectIntrinsic(Function *function);
static DataType instantiateBoundClassType(
	ParseContext &parseContext, ClassDefinition *classDef, const std::unordered_map<std::string, Function *> &bindings
);
static bool
instantiateClassFromArgumentTypes(ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef);

static thread_local ParseContext *activeTypeResolutionParseContext = nullptr;
static thread_local std::unordered_set<const Function *> activeTypeResolutionFunctions;

struct ActiveTypeResolutionParseContextGuard {
	ParseContext *previous;

	explicit ActiveTypeResolutionParseContextGuard(ParseContext &parseContext) : previous(activeTypeResolutionParseContext) {
		activeTypeResolutionParseContext = &parseContext;
	}

	~ActiveTypeResolutionParseContextGuard() { activeTypeResolutionParseContext = previous; }
};

static void appendPatternCallBindings(
	Function *expr, PatternDefinition *definition, std::unordered_map<std::string, Function *> &bindings
) {
	if (!expr || !definition || !expr->patternMatch)
		return;
	std::vector<Function *> sortedArgs = sortArgumentsByPosition(expr->arguments);
	size_t argIndex = 0;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		auto paramIt = node->parameterNames.find(definition);
		if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size())
			bindings[paramIt->second] = sortedArgs[argIndex++];
	}
}

static Function *selectCompileTimeBranch(
	Function *selectExpr, ParseContext &parseContext, const std::unordered_map<std::string, Function *> &bindings,
	const Instantiation *instantiation = nullptr
) {
	if (!selectExpr || selectExpr->intrinsicName != "select" || selectExpr->arguments.size() < 4)
		return nullptr;
	CompileTimeValue conditionValue = evaluateCompileTimeValue(selectExpr->arguments[1], parseContext, bindings, instantiation);
	std::optional<bool> condition = compileTimeTruthiness(conditionValue);
	if (!condition.has_value())
		return nullptr;
	return selectExpr->arguments[*condition ? 2 : 3];
}

static void markCompileTimeParameterRequirements(
	Function *expr, const std::unordered_map<std::string, Function *> &bindings, Instantiation *instantiation
) {
	if (!expr || !instantiation)
		return;

	std::unordered_set<Function *> visited;
	std::function<void(Function *)> visit = [&](Function *current) {
		if (!current || visited.contains(current))
			return;
		visited.insert(current);
		if (current->kind == Function::Kind::Variable && current->variable) {
			if (bindings.contains(current->variable->name))
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
	const std::vector<std::pair<std::string, Function *>> &paramBindings,
	const std::unordered_map<std::string, Function *> &callerBindings, const Instantiation *callerInstantiation
) {
	for (const auto &[name, argExpr] : paramBindings) {
		CompileTimeValue value = evaluateCompileTimeValue(argExpr, parseContext, callerBindings, callerInstantiation);
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
	if (!def)
		return indices;

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
	size_t argIndex = 0;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		auto paramIt = node->parameterNames.find(def);
		if (paramIt != node->parameterNames.end() && argIndex < expr->arguments.size())
			paramIndices[paramIt->second] = argIndex++;
	}

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
