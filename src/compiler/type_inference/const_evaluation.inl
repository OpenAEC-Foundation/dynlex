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
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <unordered_set>
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

static Function *resolveThroughBindings(Function *expr, const BindingFrameStack &bindingFrameStack) {
	constexpr size_t maxBindingResolutionDepth = 256;
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

// Resolve a Variable function through macro bindings to find the bound function.
// Only follows Variable → Variable chains; stops at non-Variable functions (PatternCall,
// IntrinsicCall, Literal, etc.). The caller handles those function kinds separately.
// See also: resolveThroughMacroLayers (codegen, codegenTypes.cpp) which additionally
// expands macro PatternCalls and operates on the context's binding stack.
static Function *resolveThroughBindings(Function *expr, const std::unordered_map<std::string, Function *> &bindings) {
	BindingFrameStack bindingFrameStack;
	bindingFrameStack.pushFrame(bindings);
	return resolveThroughBindings(expr, bindingFrameStack);
}

static Function *resolveThroughBindingsDeep(
	Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	std::unordered_map<std::string, Function *> &outBindings
);

static bool expressionReferencesAnyBindingName(
	Function *expr, const std::unordered_map<std::string, Function *> &bindingNames, std::unordered_set<Function *> &visited
) {
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

static bool
expressionReferencesAnyBindingName(Function *expr, const std::unordered_map<std::string, Function *> &bindingNames) {
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
	std::unordered_map<std::string, Function *> innerBindings;
	Function *bodyExpr = expandMacroPatternCall(expr, innerBindings);
	if (bodyExpr) {
		std::unordered_map<std::string, Function *> scopedMacroBindings;
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
	Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	std::unordered_map<std::string, Function *> &outBindings
) {
	BindingFrameStack bindingFrameStack;
	bindingFrameStack.pushFrame(bindings);
	BindingFrameStack outBindingFrameStack;
	std::unordered_set<Function *> visited;
	Function *resolvedExpression = resolveThroughBindingsDeepImpl(expr, bindingFrameStack, outBindingFrameStack, visited);
	outBindings = outBindingFrameStack.flattenBindings();
	return resolvedExpression;
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
static bool instantiateClassFromArgumentTypes(
	ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef, int baseClassInstIndex = -1
);

struct BindingContext {
	std::unordered_map<std::string, const Function *> bindingEntries;

	bool operator==(const BindingContext &other) const { return bindingEntries == other.bindingEntries; }
};

struct BindingContextHasher {
	size_t operator()(const BindingContext &bindingContext) const {
		size_t hashValue = bindingContext.bindingEntries.size() * 1099511628211ull;
		for (const auto &[bindingName, functionExpression] : bindingContext.bindingEntries) {
			size_t nameHash = std::hash<std::string>{}(bindingName);
			size_t functionHash = std::hash<const Function *>{}(functionExpression);
			size_t entryHash = nameHash;
			entryHash ^= functionHash + 0x9e3779b9 + (entryHash << 6) + (entryHash >> 2);
			hashValue ^= entryHash;
		}
		return hashValue;
	}
};

struct TypeResolutionKey {
	const Function *functionExpression = nullptr;
	BindingContext bindingContext;

	bool operator==(const TypeResolutionKey &other) const {
		return functionExpression == other.functionExpression && bindingContext == other.bindingContext;
	}
};

struct TypeResolutionKeyHasher {
	size_t operator()(const TypeResolutionKey &typeResolutionKey) const {
		size_t hashValue = std::hash<const Function *>{}(typeResolutionKey.functionExpression);
		size_t bindingContextHash = BindingContextHasher{}(typeResolutionKey.bindingContext);
		hashValue ^= bindingContextHash + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
		return hashValue;
	}
};

static thread_local ParseContext *activeTypeResolutionParseContext = nullptr;
static thread_local std::unordered_set<TypeResolutionKey, TypeResolutionKeyHasher> activeTypeResolutionKeys;

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
		if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
			Function *argExpr = sortedArgs[argIndex++];
			bindings[paramIt->second] = argExpr;
		}
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
