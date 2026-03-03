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

static DataType resolveTypeThroughBindings(Function *expr, const std::unordered_map<std::string, Function *> &bindings) {
	std::unordered_map<std::string, Function *> effectiveBindings;
	Function *resolved = resolveThroughBindingsDeep(expr, bindings, effectiveBindings);
	if (!resolved)
		return {};
	bool dependsOnBindings = resolved != expr || !effectiveBindings.empty();
	if (activeTypeResolutionFunctions.contains(resolved))
		return {};

	struct ActiveTypeResolutionGuard {
		const Function *expr;

		explicit ActiveTypeResolutionGuard(const Function *function) : expr(function) {
			activeTypeResolutionFunctions.insert(expr);
		}
		~ActiveTypeResolutionGuard() { activeTypeResolutionFunctions.erase(expr); }
	} activeGuard(resolved);
	if (!dependsOnBindings && resolved->type.isDeduced())
		return concretizeClassType(resolved->type);
	if (resolved->kind == Function::Kind::Literal) {
		if (std::holds_alternative<double>(resolved->literalValue)) {
			double value = std::get<double>(resolved->literalValue);
			std::string_view literalText = resolved->range.subString;
			bool explicitlyFloat = literalText.find('.') != std::string_view::npos ||
								   literalText.find('e') != std::string_view::npos ||
								   literalText.find('E') != std::string_view::npos;
			if (!explicitlyFloat && std::trunc(value) == value)
				return {DataType::Kind::Int, 4};
			return {DataType::Kind::Float, 8};
		}
		if (std::holds_alternative<std::string>(resolved->literalValue)) {
			DataType strType{DataType::Kind::Int, 1};
			strType.pointerDepth = 1;
			return strType;
		}
	}
	if (resolved->kind == Function::Kind::ArrayLiteral) {
		if (resolved->arguments.empty())
			return {};
		DataType elementType = resolveTypeThroughBindings(resolved->arguments[0], effectiveBindings);
		if (!elementType.isDeduced())
			return {};
		for (size_t i = 1; i < resolved->arguments.size(); i++) {
			DataType nextType = resolveTypeThroughBindings(resolved->arguments[i], effectiveBindings);
			DataType merged;
			if (!mergeArrayElementType(elementType, nextType, merged))
				return {};
			elementType = merged;
		}
		DataType arrayType{DataType::Kind::Array};
		arrayType.arraySize = static_cast<int>(resolved->arguments.size());
		arrayType.arrayElementType = std::make_shared<DataType>(elementType);
		return arrayType;
	}
	if (resolved->kind == Function::Kind::Variable && resolved->variable) {
		VariableReference *varRef = resolved->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		Section *sec = definition->range.line ? definition->range.line->section : nullptr;
		Variable *var = sec ? sec->findVariable(definition->name) : nullptr;
		if (!var && resolved->range.line)
			var =
				resolved->range.line->section ? resolved->range.line->section->findVariable(resolved->variable->name) : nullptr;
		if (var && var->type.isDeduced())
			return concretizeClassType(var->type);
	}
	if (resolved->kind == Function::Kind::PatternCall && resolved->patternMatch && resolved->patternMatch->matchedEndNode) {
		auto &defs = resolved->patternMatch->matchedEndNode->matchingDefinitions;
		if (!defs.empty()) {
			std::vector<DataType> argTypesForOverload;
			for (Function *arg : resolved->arguments)
				argTypesForOverload.push_back(resolveTypeThroughBindings(arg, effectiveBindings));
			PatternDefinition *def =
				selectOverload(defs, resolved->arguments, resolved->patternMatch->nodesPassed, argTypesForOverload);
			if (def && def->section && def->section->type == SectionType::Class && !def->section->isMacro &&
				activeTypeResolutionParseContext) {
				return instantiateBoundClassType(
					*activeTypeResolutionParseContext, static_cast<ClassSection *>(def->section)->classDefinition,
					effectiveBindings
				);
			}
		}
	}
	if (resolved->kind == Function::Kind::IntrinsicCall) {
		if (resolved->intrinsicName == "property" && resolved->arguments.size() >= 3) {
			DataType instType = concretizeClassType(resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings));
			Function *propExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindings);
			std::string fieldName = extractFieldName(propExpr);
			DataType builtInPropertyType = resolveBuiltInPropertyType(instType, fieldName);
			if (builtInPropertyType.isDeduced())
				return builtInPropertyType;
			if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
				for (size_t i = 0; i < instType.classDefinition->fields.size(); i++) {
					if (instType.classDefinition->fields[i].name == fieldName)
						return instType.classDefinition->instantiations[instType.classInstIndex].fieldTypes[i];
				}
			}
		} else if (resolved->intrinsicName == "dereference" && resolved->arguments.size() >= 2) {
			DataType ptrType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (ptrType.isDeduced() && ptrType.isPointer())
				return concretizeClassType(ptrType.dereferenced());
		} else if (resolved->intrinsicName == "address of" && resolved->arguments.size() >= 2) {
			DataType valueType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (valueType.isDeduced())
				return valueType.pointed();
		} else if (resolved->intrinsicName == "cast" && resolved->arguments.size() >= 3) {
			DataType typeArgType = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings);
			if (typeArgType.kind == DataType::Kind::Type)
				return concretizeClassType(typeArgType.toReferencedType());
		} else if (resolved->intrinsicName == "type") {
			std::string kindStr;
			Function *kindExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			if (auto *str = std::get_if<std::string>(&kindExpr->literalValue))
				kindStr = *str;
			if (!kindStr.empty()) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				if (kindStr == "int") {
					typeRef.referencedKind = DataType::Kind::Int;
					typeRef.numericSize = 4;
				} else if (kindStr == "float") {
					typeRef.referencedKind = DataType::Kind::Float;
					typeRef.numericSize = 8;
				} else if (kindStr == "bool") {
					typeRef.referencedKind = DataType::Kind::Bool;
				} else if (kindStr == "void") {
					typeRef.referencedKind = DataType::Kind::Void;
				} else if (kindStr == "string") {
					typeRef.referencedKind = DataType::Kind::Int;
					typeRef.numericSize = 1;
					typeRef.pointerDepth = 1;
				}
				if (resolved->arguments.size() >= 3) {
					Function *bitsExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindings);
					if (auto *bits = std::get_if<double>(&bitsExpr->literalValue))
						typeRef.numericSize = (int)*bits / 8;
				}
				return typeRef;
			}
		} else if (resolved->intrinsicName == "type of" && resolved->arguments.size() >= 2) {
			DataType valueType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (valueType.isDeduced()) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				typeRef.referencedKind = valueType.kind;
				typeRef.numericSize = valueType.numericSize;
				typeRef.pointerDepth = valueType.pointerDepth;
				typeRef.classDefinition = valueType.classDefinition;
				typeRef.classInstIndex = valueType.classInstIndex;
				typeRef.arraySize = valueType.arraySize;
				typeRef.arrayElementType =
					valueType.arrayElementType ? std::make_shared<DataType>(*valueType.arrayElementType) : nullptr;
				return typeRef;
			}
		} else if (resolved->intrinsicName == "build info" && resolved->arguments.size() >= 2) {
			Function *keyExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			if (auto *key = std::get_if<std::string>(&keyExpr->literalValue)) {
				if (*key == "word size" || *key == "optimization level") {
					return {DataType::Kind::Int, 4};
				}
				return {DataType::Kind::Int, 1, 1};
			}
		} else if (resolved->intrinsicName == "select" && resolved->arguments.size() >= 4 && activeTypeResolutionParseContext) {
			CompileTimeValue conditionValue =
				evaluateCompileTimeValue(resolved->arguments[1], *activeTypeResolutionParseContext, effectiveBindings);
			std::optional<bool> condition = compileTimeTruthiness(conditionValue);
			if (condition.has_value())
				return resolveTypeThroughBindings(resolved->arguments[*condition ? 2 : 3], effectiveBindings);
		} else if (resolved->intrinsicName == "array" && resolved->arguments.size() >= 2) {
			Function *sizeExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			if (auto *size = std::get_if<double>(&sizeExpr->literalValue)) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				typeRef.referencedKind = DataType::Kind::Array;
				typeRef.arraySize = static_cast<int>(*size);
				if (resolved->arguments.size() >= 3) {
					DataType elemTypeRef = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings);
					if (elemTypeRef.kind == DataType::Kind::Type)
						typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
				}
				return typeRef;
			}
		} else if (resolved->intrinsicName == "vector" && resolved->arguments.size() >= 2) {
			Function *sizeExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			auto *size = std::get_if<double>(&sizeExpr->literalValue);
			if (!size)
				return {};
			int vectorSize = static_cast<int>(*size);
			if (*size != static_cast<double>(vectorSize) || vectorSize < 1)
				return {};
			DataType typeRef;
			typeRef.kind = DataType::Kind::Type;
			typeRef.referencedKind = DataType::Kind::Vector;
			typeRef.arraySize = vectorSize;
			typeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() >= 3) {
				DataType elemTypeRef;
				if (resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings).kind == DataType::Kind::Type)
					elemTypeRef = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings).toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (resolved->intrinsicName == "matrix" && resolved->arguments.size() >= 3) {
			Function *rowsExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			Function *columnsExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindings);
			auto *rowsValue = std::get_if<double>(&rowsExpr->literalValue);
			auto *columnsValue = std::get_if<double>(&columnsExpr->literalValue);
			if (!rowsValue || !columnsValue)
				return {};
			int rows = static_cast<int>(*rowsValue);
			int columns = static_cast<int>(*columnsValue);
			if (*rowsValue != static_cast<double>(rows) || *columnsValue != static_cast<double>(columns))
				return {};
			if (rows < 1 || columns < 1)
				return {};
			DataType typeRef;
			typeRef.kind = DataType::Kind::Type;
			typeRef.referencedKind = DataType::Kind::Matrix;
			typeRef.matrixRowCount = rows;
			typeRef.arraySize = columns;
			typeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() >= 4) {
				DataType elemTypeRef;
				if (resolveTypeThroughBindings(resolved->arguments[3], effectiveBindings).kind == DataType::Kind::Type)
					elemTypeRef = resolveTypeThroughBindings(resolved->arguments[3], effectiveBindings).toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (resolved->intrinsicName == "add pointer depth" && resolved->arguments.size() >= 2) {
			DataType typeArgType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (typeArgType.kind == DataType::Kind::Type) {
				typeArgType.pointerDepth++;
				return typeArgType;
			}
		} else if (resolved->intrinsicName == "construct" && resolved->arguments.size() >= 2) {
			DataType typeRefType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (typeRefType.kind == DataType::Kind::Type && typeRefType.referencedKind == DataType::Kind::Array) {
				DataType arrayType = typeRefType.toReferencedType();
				if (arrayType.arraySize == static_cast<int>(resolved->arguments.size()) - 2) {
					DataType elementType =
						arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
					bool allDeduced = true;
					for (size_t i = 2; i < resolved->arguments.size(); i++) {
						DataType argType = resolveTypeThroughBindings(resolved->arguments[i], effectiveBindings);
						if (!argType.isDeduced())
							allDeduced = false;
						if (!arrayType.arrayElementType) {
							if (!elementType.isDeduced())
								elementType = argType;
							else if (elementType != argType)
								allDeduced = false;
						}
					}
					if (allDeduced && elementType.isDeduced()) {
						arrayType.arrayElementType = std::make_shared<DataType>(elementType);
						return arrayType;
					}
				}
			} else if (typeRefType.kind == DataType::Kind::Type && typeRefType.classDefinition) {
				std::vector<DataType> argumentTypes;
				argumentTypes.reserve(resolved->arguments.size() - 2);
				bool allArgumentsDeduced = true;
				for (size_t i = 2; i < resolved->arguments.size(); i++) {
					DataType argumentType = resolveTypeThroughBindings(resolved->arguments[i], effectiveBindings);
					if (!argumentType.isDeduced()) {
						allArgumentsDeduced = false;
						break;
					}
					argumentTypes.push_back(argumentType);
				}

				DataType instantiatedTypeRef;
				if (allArgumentsDeduced &&
					instantiateClassFromArgumentTypes(typeRefType.classDefinition, argumentTypes, instantiatedTypeRef)) {
					return instantiatedTypeRef.toReferencedType();
				}

				DataType targetType = concretizeClassType(typeRefType.toReferencedType());
				if (resolved->arguments.size() == targetType.classDefinition->fields.size() + 2 &&
					targetType.classInstIndex >= 0) {
					const auto &fieldTypes = targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
					bool allCompatible = argumentTypes.size() == fieldTypes.size();
					for (size_t i = 0; allCompatible && i < fieldTypes.size(); i++) {
						if (argumentTypes[i] != fieldTypes[i])
							allCompatible = false;
					}
					if (allCompatible)
						return targetType;
				}
			} else if (typeRefType.kind == DataType::Kind::Type && resolved->arguments.size() == 3) {
				DataType targetType = typeRefType.toReferencedType();
				DataType valueType = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings);
				if (valueType.isDeduced())
					return targetType;
			}
		}
	}
	std::unordered_map<std::string, Function *> innerBindings;
	Function *bodyExpr = expandMacroPatternCall(resolved, innerBindings);
	if (bodyExpr) {
		std::unordered_map<std::string, Function *> mergedBindings = bindings;
		for (auto &[name, argExpr] : innerBindings) {
			std::unordered_map<std::string, Function *> ignoredBindings;
			mergedBindings[name] = resolveThroughBindingsDeep(argExpr, bindings, ignoredBindings);
		}
		return resolveTypeThroughBindings(bodyExpr, mergedBindings);
	}
	return concretizeClassType(resolved->type);
}

static std::string typeToUserName(const DataType &type, ParseContext &parseContext) {
	auto it = parseContext.typeAliasNames.find(type);
	if (it != parseContext.typeAliasNames.end())
		return it->second;
	return type.toString();
}

static bool resolveCompileTimeTypeReference(
	ParseContext &parseContext, Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	DataType &outTypeRef
) {
	std::unordered_map<std::string, Function *> effectiveBindings;
	Function *resolved = resolveThroughBindingsDeep(expr, bindings, effectiveBindings);
	if (!resolved)
		return false;
	bool dependsOnBindings = resolved != expr || !effectiveBindings.empty();

	if (!dependsOnBindings && resolved->type.kind == DataType::Kind::Type) {
		outTypeRef = resolved->type;
		return true;
	}

	if (resolved->kind == Function::Kind::Variable && resolved->variable) {
		DataType shorthandType = DataType::fromString(resolved->variable->name);
		if (shorthandType.isDeduced()) {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = shorthandType.kind;
			outTypeRef.numericSize = shorthandType.numericSize;
			outTypeRef.pointerDepth = shorthandType.pointerDepth;
			return true;
		}
		return false;
	}

	if (resolved->kind == Function::Kind::IntrinsicCall) {
		if (resolved->intrinsicName == "type") {
			DataType resolvedType = resolveTypeThroughBindings(resolved, effectiveBindings);
			if (resolvedType.kind == DataType::Kind::Type) {
				outTypeRef = resolvedType;
				return true;
			}
		}
		if (resolved->intrinsicName == "add pointer depth" && resolved->arguments.size() >= 2) {
			DataType innerTypeRef;
			if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[1], effectiveBindings, innerTypeRef) ||
				innerTypeRef.kind != DataType::Kind::Type)
				return false;
			innerTypeRef.pointerDepth++;
			outTypeRef = innerTypeRef;
			return true;
		}
		if (resolved->intrinsicName == "array" && resolved->arguments.size() >= 2) {
			int arraySize = 0;
			if (!evaluateCompileTimeInteger(parseContext, resolved->arguments[1], effectiveBindings, arraySize))
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Array;
			outTypeRef.arraySize = arraySize;
			if (resolved->arguments.size() >= 3) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[2], effectiveBindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (resolved->intrinsicName == "vector" && resolved->arguments.size() >= 2) {
			int vectorSize = 0;
			if (!evaluateCompileTimeInteger(parseContext, resolved->arguments[1], effectiveBindings, vectorSize) ||
				vectorSize < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Vector;
			outTypeRef.arraySize = vectorSize;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() >= 3) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[2], effectiveBindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (resolved->intrinsicName == "matrix" && resolved->arguments.size() >= 3) {
			int rows = 0;
			int columns = 0;
			if (!evaluateCompileTimeInteger(parseContext, resolved->arguments[1], effectiveBindings, rows) ||
				!evaluateCompileTimeInteger(parseContext, resolved->arguments[2], effectiveBindings, columns) || rows < 1 ||
				columns < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Matrix;
			outTypeRef.matrixRowCount = rows;
			outTypeRef.arraySize = columns;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() >= 4) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[3], effectiveBindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
	}

	if (resolved->kind == Function::Kind::PatternCall) {
		auto &defs = resolved->patternMatch->matchedEndNode->matchingDefinitions;
		if (defs.empty())
			return false;
		std::vector<DataType> argTypesForOverload;
		for (Function *arg : resolved->arguments)
			argTypesForOverload.push_back(resolveTypeThroughBindings(arg, effectiveBindings));
		PatternDefinition *def =
			selectOverload(defs, resolved->arguments, resolved->patternMatch->nodesPassed, argTypesForOverload);
		if (!def || !def->section)
			return false;

		std::unordered_map<std::string, Function *> callBindings = effectiveBindings;
		appendPatternCallBindings(resolved, def, callBindings);
		if (!def->section->isMacro && def->section->type == SectionType::Class) {
			outTypeRef = instantiateBoundClassType(
				parseContext, static_cast<ClassSection *>(def->section)->classDefinition, callBindings
			);
			return outTypeRef.kind == DataType::Kind::Type;
		}

		std::unordered_map<std::string, Function *> innerBindings;
		Function *bodyExpr = expandMacroPatternCall(resolved, innerBindings);
		if (!bodyExpr)
			return false;
		for (const auto &[name, argExpr] : innerBindings)
			callBindings[name] = argExpr;
		return resolveCompileTimeTypeReference(parseContext, bodyExpr, callBindings, outTypeRef);
	}
	return false;
}

static DataType instantiateBoundClassType(
	ParseContext &parseContext, ClassDefinition *classDef, const std::unordered_map<std::string, Function *> &bindings
) {
	if (!classDef)
		return {};

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(classDef->fields.size());
	for (FieldDefinition &field : classDef->fields) {
		DataType fieldType = field.declaredType;
		if (fieldType.kind == DataType::Kind::Any)
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		if (fieldType.kind == DataType::Kind::Unresolved && fieldType.typeFunction) {
			DataType resolvedTypeRef;
			if (!resolveCompileTimeTypeReference(parseContext, fieldType.typeFunction, bindings, resolvedTypeRef) ||
				resolvedTypeRef.kind != DataType::Kind::Type)
				return {};
			fieldType = concretizeClassType(resolvedTypeRef.toReferencedType());
		} else if (fieldType.kind == DataType::Kind::Class && fieldType.classInstIndex < 0) {
			fieldType = concretizeClassType(fieldType);
		}

		if (!fieldType.isDeduced())
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	return {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
}

static bool
instantiateClassFromArgumentTypes(ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef) {
	if (!classDef || classDef->fields.size() != argumentTypes.size())
		return false;

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(argumentTypes.size());
	for (size_t i = 0; i < argumentTypes.size(); i++) {
		DataType fieldType = classDef->fields[i].declaredType;
		if (fieldType.kind == DataType::Kind::Any) {
			if (!argumentTypes[i].isDeduced())
				return false;
			fieldType = argumentTypes[i];
		} else if (fieldType.kind == DataType::Kind::Class && fieldType.classInstIndex < 0) {
			fieldType = concretizeClassType(fieldType);
		}

		if (!fieldType.isDeduced())
			return false;
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	outTypeRef = {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
	return true;
}

static bool isWholeNumberLiteral(Function *expr) {
	if (!expr || expr->kind != Function::Kind::Literal || !std::holds_alternative<double>(expr->literalValue))
		return false;
	std::string_view literalText = expr->range.subString;
	if (literalText.find('.') != std::string_view::npos || literalText.find('e') != std::string_view::npos ||
		literalText.find('E') != std::string_view::npos)
		return false;
	double value = std::get<double>(expr->literalValue);
	return std::trunc(value) == value;
}

static std::string makeFloatLiteralReplacement(Function *expr) {
	if (!isWholeNumberLiteral(expr))
		return {};
	return (std::string)expr->range.subString + ".0";
}

static std::string formatTypeList(const std::vector<DataType> &types, ParseContext &parseContext) {
	std::string out;
	for (size_t i = 0; i < types.size(); i++) {
		if (i > 0)
			out += ", ";
		out += typeToUserName(types[i], parseContext);
	}
	return out;
}

static DataType concretizeClassType(DataType type) {
	if (type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex < 0 &&
		!type.classDefinition->instantiations.empty()) {
		type.classInstIndex = 0;
	}
	return type;
}

static DataType resolveBuiltInPropertyType(const DataType &ownerType, const std::string &fieldName) {
	if (fieldName == "data" && ownerType.isBytePointer())
		return ownerType;
	return {};
}

static bool isLogicalOperandType(const DataType &type) { return type.kind == DataType::Kind::Bool || type.isNumeric(); }

static std::string extractFieldName(Function *expr) {
	if (!expr)
		return {};
	if (auto *str = std::get_if<std::string>(&expr->literalValue))
		return *str;
	if (expr->kind == Function::Kind::Variable && expr->variable)
		return expr->variable->name;
	return {};
}

static std::string diagnosticFunctionText(Function *expr) {
	if (!expr)
		return {};

	std::string text = (std::string)expr->range.subString;
	if (!expr->range.line)
		return text;

	std::string_view lineText = expr->range.line->patternText;
	if (lineText.empty() || lineText == expr->range.subString || !lineText.ends_with(expr->range.subString))
		return text;

	size_t missingPrefixLength = lineText.size() - expr->range.subString.size();
	if (missingPrefixLength == 0 || !std::isspace(static_cast<unsigned char>(lineText[missingPrefixLength - 1])))
		return text;

	return (std::string)lineText;
}

static std::string buildTypeFailureDiagnostic(Function *expr, const std::string &detail) {
	std::string message =
		"Function '" + diagnosticFunctionText(expr) + "' parses successfully without types, but not with types";
	if (!detail.empty())
		message += ": " + detail;
	return message;
}

// Must stay in sync with codegen's ensureType conversion support.
static bool isSupportedCastConversion(const DataType &fromType, const DataType &toType) {
	if (fromType == toType)
		return true;
	if (fromType.isPointer() && toType.isPointer())
		return true;
	if (fromType.isPointer() && toType.kind == DataType::Kind::Int)
		return true;
	if (fromType.kind == DataType::Kind::Int && toType.isPointer())
		return true;
	if (fromType.isNumeric() && toType.isNumeric())
		return true;
	if (fromType.kind == DataType::Kind::Bool && toType.isNumeric())
		return true;
	return false;
}

static bool mergeArrayElementType(const DataType &current, const DataType &next, DataType &merged) {
	if (!current.isDeduced() || !next.isDeduced())
		return false;
	if (current == next) {
		merged = current;
		return true;
	}
	if (current.isNumeric() && next.isNumeric())
		return DataType::promoteArithmetic(current, next, merged);
	return false;
}

// Wraps ParseContext with type validity tracking and trial mode for operand reordering.
// During reordering trials, diagnostics are suppressed and failures only affect the current trial.
struct InferenceContext {
	struct TrialJournal {
		struct VariableUndo {
			Variable *variable;
			DataType type;
			Range typeOriginRange;
			std::string typeOriginFloatLiteralReplacement;
		};

		struct SectionInstantiationUndo {
			Section *section;
			std::vector<DataType> argTypes;
			bool existed;
			Instantiation value;
		};

		std::vector<VariableUndo> variableTypeUndo;
		std::unordered_set<Variable *> seenVariables;
		std::vector<std::pair<ClassDefinition *, size_t>> classInstantiationSizes;
		std::unordered_set<ClassDefinition *> seenClassDefinitions;
		std::vector<Section *> touchedSections;
		std::unordered_set<Section *> seenSections;
		std::vector<SectionInstantiationUndo> sectionInstantiationUndo;
		std::unordered_set<std::string> seenSectionInstantiations;

		void recordVariableWrite(Variable *var) {
			if (!var || seenVariables.contains(var))
				return;
			seenVariables.insert(var);
			variableTypeUndo.push_back({var, var->type, var->typeOriginRange, var->typeOriginFloatLiteralReplacement});
		}

		void recordClassInstantiationAppend(ClassDefinition *classDef) {
			if (!classDef || seenClassDefinitions.contains(classDef))
				return;
			seenClassDefinitions.insert(classDef);
			classInstantiationSizes.push_back({classDef, classDef->instantiations.size()});
		}

		void recordTouchedSection(Section *section) {
			if (!section || seenSections.contains(section))
				return;
			seenSections.insert(section);
			touchedSections.push_back(section);
		}

		void recordSectionInstantiationWrite(Section *section, const std::vector<DataType> &argTypes) {
			if (!section)
				return;
			std::string key = std::to_string(reinterpret_cast<uintptr_t>(section)) + "|";
			for (const DataType &type : argTypes)
				key += type.toString() + ";";
			if (seenSectionInstantiations.contains(key))
				return;
			seenSectionInstantiations.insert(key);
			auto it = section->instantiations.find(argTypes);
			if (it == section->instantiations.end())
				sectionInstantiationUndo.push_back({section, argTypes, false, {}});
			else
				sectionInstantiationUndo.push_back({section, argTypes, true, it->second});
		}
	};

	ParseContext &parseContext;
	Instantiation *currentInstantiation{};
	bool typesValid = true;
	bool trial = false;
	bool suppressDiagnostics = false;
	std::string typeFailureDetail;
	TrialJournal *trialJournal{};

	InferenceContext(ParseContext &pc) : parseContext(pc) {}
	InferenceContext(ParseContext &pc, bool trial) : parseContext(pc), trial(trial) {}

	void addDiagnostic(Diagnostic diagnostic) {
		if (!trial && !suppressDiagnostics)
			parseContext.diagnostics.push_back(std::move(diagnostic));
	}

	void setTypeFailure(std::string detail) {
		typesValid = false;
		if (typeFailureDetail.empty())
			typeFailureDetail = std::move(detail);
	}
};

struct ScopedDiagnosticSuppression {
	InferenceContext &context;
	bool previous;

	explicit ScopedDiagnosticSuppression(InferenceContext &context) : context(context), previous(context.suppressDiagnostics) {
		context.suppressDiagnostics = true;
	}

	~ScopedDiagnosticSuppression() { context.suppressDiagnostics = previous; }
};

static void resetFunctionTypes(Function *expr);
static void resetSectionFunctionTypes(Section *section);
static void recomputeRanges(Function *expr);
static bool startsWithArgument(Function *function);
static bool endsWithArgument(Function *function);

static void rollbackTrialJournal(InferenceContext::TrialJournal &journal) {
	for (Section *section : journal.touchedSections)
		resetSectionFunctionTypes(section);
	for (auto it = journal.variableTypeUndo.rbegin(); it != journal.variableTypeUndo.rend(); ++it) {
		it->variable->type = it->type;
		it->variable->typeOriginRange = it->typeOriginRange;
		it->variable->typeOriginFloatLiteralReplacement = it->typeOriginFloatLiteralReplacement;
	}
	for (auto it = journal.sectionInstantiationUndo.rbegin(); it != journal.sectionInstantiationUndo.rend(); ++it) {
		if (it->existed)
			it->section->instantiations[it->argTypes] = it->value;
		else
			it->section->instantiations.erase(it->argTypes);
	}
	for (auto it = journal.classInstantiationSizes.rbegin(); it != journal.classInstantiationSizes.rend(); ++it)
		it->first->instantiations.resize(it->second);
}

static Function *cloneFunctionTree(Function *expr) {
	if (!expr)
		return nullptr;
	Function *clone = new Function(*expr);
	clone->arguments.clear();
	for (Function *arg : expr->arguments)
		clone->arguments.push_back(cloneFunctionTree(arg));
	return clone;
}

static bool inferFunction(
	Function *&expr, InferenceContext &context, bool alreadyOrdered,
	const std::unordered_map<std::string, Function *> &macroBindings
);
static bool
inferSection(Section *section, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings);
static DataType inferFunctionTypeWithoutSideEffects(
	Function *&expr, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings
);
static DataType
ensureFunctionType(Function *&expr, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings);

static void resetSectionFunctionTypes(Section *section) {
	if (!section)
		return;
	for (CodeLine *line : section->codeLines) {
		if (line->function)
			resetFunctionTypes(line->function);
	}
}

static void resetSectionLocalVariableTypes(Section *section) {
	if (!section)
		return;
	for (auto &[name, variable] : section->variables) {
		if (!variable || variable->isGlobal)
			continue;
		variable->type = {};
		variable->typeOriginRange = {};
		variable->typeOriginFloatLiteralReplacement.clear();
	}
}

static DataType
derivePatternCallType(Function *expr, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings) {
	if (!expr || expr->kind != Function::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return {};

	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return {};

	std::vector<DataType> argTypesForOverload;
	for (Function *&arg : expr->arguments)
		argTypesForOverload.push_back(inferFunctionTypeWithoutSideEffects(arg, context, bindings));

	PatternDefinition *def = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload);
	if (!def || !def->section)
		return {};

	Section *matchedSection = def->section;
	std::unordered_map<std::string, Function *> callBindings;
	size_t argIndex = 0;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		auto paramIt = node->parameterNames.find(def);
		if (paramIt != node->parameterNames.end() && argIndex < expr->arguments.size())
			callBindings[paramIt->second] = expr->arguments[argIndex++];
	}

	if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
		auto *classSec = static_cast<ClassSection *>(matchedSection);
		return instantiateBoundClassType(context.parseContext, classSec->classDefinition, callBindings);
	}

	if (matchedSection->isMacro) {
		if (!matchedSection->inferring) {
			matchedSection->inferring = true;
			ScopedDiagnosticSuppression suppressDiagnostics(context);
			inferSection(matchedSection, context, callBindings);
			matchedSection->inferring = false;
		}
		for (Section *child : matchedSection->children) {
			for (CodeLine *line : child->codeLines) {
				if (!line->function)
					continue;
				DataType resolvedType = resolveTypeThroughBindings(line->function, callBindings);
				if (resolvedType.isDeduced())
					return resolvedType;
			}
		}
		return {};
	}

	std::vector<DataType> argTypes;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		auto paramIt = node->parameterNames.find(def);
		if (paramIt == node->parameterNames.end())
			continue;
		Function *&argExpr = callBindings[paramIt->second];
		DataType argType = inferFunctionTypeWithoutSideEffects(argExpr, context, callBindings);
		if (!argType.isDeduced())
			return {};
		argTypes.push_back(argType);
	}

	if (!ensureSectionInstantiationInferred(
			context.parseContext, matchedSection, callBindings, argTypes, context.currentInstantiation
		))
		return {};

	auto instIt = matchedSection->instantiations.find(argTypes);
	if (instIt != matchedSection->instantiations.end() && instIt->second.returnType.isDeduced())
		return instIt->second.returnType;

	return {};
}

// Probe an function's type without committing inference side effects or surfacing
// nested diagnostics. This is used by overload resolution and logical/operator
// checks where we only need the resulting type.
static DataType inferFunctionTypeWithoutSideEffects(
	Function *&expr, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings
) {
	static thread_local std::unordered_set<const Function *> activeTypeProbes;
	Function *targetExpr = expr;
	std::unordered_map<std::string, Function *> targetBindings = bindings;
	std::unordered_map<std::string, Function *> effectiveBindings;
	Function *resolvedExpr = resolveThroughBindingsDeep(expr, bindings, effectiveBindings);
	if (resolvedExpr) {
		targetExpr = resolvedExpr;
		targetBindings = std::move(effectiveBindings);
	}

	if (targetExpr && targetExpr->kind == Function::Kind::PatternCall && expandsToSelectIntrinsic(targetExpr) &&
		targetExpr->arguments.size() >= 3) {
		CompileTimeValue conditionValue = evaluateCompileTimeValue(
			targetExpr->arguments[1], context.parseContext, targetBindings, context.currentInstantiation
		);
		std::optional<bool> condition = compileTimeTruthiness(conditionValue);
		if (condition.has_value()) {
			Function *activeBranch = targetExpr->arguments[*condition ? 0 : 2];
			DataType activeType = inferFunctionTypeWithoutSideEffects(activeBranch, context, targetBindings);
			if (activeType.isDeduced()) {
				targetExpr->type = activeType;
				return activeType;
			}
		}
	}

	DataType type = resolveTypeThroughBindings(targetExpr, targetBindings);
	if (type.isDeduced())
		return type;
	if (!targetExpr)
		return {};
	if (activeTypeProbes.contains(targetExpr))
		return type;

	struct ActiveTypeProbeGuard {
		std::unordered_set<const Function *> &active;
		const Function *expr;

		ActiveTypeProbeGuard(std::unordered_set<const Function *> &active, const Function *expr) : active(active), expr(expr) {
			active.insert(expr);
		}

		~ActiveTypeProbeGuard() { active.erase(expr); }
	} activeProbe(activeTypeProbes, targetExpr);

	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.trialJournal = &journal;

	(void)inferFunction(targetExpr, trialContext, false, targetBindings);
	type = resolveTypeThroughBindings(targetExpr, targetBindings);
	if (!type.isDeduced())
		type = derivePatternCallType(targetExpr, trialContext, targetBindings);
	if (!type.isDeduced() && context.typeFailureDetail.empty() && !trialContext.typeFailureDetail.empty())
		context.typeFailureDetail = trialContext.typeFailureDetail;

	rollbackTrialJournal(journal);
	if (type.isDeduced())
		expr->type = type;
	return type;
}

static DataType
ensureFunctionType(Function *&expr, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings) {
	return inferFunctionTypeWithoutSideEffects(expr, context, bindings);
}

static void commitVariableTypeFromValue(Variable *var, Function *valueExpr, const DataType &valueType) {
	if (!var)
		return;
	var->type = concretizeClassType(valueType);
	var->typeOriginRange = valueExpr ? valueExpr->range : Range();
	var->typeOriginFloatLiteralReplacement = makeFloatLiteralReplacement(valueExpr);
}

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Function *valueExpr, const DataType &valueType, ParseContext &parseContext) {
	Range diagnosticRange = valueExpr ? valueExpr->range : (var && var->definition ? var->definition->range : Range());
	Diagnostic diagnostic(
		Diagnostic::Level::Error,
		"Variable '" + var->name + "' cannot change type from " + typeToUserName(var->type, parseContext) + " to " +
			typeToUserName(valueType, parseContext),
		diagnosticRange
	);
	if (var->typeOriginRange.line) {
		diagnostic.relatedInfo.push_back(
			{"Variable '" + var->name + "' first became " + typeToUserName(var->type, parseContext) + " here",
			 var->typeOriginRange}
		);
	}
	if (!var->typeOriginFloatLiteralReplacement.empty() && valueType.kind == DataType::Kind::Float &&
		var->type.kind == DataType::Kind::Int && var->typeOriginRange.line) {
		diagnostic.quickFixes.push_back(
			{"Change '" + (std::string)var->typeOriginRange.subString + "' to '" + var->typeOriginFloatLiteralReplacement + "'",
			 var->typeOriginRange, var->typeOriginFloatLiteralReplacement}
		);
	}
	return diagnostic;
}

static Variable *findVariableInSectionTree(Section *section, const std::string &name) {
	if (!section)
		return nullptr;
	if (Variable *var = section->findVariable(name))
		return var;
	for (Section *child : section->children) {
		if (Variable *var = findVariableInSectionTree(child, name))
			return var;
	}
	return nullptr;
}

static int getRefinedClassInstantiationIndex(
	InferenceContext &context, ClassDefinition *classDef, int instIndex, size_t fieldIndex, const DataType &fieldType
) {
	if (!classDef || instIndex < 0 || instIndex >= (int)classDef->instantiations.size())
		return -1;
	const auto &baseFieldTypes = classDef->instantiations[instIndex].fieldTypes;
	if (fieldIndex >= baseFieldTypes.size())
		return -1;
	const DataType &declaredFieldType = classDef->fields[fieldIndex].declaredType;
	if (declaredFieldType.isDeduced() && declaredFieldType != fieldType)
		return -1;
	std::vector<DataType> refinedFieldTypes = baseFieldTypes;
	refinedFieldTypes[fieldIndex] = fieldType;
	bool instantiationExists = false;
	for (const auto &inst : classDef->instantiations) {
		if (inst.fieldTypes == refinedFieldTypes) {
			instantiationExists = true;
			break;
		}
	}
	if (!instantiationExists && context.trial && context.trialJournal)
		context.trialJournal->recordClassInstantiationAppend(classDef);
	int existingIndex = classDef->getOrCreateInstantiation(refinedFieldTypes);
	return existingIndex;
}

// Infer types for a section's code lines with operand reordering. Returns false on failure.
static bool
inferSection(Section *section, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings = {});

// Infer the type of an function bottom-up.
// Sets context.typesValid = false if types are invalid for this grouping.
static void inferOrderedFunction(
	Function *expr, InferenceContext &context, const std::unordered_map<std::string, Function *> &macroBindings = {}
) {
	context.typesValid = true;
	if (expr->kind == Function::Kind::IntrinsicCall && expr->intrinsicName == "select") {
		markCompileTimeParameterRequirements(expr->arguments[1], macroBindings, context.currentInstantiation);
		Function *chosenBranch =
			selectCompileTimeBranch(expr, context.parseContext, macroBindings, context.currentInstantiation);
		if (!chosenBranch) {
			context.setTypeFailure("select condition must be compile-time known");
			return;
		}
		size_t chosenIndex = chosenBranch == expr->arguments[2] ? 2 : 3;
		Function *activeBranch = chosenBranch;
		if (!inferFunction(activeBranch, context, false, macroBindings))
			return;
		expr->arguments[chosenIndex] = activeBranch;
		expr->type = resolveTypeThroughBindings(activeBranch, macroBindings);
		return;
	}
	bool deferArgumentInference = expr->kind == Function::Kind::PatternCall && expandsToSelectIntrinsic(expr);
	std::unordered_set<size_t> skippedArgumentIndices;
	if (expr->kind == Function::Kind::PatternCall && !deferArgumentInference)
		skippedArgumentIndices = compileTimeOnlyArgumentIndices(expr);
	// Recurse into arguments first (bottom-up)
	if (!deferArgumentInference) {
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			if (skippedArgumentIndices.contains(i))
				continue;
			Function *arg = expr->arguments[i];
			inferOrderedFunction(arg, context, macroBindings);
			if (!context.typesValid)
				return;
		}
	}

	switch (expr->kind) {
	case Function::Kind::Literal: {
		if (std::holds_alternative<double>(expr->literalValue)) {
			double value = std::get<double>(expr->literalValue);
			std::string_view literalText = expr->range.subString;
			bool explicitlyFloat = literalText.find('.') != std::string_view::npos ||
								   literalText.find('e') != std::string_view::npos ||
								   literalText.find('E') != std::string_view::npos;
			if (!explicitlyFloat && std::trunc(value) == value) {
				expr->type = {DataType::Kind::Int, 4};
			} else {
				expr->type = {DataType::Kind::Float, 8};
			}
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {DataType::Kind::Int, 1};
			expr->type.pointerDepth = 1;
		}
		break;
	}

	case Function::Kind::ArrayLiteral: {
		if (expr->arguments.empty())
			break;
		DataType elementType = resolveTypeThroughBindings(expr->arguments[0], macroBindings);
		if (!elementType.isDeduced())
			break;
		for (size_t i = 1; i < expr->arguments.size(); i++) {
			DataType nextType = resolveTypeThroughBindings(expr->arguments[i], macroBindings);
			DataType merged;
			if (!mergeArrayElementType(elementType, nextType, merged)) {
				context.typesValid = false;
				context.setTypeFailure("Array literal elements must have matching or compatible numeric types");
				return;
			}
			elementType = merged;
		}
		expr->type = DataType(DataType::Kind::Array);
		expr->type.arraySize = static_cast<int>(expr->arguments.size());
		expr->type.arrayElementType = std::make_shared<DataType>(elementType);
		break;
	}

	case Function::Kind::Variable: {
		if (expr->variable) {
			std::string varName = expr->variable->name;
			// Check macro bindings first
			auto macroIt = macroBindings.find(varName);
			if (macroIt != macroBindings.end()) {
				DataType boundType = resolveTypeThroughBindings(macroIt->second, macroBindings);
				if (boundType.isDeduced()) {
					expr->type = boundType;
				}
				break;
			}
			// Look up variable in scope
			Section *sec = expr->range.line ? expr->range.line->section : nullptr;
			Variable *var = sec ? sec->findVariable(varName) : nullptr;
			if (var && var->type.isDeduced()) {
				expr->type = var->type;
			}
		}
		break;
	}

	case Function::Kind::IntrinsicCall: {
		const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
		if (info) {
			switch (info->returnKind) {
			case IntrinsicReturnKind::SameAsArgs:
				if (info->argCount == 2) {
					expr->type = ensureFunctionType(expr->arguments[1], context, macroBindings);
				} else {
					DataType leftType = ensureFunctionType(expr->arguments[1], context, macroBindings);
					DataType rightType = ensureFunctionType(expr->arguments[2], context, macroBindings);
					DataType result;
					if (!DataType::promoteArithmetic(leftType, rightType, result)) {
						context.setTypeFailure(
							"Incompatible operand types '" + typeToUserName(leftType, context.parseContext) + "' and '" +
							typeToUserName(rightType, context.parseContext) + "'"
						);
						break;
					}
					expr->type = result;
				}
				break;
			case IntrinsicReturnKind::Bool: {
				if (expr->intrinsicName == "and" || expr->intrinsicName == "or") {
					DataType leftType = ensureFunctionType(expr->arguments[1], context, macroBindings);
					DataType rightType = ensureFunctionType(expr->arguments[2], context, macroBindings);
					if (!isLogicalOperandType(leftType) || !isLogicalOperandType(rightType)) {
						context.setTypeFailure(
							"Logical operator '" + expr->intrinsicName + "' requires boolean or numeric operands, got '" +
							typeToUserName(leftType, context.parseContext) + "' and '" +
							typeToUserName(rightType, context.parseContext) + "'"
						);
						break;
					}
				} else if (expr->intrinsicName == "not") {
					DataType valueType = ensureFunctionType(expr->arguments[1], context, macroBindings);
					if (!isLogicalOperandType(valueType)) {
						context.setTypeFailure(
							"Logical operator 'not' requires a boolean or numeric operand, got '" +
							typeToUserName(valueType, context.parseContext) + "'"
						);
						break;
					}
				} else {
					DataType leftType = ensureFunctionType(expr->arguments[1], context, macroBindings);
					DataType rightType = ensureFunctionType(expr->arguments[2], context, macroBindings);
					DataType promoted;
					bool pointerEquality = (expr->intrinsicName == "equal" || expr->intrinsicName == "not equal") &&
										   leftType.isPointer() && rightType.isPointer() && leftType == rightType;
					if (!pointerEquality && !DataType::promoteArithmetic(leftType, rightType, promoted)) {
						context.setTypeFailure(
							"Incompatible operand types '" + typeToUserName(leftType, context.parseContext) + "' and '" +
							typeToUserName(rightType, context.parseContext) + "'"
						);
						break;
					}
				}
				expr->type = {DataType::Kind::Bool};
				break;
			}
			case IntrinsicReturnKind::Void:
				// "store" has side effects on variable types beyond just being Void
				if (expr->intrinsicName == "store") {
					std::unordered_map<std::string, Function *> destBindings;
					Function *destExpr = resolveThroughBindingsDeep(expr->arguments[1], macroBindings, destBindings);
					std::unordered_map<std::string, Function *> valueBindings;
					Function *valueExpr = resolveThroughBindingsDeep(expr->arguments[2], macroBindings, valueBindings);
					DataType valType = ensureFunctionType(valueExpr, context, valueBindings);
					if (destExpr->kind == Function::Kind::Variable && destExpr->variable && valType.isDeduced()) {
						Section *sec = destExpr->range.line ? destExpr->range.line->section : nullptr;
						Variable *var = sec ? sec->findVariable(destExpr->variable->name) : nullptr;
						if (var) {
							if (!var->type.isDeduced() || var->type == valType) {
								if (context.trial && context.trialJournal)
									context.trialJournal->recordVariableWrite(var);
								commitVariableTypeFromValue(var, valueExpr, valType);
							} else if (context.trial) {
								context.setTypeFailure(
									"Variable '" + var->name + "' cannot change type from " +
									typeToUserName(var->type, context.parseContext) + " to " +
									typeToUserName(valType, context.parseContext)
								);
								break;
							} else {
								context.setTypeFailure(
									"Variable '" + var->name + "' cannot change type from " +
									typeToUserName(var->type, context.parseContext) + " to " +
									typeToUserName(valType, context.parseContext)
								);
								context.addDiagnostic(
									buildVariableTypeChangeDiagnostic(var, valueExpr, valType, context.parseContext)
								);
								break;
							}
						}
					} else if (destExpr->kind == Function::Kind::IntrinsicCall && destExpr->intrinsicName == "property" &&
							   valType.isDeduced()) {
						std::unordered_map<std::string, Function *> resolvedBindings = macroBindings;
						for (const auto &[name, boundExpr] : destBindings)
							resolvedBindings[name] = boundExpr;
						std::unordered_map<std::string, Function *> ignoredBindings;
						Function *ownerExpr =
							resolveThroughBindingsDeep(destExpr->arguments[1], resolvedBindings, ignoredBindings);
						DataType instType = ownerExpr ? concretizeClassType(ownerExpr->type) : DataType{};
						if (instType.kind == DataType::Kind::Class && instType.classDefinition &&
							instType.classInstIndex >= 0) {
							Function *propExpr = resolveThroughBindings(destExpr->arguments[2], resolvedBindings);
							std::string fieldName;
							if (auto *str = std::get_if<std::string>(&propExpr->literalValue))
								fieldName = *str;
							if (!fieldName.empty()) {
								ClassDefinition *classDef = instType.classDefinition;
								for (size_t i = 0; i < classDef->fields.size(); i++) {
									if (classDef->fields[i].name == fieldName) {
										int refinedInstIndex = getRefinedClassInstantiationIndex(
											context, classDef, instType.classInstIndex, i, valType
										);
										if (refinedInstIndex < 0)
											break;
										if (ownerExpr && ownerExpr->kind == Function::Kind::Variable && ownerExpr->variable) {
											Section *ownerSection =
												ownerExpr->range.line ? ownerExpr->range.line->section : nullptr;
											Variable *ownerVar =
												ownerSection ? ownerSection->findVariable(ownerExpr->variable->name) : nullptr;
											if (ownerVar && ownerVar->type.kind == DataType::Kind::Class &&
												ownerVar->type.classDefinition == classDef) {
												if (context.trial && context.trialJournal)
													context.trialJournal->recordVariableWrite(ownerVar);
												ownerVar->type.classInstIndex = refinedInstIndex;
											}
										}
										break;
									}
								}
							}
						}
					}
				}
				expr->type = {DataType::Kind::Void};
				break;
			case IntrinsicReturnKind::Float:
				expr->type = {DataType::Kind::Float, 4};
				break;
			case IntrinsicReturnKind::Custom:
				if (expr->intrinsicName == "address of") {
					DataType varType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (varType.isDeduced())
						expr->type = varType.pointed();
				} else if (expr->intrinsicName == "dereference") {
					DataType ptrType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (ptrType.isDeduced() && ptrType.isPointer())
						expr->type = concretizeClassType(ptrType.dereferenced());
				} else if (expr->intrinsicName == "load at") {
					DataType ptrType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (ptrType.isDeduced() && ptrType.isPointer()) {
						DataType pointedType = ptrType.dereferenced();
						if (pointedType.kind == DataType::Kind::Array && pointedType.arrayElementType)
							expr->type = *pointedType.arrayElementType;
						else
							expr->type = pointedType;
					} else {
						expr->type = {DataType::Kind::Int, 8};
					}
				} else if (expr->intrinsicName == "return") {
					if (expr->arguments.size() >= 2) {
						DataType retType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
						if (retType.isDeduced()) {
							expr->type = retType;
							if (context.currentInstantiation)
								context.currentInstantiation->returnType = retType;
						}
					}
				} else if (expr->intrinsicName == "call") {
					DataType retTypeRef = resolveTypeThroughBindings(expr->arguments[3], macroBindings);
					if (retTypeRef.kind == DataType::Kind::Type)
						expr->type = retTypeRef.toReferencedType();
				} else if (expr->intrinsicName == "cast") {
					DataType valueType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (!valueType.isDeduced() || valueType.kind == DataType::Kind::Void) {
						context.setTypeFailure(
							"Invalid cast source type '" + typeToUserName(valueType, context.parseContext) + "'"
						);
						break;
					}
					if (typeArgType.kind == DataType::Kind::Type) {
						expr->type = concretizeClassType(typeArgType.toReferencedType());
						if (!isSupportedCastConversion(valueType, expr->type)) {
							context.setTypeFailure(
								"Unsupported cast from '" + typeToUserName(valueType, context.parseContext) + "' to '" +
								typeToUserName(expr->type, context.parseContext) + "'"
							);
							break;
						}
					}
				} else if (expr->intrinsicName == "type") {
					// @intrinsic("type", kindString[, bits])
					// Resolve kind string through macro bindings
					Function *kindExpr = resolveThroughBindings(expr->arguments[1], macroBindings);
					std::string kindStr;
					if (auto *str = std::get_if<std::string>(&kindExpr->literalValue))
						kindStr = *str;
					if (!kindStr.empty()) {
						DataType typeRef;
						typeRef.kind = DataType::Kind::Type;
						if (kindStr == "int") {
							typeRef.referencedKind = DataType::Kind::Int;
							typeRef.numericSize = 4; // default
						} else if (kindStr == "float") {
							typeRef.referencedKind = DataType::Kind::Float;
							typeRef.numericSize = 8; // default
						} else if (kindStr == "bool") {
							typeRef.referencedKind = DataType::Kind::Bool;
						} else if (kindStr == "void") {
							typeRef.referencedKind = DataType::Kind::Void;
						} else if (kindStr == "string") {
							// string = pointer to byte (i8*)
							typeRef.referencedKind = DataType::Kind::Int;
							typeRef.numericSize = 1;
							typeRef.pointerDepth = 1;
						}
						// Override byte size if bits argument provided
						if (expr->arguments.size() >= 3) {
							Function *bitsExpr = resolveThroughBindings(expr->arguments[2], macroBindings);
							if (auto *bits = std::get_if<double>(&bitsExpr->literalValue))
								typeRef.numericSize = (int)*bits / 8;
						}
						expr->type = typeRef;
					}
				} else if (expr->intrinsicName == "type of") {
					DataType valueType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (valueType.isDeduced()) {
						expr->type.kind = DataType::Kind::Type;
						expr->type.referencedKind = valueType.kind;
						expr->type.numericSize = valueType.numericSize;
						expr->type.pointerDepth = valueType.pointerDepth;
						expr->type.classDefinition = valueType.classDefinition;
						expr->type.classInstIndex = valueType.classInstIndex;
						expr->type.arraySize = valueType.arraySize;
						expr->type.arrayElementType =
							valueType.arrayElementType ? std::make_shared<DataType>(*valueType.arrayElementType) : nullptr;
					}
				} else if (expr->intrinsicName == "build info") {
					Function *keyExpr = resolveThroughBindings(expr->arguments[1], macroBindings);
					if (auto *key = std::get_if<std::string>(&keyExpr->literalValue)) {
						if (*key == "word size" || *key == "optimization level") {
							expr->type = {DataType::Kind::Int, 4};
						} else {
							expr->type = {DataType::Kind::Int, 1};
							expr->type.pointerDepth = 1;
						}
					}
				} else if (expr->intrinsicName == "select") {
					markCompileTimeParameterRequirements(expr->arguments[1], macroBindings, context.currentInstantiation);
					Function *chosenBranch =
						selectCompileTimeBranch(expr, context.parseContext, macroBindings, context.currentInstantiation);
					if (!chosenBranch) {
						context.setTypeFailure("select condition must be compile-time known");
						break;
					}
					DataType chosenType = resolveTypeThroughBindings(chosenBranch, macroBindings);
					if (chosenType.isDeduced())
						expr->type = chosenType;
				} else if (expr->intrinsicName == "array") {
					Function *sizeExpr = resolveThroughBindings(expr->arguments[1], macroBindings);
					if (auto *size = std::get_if<double>(&sizeExpr->literalValue)) {
						expr->type.kind = DataType::Kind::Type;
						expr->type.referencedKind = DataType::Kind::Array;
						expr->type.arraySize = static_cast<int>(*size);
						if (expr->arguments.size() >= 3) {
							DataType elemTypeRef = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
							if (elemTypeRef.kind == DataType::Kind::Type)
								expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
						}
					}
				} else if (expr->intrinsicName == "vector") {
					int vectorSize = 0;
					if (!evaluateCompileTimeInteger(context.parseContext, expr->arguments[1], macroBindings, vectorSize) ||
						vectorSize < 1) {
						context.setTypeFailure("vector size must be a compile-time integer greater than 0");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Vector;
					expr->type.arraySize = vectorSize;
					expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
					if (expr->arguments.size() >= 3) {
						DataType elemTypeRef = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
						if (elemTypeRef.kind == DataType::Kind::Type)
							expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
				} else if (expr->intrinsicName == "matrix") {
					int rows = 0;
					int columns = 0;
					if (!evaluateCompileTimeInteger(context.parseContext, expr->arguments[1], macroBindings, rows) ||
						!evaluateCompileTimeInteger(context.parseContext, expr->arguments[2], macroBindings, columns) ||
						rows < 1 || columns < 1) {
						context.setTypeFailure("matrix dimensions must be compile-time integers greater than 0");
						break;
					}
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = DataType::Kind::Matrix;
					expr->type.matrixRowCount = rows;
					expr->type.arraySize = columns;
					expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
					if (expr->arguments.size() >= 4) {
						DataType elemTypeRef = resolveTypeThroughBindings(expr->arguments[3], macroBindings);
						if (elemTypeRef.kind == DataType::Kind::Type)
							expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
					}
				} else if (expr->intrinsicName == "add pointer depth") {
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (typeArgType.kind == DataType::Kind::Type) {
						expr->type = typeArgType;
						expr->type.pointerDepth++;
					}
				} else if (expr->intrinsicName == "construct") {
					markCompileTimeParameterRequirements(expr->arguments[1], macroBindings, context.currentInstantiation);
					DataType typeRefType;
					if (!resolveCompileTimeTypeReference(
							context.parseContext, expr->arguments[1], macroBindings, typeRefType
						) ||
						typeRefType.kind != DataType::Kind::Type) {
						break;
					}
					if (typeRefType.referencedKind == DataType::Kind::Array) {
						DataType arrayType = typeRefType.toReferencedType();
						if (arrayType.arraySize == static_cast<int>(expr->arguments.size()) - 2) {
							DataType elementType =
								arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
							bool allDeduced = true;
							for (size_t i = 2; i < expr->arguments.size(); i++) {
								DataType argType = resolveTypeThroughBindings(expr->arguments[i], macroBindings);
								if (!argType.isDeduced())
									allDeduced = false;
								if (!arrayType.arrayElementType) {
									if (!elementType.isDeduced())
										elementType = argType;
									else if (elementType != argType)
										allDeduced = false;
								}
							}
							if (allDeduced && elementType.isDeduced()) {
								arrayType.arrayElementType = std::make_shared<DataType>(elementType);
								expr->type = arrayType;
							}
						}
					} else if (typeRefType.referencedKind == DataType::Kind::Vector) {
						DataType vectorType = typeRefType.toReferencedType();
						if (vectorType.arraySize == static_cast<int>(expr->arguments.size()) - 2) {
							bool allCompatible = true;
							for (size_t i = 2; i < expr->arguments.size(); i++) {
								DataType argType = resolveTypeThroughBindings(expr->arguments[i], macroBindings);
								if (!argType.isDeduced()) {
									allCompatible = false;
									break;
								}
								DataType promoted;
								if (!DataType::promoteArithmetic(argType, *vectorType.arrayElementType, promoted) ||
									promoted != *vectorType.arrayElementType) {
									allCompatible = false;
									break;
								}
							}
							if (allCompatible)
								expr->type = vectorType;
						}
					} else if (typeRefType.referencedKind == DataType::Kind::Matrix) {
						DataType matrixType = typeRefType.toReferencedType();
						if (expr->arguments.size() == 3) {
							DataType valueType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
							if (valueType.kind == DataType::Kind::Array && valueType.arrayElementType &&
								valueType.arraySize == matrixType.matrixRows() * matrixType.matrixColumns()) {
								DataType promoted;
								if (DataType::promoteArithmetic(
										*valueType.arrayElementType, matrixType.matrixElementType(), promoted
									) &&
									promoted == matrixType.matrixElementType()) {
									expr->type = matrixType;
								}
							}
						}
					} else if (typeRefType.classDefinition) {
						std::vector<DataType> argumentTypes;
						argumentTypes.reserve(expr->arguments.size() - 2);
						bool allArgumentsDeduced = true;
						for (size_t i = 2; i < expr->arguments.size(); i++) {
							DataType argumentType = resolveTypeThroughBindings(expr->arguments[i], macroBindings);
							if (!argumentType.isDeduced()) {
								allArgumentsDeduced = false;
								break;
							}
							argumentTypes.push_back(argumentType);
						}

						DataType instantiatedTypeRef;
						if (allArgumentsDeduced && instantiateClassFromArgumentTypes(
													   typeRefType.classDefinition, argumentTypes, instantiatedTypeRef
												   )) {
							expr->type = instantiatedTypeRef.toReferencedType();
						} else {
							DataType targetType = concretizeClassType(typeRefType.toReferencedType());
							if (expr->arguments.size() == targetType.classDefinition->fields.size() + 2 &&
								targetType.classInstIndex >= 0) {
								const auto &fieldTypes =
									targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
								bool allCompatible = argumentTypes.size() == fieldTypes.size();
								for (size_t i = 0; allCompatible && i < fieldTypes.size(); i++) {
									if (argumentTypes[i] != fieldTypes[i])
										allCompatible = false;
								}
								if (allCompatible)
									expr->type = targetType;
							}
						}
					} else if (expr->arguments.size() == 3) {
						DataType targetType = typeRefType.toReferencedType();
						DataType valueType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
						if (valueType.isDeduced())
							expr->type = targetType;
					}
				} else if (expr->intrinsicName == "property") {
					DataType instType = concretizeClassType(resolveTypeThroughBindings(expr->arguments[1], macroBindings));
					Function *propExpr = resolveThroughBindings(expr->arguments[2], macroBindings);
					std::string fieldName = extractFieldName(propExpr);
					DataType builtInPropertyType = resolveBuiltInPropertyType(instType, fieldName);
					if (builtInPropertyType.isDeduced()) {
						expr->type = builtInPropertyType;
						break;
					}
					if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
						if (!fieldName.empty()) {
							ClassDefinition *classDef = instType.classDefinition;
							for (size_t i = 0; i < classDef->fields.size(); i++) {
								if (classDef->fields[i].name == fieldName) {
									expr->type = classDef->instantiations[instType.classInstIndex].fieldTypes[i];
									break;
								}
							}
						}
					}
				}
				break;
			}
		}
		break;
	}

	case Function::Kind::PatternCall: {
		if (expandsToSelectIntrinsic(expr)) {
			if (expr->arguments.size() < 3) {
				context.setTypeFailure("select pattern requires condition and both branches");
				break;
			}
			CompileTimeValue conditionValue =
				evaluateCompileTimeValue(expr->arguments[1], context.parseContext, macroBindings, context.currentInstantiation);
			markCompileTimeParameterRequirements(expr->arguments[1], macroBindings, context.currentInstantiation);
			std::optional<bool> condition = compileTimeTruthiness(conditionValue);
			if (!condition.has_value()) {
				context.setTypeFailure("select condition must be compile-time known");
				break;
			}
			size_t chosenIndex = *condition ? 0 : 2;
			Function *activeBranch = expr->arguments[chosenIndex];
			if (!inferFunction(activeBranch, context, false, macroBindings))
				return;
			expr->arguments[chosenIndex] = activeBranch;
			expr->type = resolveTypeThroughBindings(activeBranch, macroBindings);
			break;
		}
		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;

		// Build argument types for overload selection.
		// Arguments are sorted by source position and include both Variable and Word captures.
		std::vector<DataType> argTypesForOverload;
		if (!expandsToSelectIntrinsic(expr)) {
			for (size_t ai = 0; ai < expr->arguments.size(); ai++)
				argTypesForOverload.push_back(resolveTypeThroughBindings(expr->arguments[ai], macroBindings));
		}

		// Select the best overload based on argument types
		PatternDefinition *def = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload);
		if (!def) {
			std::string candidates;
			std::unordered_set<std::string> uniqueCandidates;
			for (PatternDefinition *candidate : defs) {
				std::string pattern = (std::string)candidate->range.subString;
				if (!pattern.empty() && !uniqueCandidates.contains(pattern)) {
					if (!candidates.empty())
						candidates += ", ";
					candidates += "'" + pattern + "'";
					uniqueCandidates.insert(pattern);
				}
			}
			context.setTypeFailure(
				"No overload matches call '" + (std::string)expr->range.subString + "' for argument types [" +
				formatTypeList(argTypesForOverload, context.parseContext) + "]" +
				(candidates.empty() ? "" : (". Available overloads: " + candidates))
			);
			break;
		}

		Section *matchedSection = def->section;

		// Build parameter bindings from call-site arguments
		std::unordered_map<std::string, Function *> callBindings;
		size_t argIndex = 0;
		for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
			auto paramIt = node->parameterNames.find(def);
			if (paramIt != node->parameterNames.end() && argIndex < expr->arguments.size()) {
				Function *actualArg = expr->arguments[argIndex++];
				actualArg = resolveThroughBindings(actualArg, macroBindings);
				callBindings[paramIt->second] = actualArg;
			}
		}

		if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
			auto *classSec = static_cast<ClassSection *>(matchedSection);
			expr->type = instantiateBoundClassType(context.parseContext, classSec->classDefinition, callBindings);
		} else if (matchedSection->isMacro) {
			// Code replacement: infer body, type = replacement function type
			if (!matchedSection->inferring) {
				matchedSection->inferring = true;
				ScopedDiagnosticSuppression suppressDiagnostics(context);
				inferSection(matchedSection, context, callBindings);
				matchedSection->inferring = false;
			}
			if (!context.typesValid)
				break;
			for (Section *child : matchedSection->children) {
				for (CodeLine *line : child->codeLines) {
					if (!line->function)
						continue;
					DataType resolvedType = resolveTypeThroughBindings(line->function, callBindings);
					if (resolvedType.isDeduced()) {
						line->function->type = resolvedType;
						expr->type = resolvedType;
					} else if (line->function->type.isDeduced()) {
						expr->type = line->function->type;
					}
				}
			}
		} else {
			// Non-macro function: infer body per-instantiation
			// Build parameter bindings and argTypes in nodesPassed order (must match codegen's paramBindings order)
			std::vector<std::pair<std::string, Function *>> paramBindings;
			std::vector<DataType> argTypes;
			for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
				auto paramIt = node->parameterNames.find(def);
				if (paramIt != node->parameterNames.end()) {
					Function *argExpr = callBindings[paramIt->second];
					paramBindings.push_back({paramIt->second, argExpr});
					argTypes.push_back(resolveTypeThroughBindings(argExpr, macroBindings));
				}
			}

			// Skip if any argument type is undeduced — can't meaningfully
			// infer the body without knowing all argument types.
			bool allDeduced = true;
			for (auto &t : argTypes) {
				if (!t.isDeduced()) {
					allDeduced = false;
					break;
				}
			}
			if (!allDeduced)
				break;

			if (context.trial && context.trialJournal)
				context.trialJournal->recordSectionInstantiationWrite(matchedSection, argTypes);
			Instantiation &inst = matchedSection->instantiations[argTypes];
			seedInstantiationCompileTimeParameters(
				context.parseContext, inst, paramBindings, macroBindings, context.currentInstantiation
			);
			if (!inst.inferring) {
				inst.inferring = true;
				Instantiation *savedInst = context.currentInstantiation;
				context.currentInstantiation = &inst;
				bool inferenceSucceeded = inferSection(matchedSection, context, callBindings);
				context.currentInstantiation = savedInst;
				inst.inferring = false;
				inst.valid = inferenceSucceeded;
			} else if (inst.returnType.isDeduced()) {
				expr->type = inst.returnType;
			}
			if (!inst.valid) {
				context.typesValid = false;
				break;
			}
			if (!context.typesValid)
				break;

			if (inst.parameterTypes.size() != paramBindings.size()) {
				inst.parameterTypes.clear();
				for (size_t i = 0; i < paramBindings.size(); i++) {
					Variable *paramVar = findVariableInSectionTree(matchedSection, paramBindings[i].first);
					if (paramVar && paramVar->type.isDeduced())
						inst.parameterTypes.push_back(paramVar->type);
					else
						inst.parameterTypes.push_back(argTypes[i]);
				}
			}
			for (size_t i = 0; i < paramBindings.size() && i < inst.parameterTypes.size(); i++) {
				const DataType &parameterType = inst.parameterTypes[i];
				if (!parameterType.isDeduced() || parameterType == argTypes[i])
					continue;
				Function *argExpr = resolveThroughBindings(paramBindings[i].second, macroBindings);
				if (!argExpr || argExpr->kind != Function::Kind::Variable || !argExpr->variable)
					continue;
				Section *argSection = argExpr->range.line ? argExpr->range.line->section : nullptr;
				Variable *argVar = argSection ? argSection->findVariable(argExpr->variable->name) : nullptr;
				if (!argVar)
					continue;
				if (!argVar->type.isDeduced() || argVar->type == parameterType) {
					if (context.trial && context.trialJournal)
						context.trialJournal->recordVariableWrite(argVar);
					commitVariableTypeFromValue(argVar, argExpr, parameterType);
				} else if (argVar->type.kind == DataType::Kind::Class && parameterType.kind == DataType::Kind::Class &&
						   argVar->type.classDefinition == parameterType.classDefinition) {
					if (context.trial && context.trialJournal)
						context.trialJournal->recordVariableWrite(argVar);
					argVar->type.classInstIndex = parameterType.classInstIndex;
				}
			}

			// If no return intrinsic was found, default to Void
			if (!inst.inferring && inst.returnType.kind == DataType::Kind::Any) {
				inst.returnType = {DataType::Kind::Void};
			}
			if (inst.returnType.isDeduced())
				expr->type = inst.returnType;
		}

		break;
	}

	case Function::Kind::Pending:
		break;
	}
}

// Recompute function ranges bottom-up after reordering. After swapping parent-child
// relationships, the old root retains the full-line range even though it's now a child.
// Fix by spanning each PatternCall's range from its first to last argument.
static void recomputeRanges(Function *expr) {
	if (!expr)
		return;
	for (Function *arg : expr->arguments)
		recomputeRanges(arg);
	if (expr->kind == Function::Kind::PatternCall && !expr->arguments.empty()) {
		int originalStart = expr->range.start();
		int originalEnd = expr->range.end();
		int minStart = expr->arguments.front()->range.start();
		int maxEnd = expr->arguments.front()->range.end();
		for (Function *arg : expr->arguments) {
			minStart = std::min(minStart, arg->range.start());
			maxEnd = std::max(maxEnd, arg->range.end());
		}
		if (!startsWithArgument(expr))
			minStart = originalStart;
		if (!endsWithArgument(expr))
			maxEnd = originalEnd;
		expr->range = Range(expr->range.line, minStart, maxEnd);
	}
}

// Reset non-literal function types in a subtree.
static void resetFunctionTypes(Function *expr) {
	if (!expr)
		return;
	if (expr->kind != Function::Kind::Literal)
		expr->type = {};
	for (Function *arg : expr->arguments)
		resetFunctionTypes(arg);
}

// Sort arguments by source position recursively for all PatternCall nodes.
static void sortArgumentsRecursive(Function *expr) {
	if (!expr)
		return;
	for (Function *arg : expr->arguments)
		sortArgumentsRecursive(arg);
	if (expr->kind == Function::Kind::PatternCall)
		expr->arguments = sortArgumentsByPosition(expr->arguments);
}

static bool startsWithArgument(Function *function) {
	return function->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
}

static bool endsWithArgument(Function *function) {
	return function->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
}

static size_t countLeadingBoundaryArguments(Function *function) {
	if (!function || function->kind != Function::Kind::PatternCall || !function->patternMatch ||
		!function->patternMatch->matchedEndNode)
		return 0;

	size_t maxLeadingCount = 0;
	for (PatternDefinition *def : function->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def)
			continue;
		size_t leadingCount = 0;
		for (PatternTreeNode *node : function->patternMatch->nodesPassed) {
			if (!node->parameterNames.contains(def))
				break;
			leadingCount++;
		}
		maxLeadingCount = std::max(maxLeadingCount, leadingCount);
	}
	return maxLeadingCount;
}

static size_t countTrailingBoundaryArguments(Function *function) {
	if (!function || function->kind != Function::Kind::PatternCall || !function->patternMatch ||
		!function->patternMatch->matchedEndNode)
		return 0;

	size_t maxTrailingCount = 0;
	for (PatternDefinition *def : function->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def)
			continue;
		size_t trailingCount = 0;
		for (auto it = function->patternMatch->nodesPassed.rbegin(); it != function->patternMatch->nodesPassed.rend(); ++it) {
			if (!(*it)->parameterNames.contains(def))
				break;
			trailingCount++;
		}
		maxTrailingCount = std::max(maxTrailingCount, trailingCount);
	}
	return maxTrailingCount;
}

static bool hasMultipleBoundaryArguments(Function *function) {
	return countLeadingBoundaryArguments(function) > 1 || countTrailingBoundaryArguments(function) > 1;
}

static bool functionContainsExplicitReturn(Function *function) {
	if (!function)
		return false;
	if (function->kind == Function::Kind::IntrinsicCall && function->intrinsicName == "return")
		return true;
	std::unordered_map<std::string, Function *> ignoredBindings;
	Function *bodyExpr = expandMacroPatternCall(function, ignoredBindings);
	if (bodyExpr && functionContainsExplicitReturn(bodyExpr))
		return true;
	for (Function *arg : function->arguments) {
		if (functionContainsExplicitReturn(arg))
			return true;
	}
	return false;
}

static bool expandsToSelectIntrinsic(Function *function) {
	std::unordered_map<std::string, Function *> ignoredBindings;
	Function *bodyExpr = expandMacroPatternCall(function, ignoredBindings);
	return bodyExpr && bodyExpr->kind == Function::Kind::IntrinsicCall && bodyExpr->intrinsicName == "select";
}

static bool sectionDefaultsToVoid(Section *section) {
	if (!section || section->type != SectionType::Function)
		return false;
	for (Section *child : section->children) {
		for (CodeLine *line : child->codeLines) {
			if (functionContainsExplicitReturn(line->function))
				return false;
		}
	}
	return true;
}

static bool mustOwnEntireRange(Function *function) {
	if (!function || function->kind != Function::Kind::PatternCall || !function->patternMatch ||
		!function->patternMatch->matchedEndNode)
		return false;

	bool sawCandidate = false;
	for (PatternDefinition *def : function->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def || !def->section)
			continue;
		sawCandidate = true;
		if (def->section->isMacro) {
			std::unordered_map<std::string, Function *> ignoredBindings;
			Function *bodyExpr = expandMacroPatternCall(function, ignoredBindings);
			if (!bodyExpr)
				return false;
			if (bodyExpr->kind == Function::Kind::IntrinsicCall) {
				if (bodyExpr->intrinsicName == "return")
					continue;
				const IntrinsicInfo *info = findIntrinsic(bodyExpr->intrinsicName);
				if (info && info->returnKind == IntrinsicReturnKind::Void)
					continue;
			}
			return false;
		}
		if (!sectionDefaultsToVoid(def->section))
			return false;
	}
	if (!sawCandidate)
		return false;
	return true;
}

static int functionPrecedence(Function *function) {
	if (!function || function->isExplicitGroup || function->kind != Function::Kind::PatternCall || !function->patternMatch ||
		!function->patternMatch->matchedEndNode || function->patternMatch->matchedEndNode->matchingDefinitions.empty())
		return 0;
	auto countParameters = [](PatternDefinition *def) {
		if (!def)
			return 0;
		int count = 0;
		for (const auto &elem : def->patternElements) {
			if (elem.type == PatternElement::Type::Variable)
				count++;
		}
		return count;
	};
	int precedence = 0;
	for (PatternDefinition *def : function->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def || countParameters(def) != (int)function->arguments.size())
			continue;
		if (def->precedence <= 0)
			continue;
		if (precedence == 0 || def->precedence < precedence)
			precedence = def->precedence;
	}
	return precedence;
}

// Infer an function's types, reordering operands if needed.
// If alreadyOrdered is true, skips reordering and just resets types and infers.
// Returns false on failure (no valid grouping found).
static bool inferFunction(
	Function *&expr, InferenceContext &context, bool alreadyOrdered,
	const std::unordered_map<std::string, Function *> &macroBindings = {}
) {
	recomputeRanges(expr);
	sortArgumentsRecursive(expr);
	Function *originalExpr = cloneFunctionTree(expr);
	auto inferNestedForGrouping = [&](Function *&subExpr) -> bool {
		auto isMacroPatternCall = [&](Function *candidate) -> bool {
			if (!candidate || candidate->kind != Function::Kind::PatternCall || !candidate->patternMatch ||
				!candidate->patternMatch->matchedEndNode)
				return false;
			auto &defs = candidate->patternMatch->matchedEndNode->matchingDefinitions;
			if (defs.empty())
				return false;
			std::vector<DataType> argTypesForOverload;
			for (Function *arg : candidate->arguments)
				argTypesForOverload.push_back(resolveTypeThroughBindings(arg, macroBindings));
			PatternDefinition *def =
				selectOverload(defs, candidate->arguments, candidate->patternMatch->nodesPassed, argTypesForOverload);
			return def && def->section && def->section->isMacro;
		};

		InferenceContext::TrialJournal journal;
		InferenceContext trialContext(context.parseContext, true);
		trialContext.currentInstantiation = context.currentInstantiation;
		trialContext.trialJournal = &journal;
		bool ok = inferFunction(subExpr, trialContext, false, macroBindings);
		if (!ok && context.typeFailureDetail.empty())
			context.typeFailureDetail = trialContext.typeFailureDetail;
		if (ok && isMacroPatternCall(subExpr)) {
			DataType resolvedType = inferFunctionTypeWithoutSideEffects(subExpr, context, macroBindings);
			ok = resolvedType.isDeduced();
		}
		rollbackTrialJournal(journal);
		return ok;
	};

	auto tryInfer = [&]() -> bool {
		context.typeFailureDetail.clear();
		inferOrderedFunction(expr, context, macroBindings);
		return context.typesValid;
	};

	if (alreadyOrdered) {
		if (!tryInfer()) {
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			return false;
		}
		return true;
	}
	if (expandsToSelectIntrinsic(expr)) {
		if (!tryInfer()) {
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			return false;
		}
		return true;
	}
	if (mustOwnEntireRange(expr)) {
		for (Function *&argument : expr->arguments) {
			if (!inferNestedForGrouping(argument)) {
				expr = originalExpr;
				resetFunctionTypes(expr);
				context.addDiagnostic(
					{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
					 originalExpr->range}
				);
				return false;
			}
		}
		if (!tryInfer()) {
			expr = originalExpr;
			resetFunctionTypes(expr);
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			return false;
		}
		return true;
	}
	// Flatten the function tree into token order for reordering.
	// Operators (PatternCalls starting/ending with an argument) are interleaved
	// with their boundary arguments. Everything else is a leaf.
	//
	// Example: "print the x of p + the y of p as line" produces:
	//   [print, the_x_of, p, +, the_y_of, p, as_line]
	//    pfx    pfx      leaf inx pfx     leaf sfx
	std::vector<Function *> flatNodes;
	size_t operatorCount = 0;
	std::function<void(Function *&, bool, bool)> collectFlatNodes = [&](Function *&function, bool isOnBoundary, bool isRoot) {
		bool isPatternCall =
			function->kind == Function::Kind::PatternCall && !function->arguments.empty() && !function->isExplicitGroup;

		if (!isPatternCall) {
			flatNodes.push_back(function);
			return;
		}

		// Explicitly grouped functions and non-subMatch PatternCalls are independent
		// groups. The root is always flattened regardless of grouping origin.
		if (!isRoot &&
			(function->isExplicitGroup || !isOnBoundary || !function->isSubMatch || hasMultipleBoundaryArguments(function))) {
			if (!inferNestedForGrouping(function)) {
				context.typesValid = false;
				return;
			}
			flatNodes.push_back(function);
			return;
		}

		bool hasLeftEdge = startsWithArgument(function);
		bool hasRightEdge = endsWithArgument(function);

		if (!hasLeftEdge && !hasRightEdge) {
			// No boundary arguments (e.g. "draw $ lines") — leaf.
			for (Function *&argument : function->arguments) {
				if (!inferNestedForGrouping(argument)) {
					context.typesValid = false;
					return;
				}
			}
			flatNodes.push_back(function);
			return;
		}

		// Prefix/suffix patterns with multiple adjacent boundary arguments must keep
		// those argument ranges internal to the call. Exposing only the first/last
		// argument to the outer regrouping logic lets operators leak across adjacent
		// argument boundaries, e.g. `a new vector from x + y z + w ...`.
		if (hasMultipleBoundaryArguments(function)) {
			for (Function *&argument : function->arguments) {
				if (!inferNestedForGrouping(argument)) {
					context.typesValid = false;
					return;
				}
			}
			flatNodes.push_back(function);
			return;
		}

		// Operator: recurse into boundary args, add self in between (in-order).
		if (hasLeftEdge)
			collectFlatNodes(function->arguments.front(), true, false);

		// Infer non-boundary arguments independently.
		for (size_t i = (hasLeftEdge ? 1 : 0); i < function->arguments.size() - (hasRightEdge ? 1 : 0); i++) {
			if (!inferNestedForGrouping(function->arguments[i])) {
				context.typesValid = false;
				return;
			}
		}

		operatorCount++;
		flatNodes.push_back(function);

		if (hasRightEdge)
			collectFlatNodes(function->arguments.back(), true, false);
	};

	collectFlatNodes(expr, true, true);
	if (!context.typesValid) {
		expr = originalExpr;
		resetFunctionTypes(expr);
		context.addDiagnostic(
			{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail), originalExpr->range}
		);
		return false;
	}
	if (operatorCount <= 1) {
		if (!tryInfer()) {
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			return false;
		}
		return true;
	}

	size_t ambiguousOperatorCount = operatorCount;
	if (expr && expr->kind == Function::Kind::PatternCall && mustOwnEntireRange(expr) && ambiguousOperatorCount > 0)
		ambiguousOperatorCount--;
	if (ambiguousOperatorCount > 8) {
		context.addDiagnostic({Diagnostic::Level::Error, "Too many ambiguous operand groupings", expr->range});
		return false;
	}

	// Constrained Catalan enumeration over the flat token-order sequence.
	// Pick an operator as root of the range, partition into left/right subtrees, recurse.
	//
	// Root constraints based on operator shape:
	//   Prefix  (e.g. "print $"):    can only be root at start of range
	//   Postfix (e.g. "$ as line"):  can only be root at end of range
	//   Infix   (e.g. "$ + $"):      needs nodes on both sides
	//   Leaf    (e.g. variable, literal): only valid alone (start == end)
	//
	// Example: print  the_x_of  p  +  the_y_of  p  as_line
	//          pfx    pfx      leaf inx pfx     leaf sfx
	//
	// Picking + as root of the full range:
	//          +                 <- root
	//         / \
	//   print    the_y_of       <- left/right subtrees
	//   the_x_of   p  as_line
	//     p
	//
	// Right-to-left iteration prefers left-to-right evaluation order.
	// Returns true on first valid grouping (early exit propagates up).
	std::function<bool(int, int, std::function<bool(Function *)>)> tryGroupings =
		[&](int start, int end, std::function<bool(Function *)> onResult) -> bool {
		if (start > end)
			return onResult(nullptr);
		if (start == end)
			return onResult(flatNodes[start]);

		Function *mandatoryRoot = flatNodes[start];
		bool rangeStartsWithMandatoryPrefix = mandatoryRoot->kind == Function::Kind::PatternCall &&
											  !mandatoryRoot->arguments.empty() && !mandatoryRoot->isExplicitGroup &&
											  !startsWithArgument(mandatoryRoot) && endsWithArgument(mandatoryRoot) &&
											  mustOwnEntireRange(mandatoryRoot);

		for (int rootIndex = end; rootIndex >= start; rootIndex--) {
			Function *rootFunction = flatNodes[rootIndex];
			if (rangeStartsWithMandatoryPrefix && rootIndex != start)
				continue;

			bool isPatternCall = rootFunction->kind == Function::Kind::PatternCall && !rootFunction->arguments.empty() &&
								 !rootFunction->isExplicitGroup;
			if (!isPatternCall)
				continue;
			bool hasLeftEdge = startsWithArgument(rootFunction);
			bool hasRightEdge = endsWithArgument(rootFunction);
			if (!hasLeftEdge && rootIndex > start)
				continue;
			if (!hasRightEdge && rootIndex < end)
				continue;
			if (hasLeftEdge && hasRightEdge && (rootIndex == start || rootIndex == end))
				continue;
			int rootPrecedence = functionPrecedence(rootFunction);
			if (rootPrecedence > 0 && hasLeftEdge && hasRightEdge) {
				bool lowerPrecedenceExists = false;
				for (int otherIndex = start; otherIndex <= end; otherIndex++) {
					if (otherIndex == rootIndex)
						continue;
					Function *otherFunction = flatNodes[otherIndex];
					if (otherFunction->kind != Function::Kind::PatternCall || otherFunction->arguments.empty() ||
						otherFunction->isExplicitGroup)
						continue;
					if (!startsWithArgument(otherFunction) || !endsWithArgument(otherFunction))
						continue;
					int otherPrecedence = functionPrecedence(otherFunction);
					if (otherPrecedence > 0 && otherPrecedence < rootPrecedence) {
						lowerPrecedenceExists = true;
						break;
					}
				}
				if (lowerPrecedenceExists)
					continue;
			}

			Function *savedLeft = hasLeftEdge ? rootFunction->arguments.front() : nullptr;
			Function *savedRight = hasRightEdge ? rootFunction->arguments.back() : nullptr;
			bool done = tryGroupings(start, rootIndex - 1, [&](Function *leftResult) -> bool {
				if (hasLeftEdge)
					rootFunction->arguments.front() = leftResult;
				return tryGroupings(rootIndex + 1, end, [&](Function *rightResult) -> bool {
					if (hasRightEdge)
						rootFunction->arguments.back() = rightResult;
					return onResult(rootFunction);
				});
			});
			if (done)
				return true;
			if (hasLeftEdge)
				rootFunction->arguments.front() = savedLeft;
			if (hasRightEdge)
				rootFunction->arguments.back() = savedRight;
		}
		return false;
	};

	int lastIndex = (int)flatNodes.size() - 1;
	std::string trialFailureDetail;
	bool found = tryGroupings(0, lastIndex, [&](Function *rootFunction) -> bool {
		expr = rootFunction;
		resetFunctionTypes(expr);
		InferenceContext::TrialJournal journal;
		InferenceContext trialContext(context.parseContext, true);
		trialContext.currentInstantiation = context.currentInstantiation;
		trialContext.trialJournal = &journal;
		inferOrderedFunction(expr, trialContext, macroBindings);
		if (!trialContext.typesValid && trialFailureDetail.empty() && !trialContext.typeFailureDetail.empty())
			trialFailureDetail = trialContext.typeFailureDetail;
		rollbackTrialJournal(journal);
		return trialContext.typesValid;
	});

	if (found) {
		recomputeRanges(expr);
		sortArgumentsRecursive(expr);
		resetFunctionTypes(expr);
		inferOrderedFunction(expr, context, macroBindings);
		return context.typesValid;
	}

	expr = originalExpr;
	resetFunctionTypes(expr);
	context.addDiagnostic(
		{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, trialFailureDetail), originalExpr->range}
	);
	return false;
}

// Returns false on failure (sets context.typesValid = false).
static bool
inferSection(Section *section, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings) {
	// The first instantiation determines operand ordering; subsequent ones reuse it.
	// size() > 1 because the current instantiation is already inserted before inferSection is called.
	bool alreadyOrdered = section->instantiations.size() > 1;
	if (context.trial && context.trialJournal)
		context.trialJournal->recordTouchedSection(section);
	resetSectionFunctionTypes(section);
	resetSectionLocalVariableTypes(section);
	for (const auto &[name, boundExpr] : bindings) {
		Variable *boundVar = section->findVariable(name);
		if (!boundVar)
			continue;
		DataType boundType = resolveTypeThroughBindings(boundExpr, bindings);
		if (!boundType.isDeduced())
			continue;
		if (context.trial && context.trialJournal)
			context.trialJournal->recordVariableWrite(boundVar);
		commitVariableTypeFromValue(boundVar, boundExpr, boundType);
	}

	for (CodeLine *line : section->codeLines) {
		if (line->function) {
			if (!inferFunction(line->function, context, alreadyOrdered, bindings)) {
				context.typesValid = false;
				return false;
			}
		}
		if (line->sectionOpening && !dynamic_cast<DefinitionSection *>(line->sectionOpening)) {
			if (!inferSection(line->sectionOpening, context, bindings)) {
				context.typesValid = false;
				return false;
			}
		}
	}
	context.typesValid = true;
	return true;
}

bool inferTypes(ParseContext &parseContext) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	InferenceContext context(parseContext);
	if (!inferSection(parseContext.mainSection, context))
		return false;

	// Validate variables — all must have deduced types
	// Skip non-macro function body sections: their variables only get types during monomorphization
	bool valid = true;
	std::function<void(Section *)> validateVariables = [&](Section *section) {
		if (section->parent && !section->parent->isMacro && !section->parent->patternDefinitions.empty())
			return;
		for (auto &[name, var] : section->variables) {
			if (!var->type.isDeduced()) {
				parseContext.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, "Variable '" + name + "' has no type (never assigned a value)",
					var->definition->range
				));
				valid = false;
			}
		}
		for (Section *child : section->children)
			validateVariables(child);
	};
	validateVariables(parseContext.mainSection);

	// Validate non-macro function functions have deduced return types
	std::function<void(Section *)> validateReturnTypes = [&](Section *section) {
		if (section->type == SectionType::Function && !section->isMacro && !section->patternDefinitions.empty()) {
			for (auto &[argTypes, inst] : section->instantiations) {
				(void)argTypes;
				if (!inst.valid)
					continue;
				if (!inst.returnType.isDeduced()) {
					parseContext.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Function '" + (std::string)section->patternDefinitions.front()->range.subString +
							"' has no deduced return type",
						section->patternDefinitions.front()->range
					));
					valid = false;
					break; // one error per section is enough
				}
			}
		}
		for (Section *child : section->children)
			validateReturnTypes(child);
	};
	validateReturnTypes(parseContext.mainSection);

	return valid;
}

bool ensureSectionInstantiationInferred(
	ParseContext &parseContext, Section *section, const std::unordered_map<std::string, Function *> &callBindings,
	const std::vector<DataType> &argTypes, const Instantiation *callerInstantiation
) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	if (!section)
		return false;

	Instantiation &inst = section->instantiations[argTypes];
	for (const auto &[name, argExpr] : callBindings) {
		CompileTimeValue value = evaluateCompileTimeValue(argExpr, parseContext, {}, callerInstantiation);
		if (isCompileTimeKnown(value))
			inst.constantParameterValues[name] = value;
		else
			inst.constantParameterValues.erase(name);
	}
	if (inst.returnType.isDeduced())
		return inst.valid;
	if (inst.inferring)
		return inst.returnType.isDeduced() && inst.valid;

	InferenceContext context(parseContext);
	inst.inferring = true;
	Instantiation *savedInst = context.currentInstantiation;
	context.currentInstantiation = &inst;
	bool inferenceSucceeded = inferSection(section, context, callBindings);
	context.currentInstantiation = savedInst;
	inst.inferring = false;
	inst.valid = inferenceSucceeded;
	if (!inst.valid || !context.typesValid)
		return false;

	if (inst.returnType.kind == DataType::Kind::Any)
		inst.returnType = {DataType::Kind::Void};

	return inst.returnType.isDeduced();
}
