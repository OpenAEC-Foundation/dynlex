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

// Infer the type of an expression bottom-up. Returns true when no compilation errors are found.
static bool inferOrderedExpression(
	Expression *expr, ParseContext &context, bool &validTypes,
	const std::unordered_map<std::string, Expression *> &macroBindings = {}
) {
	validTypes = true;
	// Recurse into arguments first (bottom-up)
	for (Expression *arg : expr->arguments) {
		if (!inferOrderedExpression(arg, context, validTypes, macroBindings))
			return false;
		if (!validTypes)
			return true;
	}

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
		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;

		// Build argument types for overload selection.
		// All overloads share the same trie path, so variable nodes are identical across definitions.
		std::vector<DataType> argTypesForOverload;
		{
			size_t ai = 0;
			for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
				if (node->type == PatternElement::Type::Variable) {
					argTypesForOverload.push_back(resolveTypeThroughBindings(expr->arguments[ai], macroBindings));
					ai++;
				}
			}
		}

		// Select the best overload based on argument types
		PatternDefinition *def = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload);
		if (!def)
			def = defs[0]; // fallback if no overload matched (types may not be deduced yet)

		Section *matchedSection = def->section;

		// Build parameter bindings from call-site arguments
		std::unordered_map<std::string, Expression *> callBindings;
		size_t argIndex = 0;
		for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
			if (node->type == PatternElement::Type::Variable) {
				// Resolve through current macro bindings if we're inside a macro
				Expression *actualArg = expr->arguments[argIndex];
				if (actualArg->kind == Expression::Kind::Variable && actualArg->variable) {
					auto macroIt = macroBindings.find(actualArg->variable->name);
					if (macroIt != macroBindings.end()) {
						actualArg = macroIt->second;
					}
				}
				callBindings[node->parameterNames[def]] = actualArg;
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
			for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
				if (node->type == PatternElement::Type::Variable) {
					Expression *argExpr = callBindings[node->parameterNames[def]];
					argTypes.push_back(resolveTypeThroughBindings(argExpr, macroBindings));
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

		break;
	}

	case Expression::Kind::Pending:
		break;
	}
	return true;
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

// Sort arguments by source position recursively for all PatternCall nodes.
static void sortArgumentsRecursive(Expression *expr) {
	if (!expr)
		return;
	for (Expression *arg : expr->arguments)
		sortArgumentsRecursive(arg);
	if (expr->kind == Expression::Kind::PatternCall)
		expr->arguments = sortArgumentsByPosition(expr->arguments);
}

static bool startsWithArgument(Expression *expression) {
	return expression->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
}

static bool endsWithArgument(Expression *expression) {
	return expression->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
}

// Infer an expression's types, reordering operands if needed.
// If alreadyOrdered is true, skips reordering and just resets types and infers.
// Returns false on failure (no valid grouping found).
static bool inferExpression(
	Expression *&expr, ParseContext &context, bool alreadyOrdered,
	const std::unordered_map<std::string, Expression *> &macroBindings = {}
) {
	sortArgumentsRecursive(expr);

	auto validate = [&]() -> bool {
		bool validTypes;
		inferOrderedExpression(expr, context, validTypes, macroBindings);
		if (!validTypes) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Invalid operand types", expr->range));
			return false;
		}
		return true;
	};

	if (alreadyOrdered) {
		return validate();
	}
	// Flatten the expression tree into token order for reordering.
	// Operators (PatternCalls starting/ending with an argument) are interleaved
	// with their boundary arguments. Everything else is a leaf.
	//
	// Example: "print the x of p + the y of p as line" produces:
	//   [print, the_x_of, p, +, the_y_of, p, as_line]
	//    pfx    pfx      leaf inx pfx     leaf sfx
	std::vector<Expression *> flatNodes;
	size_t operatorCount = 0;

	std::function<void(Expression *, bool, bool)> collectFlatNodes = [&](Expression *expression, bool isOnBoundary,
																		 bool isRoot) {
		bool isPatternCall = expression->kind == Expression::Kind::PatternCall && !expression->arguments.empty();

		if (!isPatternCall) {
			flatNodes.push_back(expression);
			return;
		}

		// Non-subMatch PatternCalls are independent groups (e.g. parenthesized expressions).
		// The root is always flattened regardless of isSubMatch.
		if (!isRoot && (!isOnBoundary || !expression->isSubMatch)) {
			inferExpression(expression, context, false, macroBindings);
			flatNodes.push_back(expression);
			return;
		}

		bool hasLeftEdge = startsWithArgument(expression);
		bool hasRightEdge = endsWithArgument(expression);

		if (!hasLeftEdge && !hasRightEdge) {
			// No boundary arguments (e.g. "draw $ lines") — leaf.
			for (Expression *&argument : expression->arguments)
				inferExpression(argument, context, false, macroBindings);
			flatNodes.push_back(expression);
			return;
		}

		// Operator: recurse into boundary args, add self in between (in-order).
		if (hasLeftEdge)
			collectFlatNodes(expression->arguments.front(), true, false);

		// Infer non-boundary arguments independently.
		for (size_t i = (hasLeftEdge ? 1 : 0); i < expression->arguments.size() - (hasRightEdge ? 1 : 0); i++)
			inferExpression(expression->arguments[i], context, false, macroBindings);

		operatorCount++;
		flatNodes.push_back(expression);

		if (hasRightEdge)
			collectFlatNodes(expression->arguments.back(), true, false);
	};

	collectFlatNodes(expr, true, true);
	if (operatorCount <= 1) {
		return validate();
	}

	if (operatorCount > 6) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Too many ambiguous operand groupings", expr->range)
		);
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
	std::function<bool(int, int, std::function<bool(Expression *)>)> tryGroupings =
		[&](int start, int end, std::function<bool(Expression *)> onResult) -> bool {
		if (start > end)
			return onResult(nullptr);
		if (start == end)
			return onResult(flatNodes[start]);

		for (int rootIndex = end; rootIndex >= start; rootIndex--) {
			Expression *rootExpression = flatNodes[rootIndex];

			bool isPatternCall = rootExpression->kind == Expression::Kind::PatternCall && !rootExpression->arguments.empty();
			if (!isPatternCall)
				continue;
			bool hasLeftEdge = startsWithArgument(rootExpression);
			bool hasRightEdge = endsWithArgument(rootExpression);
			if (!hasLeftEdge && rootIndex > start)
				continue;
			if (!hasRightEdge && rootIndex < end)
				continue;
			if (hasLeftEdge && hasRightEdge && (rootIndex == start || rootIndex == end))
				continue;

			bool done = tryGroupings(start, rootIndex - 1, [&](Expression *leftResult) -> bool {
				if (hasLeftEdge)
					rootExpression->arguments.front() = leftResult;
				return tryGroupings(rootIndex + 1, end, [&](Expression *rightResult) -> bool {
					if (hasRightEdge)
						rootExpression->arguments.back() = rightResult;
					return onResult(rootExpression);
				});
			});
			if (done)
				return true;
		}
		return false;
	};

	int lastIndex = (int)flatNodes.size() - 1;
	bool found = tryGroupings(0, lastIndex, [&](Expression *rootExpression) -> bool {
		expr = rootExpression;
		resetExpressionTypes(expr);
		bool validTypes;
		inferOrderedExpression(expr, context, validTypes, macroBindings);
		return validTypes;
	});

	if (found)
		return true;

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
