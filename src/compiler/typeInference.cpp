#include "classDefinition.h"
#include "classSection.h"
#include "compiler.h"
#include "definitionSection.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "type.h"
#include "variable.h"

// Resolve a Variable expression through macro bindings to find the bound expression.
// Only follows Variable → Variable chains; stops at non-Variable expressions (PatternCall,
// IntrinsicCall, Literal, etc.). The caller handles those expression kinds separately.
// See also: resolveThroughMacroLayers (codegen, codegenTypes.cpp) which additionally
// expands macro PatternCalls and operates on the context's binding stack.
static Expression *resolveThroughBindings(Expression *expr, const std::unordered_map<std::string, Expression *> &bindings) {
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return expr;
	auto it = bindings.find(expr->variable->name);
	if (it != bindings.end() && it->second != expr)
		return resolveThroughBindings(it->second, bindings);
	return expr;
}

// Like resolveThroughBindings, but also expands macro PatternCalls to find the
// underlying expression. Outputs the final active bindings in outBindings so the
// caller can resolve arguments of the returned expression. Use when inspecting
// expression kind matters (e.g., detecting a property intrinsic inside a store
// destination). See also: resolveThroughMacroLayers (codegen, codegenTypes.cpp)
// for the codegen equivalent that uses the context's binding stack.
static Expression *resolveThroughBindingsDeep(
	Expression *expr, const std::unordered_map<std::string, Expression *> &bindings,
	std::unordered_map<std::string, Expression *> &outBindings
) {
	expr = resolveThroughBindings(expr, bindings);
	outBindings = bindings;
	if (!expr)
		return expr;
	std::unordered_map<std::string, Expression *> innerBindings;
	Expression *bodyExpr = expandMacroPatternCall(expr, innerBindings);
	if (bodyExpr) {
		for (auto &[name, argExpr] : innerBindings)
			argExpr = resolveThroughBindings(argExpr, bindings);
		return resolveThroughBindingsDeep(bodyExpr, innerBindings, outBindings);
	}
	return expr;
}

// Convenience: resolve an expression through bindings, then return its type.
static DataType resolveTypeThroughBindings(Expression *expr, const std::unordered_map<std::string, Expression *> &bindings) {
	Expression *resolved = resolveThroughBindings(expr, bindings);
	return resolved ? resolved->type : DataType{};
}

// Infer types for a section's code lines with operand reordering. Returns false on failure.
static bool
inferSection(Section *section, ParseContext &context, const std::unordered_map<std::string, Expression *> &bindings = {});

// Infer the type of an expression bottom-up. Returns true if the type changed.
static bool inferOrderedExpression(
	Expression *expr, ParseContext &context, const std::unordered_map<std::string, Expression *> &macroBindings = {}
) {
	if (!expr)
		return false;

	bool changed = false;

	// Recurse into arguments first (bottom-up)
	for (Expression *arg : expr->arguments) {
		changed |= inferOrderedExpression(arg, context, macroBindings);
	}

	DataType oldType = expr->type;

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (std::holds_alternative<double>(expr->literalValue)) {
			expr->type = {DataType::Kind::Float, 8};
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {DataType::Kind::Int, 1};
			expr->type.pointerDepth = 1;
		}
		break;
	}

	case Expression::Kind::Variable: {
		if (expr->variable) {
			std::string varName = expr->variable->name;
			// Check macro bindings first
			auto macroIt = macroBindings.find(varName);
			if (macroIt != macroBindings.end()) {
				DataType boundType = macroIt->second->type;
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

	case Expression::Kind::IntrinsicCall: {
		const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
		if (info) {
			switch (info->returnKind) {
			case IntrinsicReturnKind::SameAsArgs:
				if (info->argCount == 2) {
					DataType argType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (argType.isDeduced())
						expr->type = argType;
				} else {
					DataType leftType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					DataType rightType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (leftType.isDeduced() && rightType.isDeduced()) {
						DataType result;
						DataType::promoteArithmetic(leftType, rightType, result);
						if (!result.isDeduced())
							return false;
						expr->type = result;
					}
				}
				break;
			case IntrinsicReturnKind::Bool:
				expr->type = {DataType::Kind::Bool};
				break;
			case IntrinsicReturnKind::Void:
				// "store" has side effects on variable types beyond just being Void
				if (expr->intrinsicName == "store") {
					std::unordered_map<std::string, Expression *> destBindings;
					Expression *destExpr = resolveThroughBindingsDeep(expr->arguments[1], macroBindings, destBindings);
					DataType valType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (destExpr->kind == Expression::Kind::Variable && destExpr->variable && valType.isDeduced()) {
						Section *sec = destExpr->range.line ? destExpr->range.line->section : nullptr;
						Variable *var = sec ? sec->findVariable(destExpr->variable->name) : nullptr;
						if (var && (!var->type.isDeduced() || var->type == valType)) {
							var->type = valType;
							changed = true;
							// Give this variable its own instantiation copy so property stores
							// don't contaminate other variables sharing the same construct instantiation
							if (var->type.kind == DataType::Kind::Class && var->type.classDefinition &&
								var->type.classInstIndex >= 0) {
								auto &src = var->type.classDefinition->instantiations[var->type.classInstIndex].fieldTypes;
								var->type.classInstIndex = (int)var->type.classDefinition->instantiations.size();
								var->type.classDefinition->instantiations.push_back({src});
							}
						}
					} else if (destExpr->kind == Expression::Kind::IntrinsicCall && destExpr->intrinsicName == "property" &&
							   valType.isDeduced()) {
						DataType instType = resolveTypeThroughBindings(destExpr->arguments[1], destBindings);
						if (instType.kind == DataType::Kind::Class && instType.classDefinition &&
							instType.classInstIndex >= 0) {
							Expression *propExpr = resolveThroughBindings(destExpr->arguments[2], destBindings);
							std::string fieldName;
							if (auto *str = std::get_if<std::string>(&propExpr->literalValue))
								fieldName = *str;
							if (!fieldName.empty()) {
								ClassDefinition *classDef = instType.classDefinition;
								auto &fieldTypes = classDef->instantiations[instType.classInstIndex].fieldTypes;
								for (size_t i = 0; i < classDef->fields.size(); i++) {
									if (classDef->fields[i].name == fieldName &&
										(!fieldTypes[i].isDeduced() || fieldTypes[i] == valType)) {
										fieldTypes[i] = valType;
										changed = true;
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
						expr->type = ptrType.dereferenced();
				} else if (expr->intrinsicName == "load at") {
					expr->type = {DataType::Kind::Int, 8};
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
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (typeArgType.kind == DataType::Kind::Type) {
						expr->type = typeArgType.toReferencedType();
						// If casting to a class type without a specific instantiation,
						// use the declared-types instantiation (index 0) if available.
						if (expr->type.kind == DataType::Kind::Class && expr->type.classDefinition &&
							expr->type.classInstIndex < 0 && !expr->type.classDefinition->instantiations.empty()) {
							expr->type.classInstIndex = 0;
						}
					}
				} else if (expr->intrinsicName == "type") {
					// @intrinsic("type", kindString[, bits])
					// Resolve kind string through macro bindings
					Expression *kindExpr = resolveThroughBindings(expr->arguments[1], macroBindings);
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
							Expression *bitsExpr = resolveThroughBindings(expr->arguments[2], macroBindings);
							if (auto *bits = std::get_if<double>(&bitsExpr->literalValue))
								typeRef.numericSize = (int)*bits / 8;
						}
						expr->type = typeRef;
					}
				} else if (expr->intrinsicName == "add pointer depth") {
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (typeArgType.kind == DataType::Kind::Type) {
						expr->type = typeArgType;
						expr->type.pointerDepth++;
					}
				} else if (expr->intrinsicName == "construct") {
					DataType typeRefType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (typeRefType.kind == DataType::Kind::Type && typeRefType.classDefinition) {
						ClassDefinition *classDef = typeRefType.classDefinition;
						std::vector<DataType> fieldTypes;
						bool allDeduced = true;
						for (size_t i = 2; i < expr->arguments.size(); i++) {
							DataType ft = resolveTypeThroughBindings(expr->arguments[i], macroBindings);
							if (!ft.isDeduced())
								allDeduced = false;
							fieldTypes.push_back(ft);
						}
						if (allDeduced) {
							int instIdx = classDef->getOrCreateInstantiation(fieldTypes);
							expr->type = {DataType::Kind::Class, 0, 0, classDef, instIdx};
						}
					}
				} else if (expr->intrinsicName == "property") {
					DataType instType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
						Expression *propExpr = resolveThroughBindings(expr->arguments[2], macroBindings);
						std::string fieldName;
						if (auto *str = std::get_if<std::string>(&propExpr->literalValue))
							fieldName = *str;
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

	case Expression::Kind::PatternCall: {
		if (expr->patternMatch && expr->patternMatch->matchedEndNode &&
			!expr->patternMatch->matchedEndNode->matchingDefinitions.empty()) {
			auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;

			// Sort arguments by source position (expandMatch appends submatches/variables/words
			// after direct args, so they may not be in text order)
			std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);

			// Build argument types for overload selection
			// Use the first definition to walk nodesPassed (all overloads share the same trie path)
			std::vector<DataType> argTypesForOverload;
			{
				size_t ai = 0;
				for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
					// Check if this node is a parameter for ANY definition at this endpoint
					bool isParam = false;
					for (auto *d : defs) {
						if (node->parameterNames.contains(d)) {
							isParam = true;
							break;
						}
					}
					if (isParam && ai < sortedArgs.size()) {
						DataType argType = resolveTypeThroughBindings(sortedArgs[ai], macroBindings);
						argTypesForOverload.push_back(argType);
						ai++;
					}
				}
			}

			// Select the best overload based on argument types
			PatternDefinition *def = selectOverload(defs, sortedArgs, expr->patternMatch->nodesPassed, argTypesForOverload);
			if (!def)
				def = defs[0]; // fallback if no overload matched (types may not be deduced yet)

			if (def && def->section) {
				Section *matchedSection = def->section;

				// Build parameter bindings from call-site arguments
				std::unordered_map<std::string, Expression *> callBindings;
				size_t argIndex = 0;
				for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
					auto paramIt = node->parameterNames.find(def);
					if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
						// Resolve through current macro bindings if we're inside a macro
						Expression *actualArg = sortedArgs[argIndex];
						if (actualArg->kind == Expression::Kind::Variable && actualArg->variable) {
							auto macroIt = macroBindings.find(actualArg->variable->name);
							if (macroIt != macroBindings.end()) {
								actualArg = macroIt->second;
							}
						}
						callBindings[paramIt->second] = actualArg;
						argIndex++;
					}
				}

				if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
					auto *classSec = static_cast<ClassSection *>(matchedSection);
					expr->type = {DataType::Kind::Type, 0, 0, classSec->classDefinition};
				} else if (matchedSection->isMacro) {
					// Code replacement: infer body, type = replacement expression type
					if (!matchedSection->inferring) {
						matchedSection->inferring = true;
						inferSection(matchedSection, context, callBindings);
						matchedSection->inferring = false;
					}
					for (Section *child : matchedSection->children) {
						for (CodeLine *line : child->codeLines) {
							if (line->expression && line->expression->type.isDeduced())
								expr->type = line->expression->type;
						}
					}
				} else {
					// Non-macro function: infer body per-instantiation
					// Build argTypes in nodesPassed order (must match codegen's paramBindings order)
					std::vector<DataType> argTypes;
					size_t argTypeIndex = 0;
					for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
						auto paramIt = node->parameterNames.find(def);
						if (paramIt != node->parameterNames.end() && argTypeIndex < sortedArgs.size()) {
							Expression *argExpr = callBindings[paramIt->second];
							argTypes.push_back(resolveTypeThroughBindings(argExpr, macroBindings));
							argTypeIndex++;
						}
					}

					// Skip if any argument type is undeduced — can't meaningfully
					// infer the body without knowing all argument types.
					bool allDeduced = true;
					for (auto &t : argTypes)
						if (!t.isDeduced()) {
							allDeduced = false;
							break;
						}
					if (!allDeduced)
						break;

					Instantiation &inst = matchedSection->instantiations[argTypes];
					if (!inst.inferring) {
						inst.inferring = true;
						Instantiation *savedInst = context.currentInstantiation;
						context.currentInstantiation = &inst;
						inferSection(matchedSection, context, callBindings);
						context.currentInstantiation = savedInst;
						inst.inferring = false;
					}

					// If no return intrinsic was found, default to Void
					if (!inst.inferring && inst.returnType.kind == DataType::Kind::Any) {
						inst.returnType = {DataType::Kind::Void};
					}
					if (inst.returnType.isDeduced())
						expr->type = inst.returnType;
				}
			}
		}
		break;
	}

	case Expression::Kind::Pending:
		break;
	}

	return changed || (expr->type != oldType);
}

// Reset non-literal expression types in a subtree.
static void resetExpressionTypes(Expression *expr) {
	if (!expr)
		return;
	if (expr->kind != Expression::Kind::Literal)
		expr->type = {};
	for (Expression *arg : expr->arguments)
		resetExpressionTypes(arg);
}

// A shared argument edge between two adjacent PatternCall expressions.
struct SharedEdge {
	Expression *parent;
	Expression *child;
	size_t parentArgIdx; // index in parent->arguments where child lives
	size_t childArgIdx;	 // index in child->arguments where shared operand lives
	Expression *shared;	 // the boundary operand
	Range childOrigRange;
};

// Sort arguments by source position recursively for all PatternCall nodes.
static void sortArgumentsRecursive(Expression *expr) {
	if (!expr)
		return;
	for (Expression *arg : expr->arguments)
		sortArgumentsRecursive(arg);
	if (expr->kind == Expression::Kind::PatternCall)
		expr->arguments = sortArgumentsByPosition(expr->arguments);
}

// Collect shared edges in the expression tree (parent-child PatternCall pairs at boundary positions).
static void collectSharedEdges(Expression *expr, std::vector<SharedEdge> &edges) {
	if (expr->kind != Expression::Kind::PatternCall)
		return;
	for (size_t si = 0; si < expr->arguments.size(); si++) {
		Expression *arg = expr->arguments[si];
		if (arg->kind != Expression::Kind::PatternCall || !arg->patternMatch || !arg->patternMatch->matchedEndNode)
			continue;
		if (arg->arguments.empty())
			continue;
		// Shared operand: if child is rightmost parent arg, shared is child's leftmost;
		// if child is leftmost parent arg, shared is child's rightmost.
		size_t sharedSortedIdx;
		if (si == expr->arguments.size() - 1)
			sharedSortedIdx = 0;
		else if (si == 0)
			sharedSortedIdx = arg->arguments.size() - 1;
		else
			continue; // middle args — no clear boundary
		Expression *shared = arg->arguments[sharedSortedIdx];
		size_t childArgIdx = std::find(arg->arguments.begin(), arg->arguments.end(), shared) - arg->arguments.begin();
		edges.push_back({expr, arg, si, childArgIdx, shared, arg->range});
	}
	for (Expression *arg : expr->arguments)
		collectSharedEdges(arg, edges);
}

// Swap the parent-child relationship at a shared edge.
static void swapEdge(SharedEdge &edge) {
	auto *parent = edge.parent;
	auto *child = edge.child;
	auto pMatch = parent->patternMatch;
	auto pArgs = parent->arguments;
	auto cArgs = child->arguments;

	parent->patternMatch = child->patternMatch;
	parent->arguments = cArgs;
	parent->arguments[edge.childArgIdx] = child;

	child->patternMatch = pMatch;
	child->arguments = pArgs;
	child->arguments[edge.parentArgIdx] = edge.shared;

	child->range = edge.shared->range;
}

// Unswap: restore original parent-child relationship at a shared edge.
static void unswapEdge(SharedEdge &edge) {
	auto *parent = edge.parent;
	auto *child = edge.child;
	auto pMatch = parent->patternMatch;
	auto pArgs = parent->arguments;
	auto cArgs = child->arguments;

	parent->patternMatch = child->patternMatch;
	parent->arguments = cArgs;
	parent->arguments[edge.parentArgIdx] = child;

	child->patternMatch = pMatch;
	child->arguments = pArgs;
	child->arguments[edge.childArgIdx] = edge.shared;

	child->range = edge.childOrigRange;
}

// Check if an inferred expression tree has valid types:
// PatternCall arguments must be deduced and non-Void.
// Unresolved types occur during recursive function inference (inst.inferring prevents re-entry,
// leaving the return type unknown). Both Void and unresolved args indicate an invalid grouping.
static bool isGroupingValid(Expression *expr) {
	if (!expr)
		return true;
	if (expr->kind == Expression::Kind::PatternCall) {
		for (Expression *arg : expr->arguments) {
			if (!arg->type.isDeduced() || arg->type.kind == DataType::Kind::Void)
				return false;
			if (!isGroupingValid(arg))
				return false;
		}
	}
	return true;
}

// Infer an expression's types, reordering operands if needed.
// If alreadyOrdered is true, skips reordering and just resets types and infers.
// Returns false on failure (no valid grouping found).
static bool inferExpression(
	Expression *expr, ParseContext &context, bool alreadyOrdered,
	const std::unordered_map<std::string, Expression *> &macroBindings = {}
) {
	sortArgumentsRecursive(expr);

	if (alreadyOrdered) {
		resetExpressionTypes(expr);
		inferOrderedExpression(expr, context, macroBindings);
		return isGroupingValid(expr);
	}

	std::vector<SharedEdge> edges;
	collectSharedEdges(expr, edges);
	if (edges.empty()) {
		inferOrderedExpression(expr, context, macroBindings);
		return true;
	}
	if (edges.size() > 6) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Too many ambiguous operand groupings", expr->range)
		);
		return false;
	}

	size_t numGroupings = 1u << edges.size();
	fprintf(
		stderr, "DEBUG reorder '%s': %zu edges, %zu groupings\n", std::string(expr->range.subString).c_str(), edges.size(),
		numGroupings
	);
	for (size_t i = 0; i < edges.size(); i++)
		fprintf(
			stderr, "  edge %zu: parent='%s' child='%s' shared='%s'\n", i,
			std::string(edges[i].parent->range.subString).c_str(), std::string(edges[i].child->range.subString).c_str(),
			std::string(edges[i].shared->range.subString).c_str()
		);
	for (size_t g = 0; g < numGroupings; g++) {
		for (size_t i = 0; i < edges.size(); i++) {
			if (g & (1u << i))
				swapEdge(edges[i]);
		}

		resetExpressionTypes(expr);
		inferOrderedExpression(expr, context, macroBindings);

		fprintf(stderr, "DEBUG g=%zu: type=%s valid=%d\n", g, expr->type.toString().c_str(), isGroupingValid(expr));
		if (isGroupingValid(expr))
			return true;

		for (size_t i = edges.size(); i-- > 0;) {
			if (g & (1u << i))
				unswapEdge(edges[i]);
		}
	}
	context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "No valid operand grouping found", expr->range));
	return false;
}

// Returns false on failure.
static bool
inferSection(Section *section, ParseContext &context, const std::unordered_map<std::string, Expression *> &bindings) {
	// The first instantiation determines operand ordering; subsequent ones reuse it.
	// size() > 1 because the current instantiation is already inserted before inferSection is called.
	bool alreadyOrdered = section->instantiations.size() > 1;

	for (CodeLine *line : section->codeLines) {
		if (line->expression) {
			if (!inferExpression(line->expression, context, alreadyOrdered, bindings))
				return false;
			fprintf(
				stderr, "DEBUG after infer: '%s' type=%s, args=[", std::string(line->expression->range.subString).c_str(),
				line->expression->type.toString().c_str()
			);
			for (auto *a : line->expression->arguments)
				fprintf(stderr, "'%s'(%s) ", std::string(a->range.subString).c_str(), a->type.toString().c_str());
			fprintf(stderr, "]\n");
			fflush(stderr);
		}
		if (line->sectionOpening && !dynamic_cast<DefinitionSection *>(line->sectionOpening)) {
			if (!inferSection(line->sectionOpening, context, bindings))
				return false;
		}
	}
	return true;
}

bool inferTypes(ParseContext &context) {
	if (!inferSection(context.mainSection, context))
		return false;

	// Validate variables — all must have deduced types
	// Skip non-macro function body sections: their variables only get types during monomorphization
	bool valid = true;
	std::function<void(Section *)> validateVariables = [&](Section *section) {
		if (section->parent && !section->parent->isMacro && !section->parent->patternDefinitions.empty())
			return;
		for (auto &[name, var] : section->variables) {
			if (!var->type.isDeduced()) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, "Variable '" + name + "' has no type (never assigned a value)",
					var->definition->range
				));
				valid = false;
			}
		}
		for (Section *child : section->children)
			validateVariables(child);
	};
	validateVariables(context.mainSection);

	// Validate non-macro expression functions have deduced return types
	std::function<void(Section *)> validateReturnTypes = [&](Section *section) {
		if (section->type == SectionType::Expression && !section->isMacro && !section->patternDefinitions.empty()) {
			for (auto &[argTypes, inst] : section->instantiations) {
				if (!inst.returnType.isDeduced()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Expression '" + (std::string)section->patternDefinitions.front()->range.subString +
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
	validateReturnTypes(context.mainSection);

	return valid;
}
