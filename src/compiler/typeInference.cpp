#include "classDefinition.h"
#include "classSection.h"
#include "compiler.h"
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
static Type resolveTypeThroughBindings(Expression *expr, const std::unordered_map<std::string, Expression *> &bindings) {
	Expression *resolved = resolveThroughBindings(expr, bindings);
	return resolved ? resolved->type : Type{};
}

// Infer types through a macro body with given parameter bindings. Returns true if anything changed.
static bool
inferMacroBody(Section *macroSection, const std::unordered_map<std::string, Expression *> &bindings, ParseContext &context);

// Infer the type of an expression bottom-up. Returns true if the type changed.
static bool inferExpressionType(
	Expression *expr, ParseContext &context, const std::unordered_map<std::string, Expression *> &macroBindings = {}
) {
	if (!expr)
		return false;

	bool changed = false;

	// Recurse into arguments first (bottom-up)
	for (Expression *arg : expr->arguments) {
		changed |= inferExpressionType(arg, context, macroBindings);
	}

	Type oldType = expr->type;

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (std::holds_alternative<int64_t>(expr->literalValue)) {
			expr->type = {Type::Kind::Numeric};
		} else if (std::holds_alternative<double>(expr->literalValue)) {
			expr->type = {Type::Kind::Float, 8}; // C++ double = f64
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {Type::Kind::Integer, 1, 1};
		}
		break;
	}

	case Expression::Kind::Variable: {
		if (expr->variable) {
			std::string varName = expr->variable->name;
			// Check macro bindings first
			auto macroIt = macroBindings.find(varName);
			if (macroIt != macroBindings.end()) {
				Type boundType = macroIt->second->type;
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
					Type argType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (argType.isDeduced())
						expr->type = argType;
				} else {
					Type leftType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					Type rightType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (leftType.isDeduced() && rightType.isDeduced()) {
						expr->type = isPointerArithmeticOperator(expr->intrinsicName)
										 ? Type::promoteArithmetic(leftType, rightType)
										 : Type::promote(leftType, rightType);
					}
				}
				break;
			case IntrinsicReturnKind::Bool:
				expr->type = {Type::Kind::Bool};
				break;
			case IntrinsicReturnKind::Void:
				// "store" has side effects on variable types beyond just being Void
				if (expr->intrinsicName == "store") {
					std::unordered_map<std::string, Expression *> destBindings;
					Expression *destExpr = resolveThroughBindingsDeep(expr->arguments[1], macroBindings, destBindings);
					Type valType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (destExpr->kind == Expression::Kind::Variable && destExpr->variable && valType.isDeduced()) {
						Section *sec = destExpr->range.line ? destExpr->range.line->section : nullptr;
						Variable *var = sec ? sec->findVariable(destExpr->variable->name) : nullptr;
						if (var && var->type.canRefineTo(valType)) {
							var->type = valType;
							changed = true;
						}
					} else if (destExpr->kind == Expression::Kind::IntrinsicCall && destExpr->intrinsicName == "property" &&
							   valType.isDeduced()) {
						Type instType = resolveTypeThroughBindings(destExpr->arguments[1], destBindings);
						if (instType.kind == Type::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
							Expression *propExpr = resolveThroughBindings(destExpr->arguments[2], destBindings);
							std::string fieldName;
							if (auto *str = std::get_if<std::string>(&propExpr->literalValue))
								fieldName = *str;
							if (!fieldName.empty()) {
								ClassDefinition *classDef = instType.classDefinition;
								auto &fieldTypes = classDef->instantiations[instType.classInstIndex].fieldTypes;
								for (size_t i = 0; i < classDef->fields.size(); i++) {
									if (classDef->fields[i].name == fieldName && fieldTypes[i].canRefineTo(valType)) {
										fieldTypes[i] = valType;
										changed = true;
										break;
									}
								}
							}
						}
					}
				}
				expr->type = {Type::Kind::Void};
				break;
			case IntrinsicReturnKind::Float:
				expr->type = {Type::Kind::Float, 4};
				break;
			case IntrinsicReturnKind::Custom:
				if (expr->intrinsicName == "address of") {
					Type varType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (varType.isDeduced())
						expr->type = varType.pointed();
				} else if (expr->intrinsicName == "dereference") {
					Type ptrType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (ptrType.isDeduced() && ptrType.isPointer())
						expr->type = ptrType.dereferenced();
				} else if (expr->intrinsicName == "load at") {
					expr->type = {Type::Kind::Integer, 8};
				} else if (expr->intrinsicName == "return") {
					if (expr->arguments.size() >= 2) {
						Type retType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
						if (retType.isDeduced()) {
							expr->type = retType;
							if (context.currentInstantiation)
								context.currentInstantiation->returnType = retType;
						}
					}
				} else if (expr->intrinsicName == "call") {
					std::string retTypeStr;
					if (auto *str = std::get_if<std::string>(&expr->arguments[3]->literalValue))
						retTypeStr = *str;
					if (!retTypeStr.empty())
						expr->type = Type::fromString(retTypeStr);
				} else if (expr->intrinsicName == "cast") {
					Type typeArgType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (typeArgType.kind == Type::Kind::TypeReference && typeArgType.classDefinition) {
						ClassDefinition *classDef = typeArgType.classDefinition;
						int instIdx = classDef->instantiations.empty() ? -1 : 0;
						expr->type = {Type::Kind::Class, 0, 0, classDef, instIdx};
					} else {
						// Resolve type string through macro bindings (e.g. "float" might be a macro arg)
						Expression *typeStrExpr = resolveThroughBindings(expr->arguments[2], macroBindings);
						std::string targetStr;
						if (auto *str = std::get_if<std::string>(&typeStrExpr->literalValue))
							targetStr = *str;
						if (targetStr == "integer" || targetStr == "float") {
							Type::Kind kind = targetStr == "integer" ? Type::Kind::Integer : Type::Kind::Float;
							int byteSize = 8;
							if (expr->arguments.size() >= 4) {
								// Resolve bits through macro bindings (e.g. bits=32 might be a macro arg)
								Expression *bitsExpr = resolveThroughBindings(expr->arguments[3], macroBindings);
								if (auto *bits = std::get_if<int64_t>(&bitsExpr->literalValue))
									byteSize = *bits / 8;
							}
							expr->type = {kind, byteSize};
						} else if (!targetStr.empty()) {
							expr->type = Type::fromString(targetStr);
						}
					}
				} else if (expr->intrinsicName == "construct") {
					Type typeRefType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (typeRefType.kind == Type::Kind::TypeReference && typeRefType.classDefinition) {
						ClassDefinition *classDef = typeRefType.classDefinition;
						std::vector<Type> fieldTypes;
						bool allDeduced = true;
						for (size_t i = 2; i < expr->arguments.size(); i++) {
							Type ft = resolveTypeThroughBindings(expr->arguments[i], macroBindings);
							if (!ft.isDeduced())
								allDeduced = false;
							fieldTypes.push_back(ft);
						}
						if (allDeduced) {
							// Prefer an existing instantiation whose fields are refinements
							// of the argument types. This avoids oscillation when later stores
							// promote field types (e.g., Integer fields promoted to Float by
							// a multiply operation).
							int instIdx = -1;
							for (int i = 0; i < (int)classDef->instantiations.size(); i++) {
								auto &existing = classDef->instantiations[i].fieldTypes;
								if (existing.size() != fieldTypes.size())
									continue;
								bool compatible = true;
								for (size_t j = 0; j < fieldTypes.size(); j++) {
									if (existing[j] != fieldTypes[j] && !fieldTypes[j].canRefineTo(existing[j])) {
										compatible = false;
										break;
									}
								}
								if (compatible) {
									instIdx = i;
									break;
								}
							}
							if (instIdx < 0)
								instIdx = classDef->getOrCreateInstantiation(fieldTypes);
							expr->type = {Type::Kind::Class, 0, 0, classDef, instIdx};
						}
					}
				} else if (expr->intrinsicName == "property") {
					Type instType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (instType.kind == Type::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
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
			std::vector<Type> argTypesForOverload;
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
						Type argType = resolveTypeThroughBindings(sortedArgs[ai], macroBindings);
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

				if (matchedSection->type == SectionType::Class) {
					auto *classSec = static_cast<ClassSection *>(matchedSection);
					expr->type = {Type::Kind::TypeReference, 0, 0, classSec->classDefinition};
				} else if (matchedSection->type == SectionType::Effect) {
					// Effects: infer body, result type is Void
					changed |= inferMacroBody(matchedSection, callBindings, context);
					expr->type = {Type::Kind::Void};
				} else if (matchedSection->isMacro) {
					// Code replacement: infer body, type = replacement expression type
					changed |= inferMacroBody(matchedSection, callBindings, context);
					for (Section *child : matchedSection->children) {
						for (CodeLine *line : child->codeLines) {
							if (line->expression && line->expression->type.isDeduced())
								expr->type = line->expression->type;
						}
					}
				} else {
					// Non-macro function: infer body per-instantiation
					// Build argTypes in nodesPassed order (must match codegen's paramBindings order)
					std::vector<Type> argTypes;
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
						changed |= inferMacroBody(matchedSection, callBindings, context);
						context.currentInstantiation = savedInst;
						inst.inferring = false;
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

// Reset non-literal expression types in a section and all its children.
// Macro body expression nodes are shared across all callers. Without resetting,
// if a previous caller set the body type to (e.g.) f64, and the current caller
// has an undeduced operand, the arithmetic guard skips the update and the stale
// f64 type is read as if it were the current call's result.
static void resetExpressionTypes(Expression *expr) {
	if (!expr)
		return;
	if (expr->kind != Expression::Kind::Literal)
		expr->type = {};
	for (Expression *arg : expr->arguments)
		resetExpressionTypes(arg);
}

static void resetSectionTypes(Section *section) {
	for (CodeLine *line : section->codeLines)
		if (line->expression)
			resetExpressionTypes(line->expression);
	for (Section *child : section->children)
		resetSectionTypes(child);
}

static bool
inferMacroBody(Section *section, const std::unordered_map<std::string, Expression *> &bindings, ParseContext &context) {
	// Only macros need resetting — non-macro functions are inferred per-
	// instantiation and their body types must persist across iterations.
	if (section->isMacro)
		resetSectionTypes(section);

	bool changed = false;
	for (CodeLine *line : section->codeLines) {
		if (line->expression)
			changed |= inferExpressionType(line->expression, context, bindings);
	}
	for (Section *child : section->children)
		changed |= inferMacroBody(child, bindings, context);
	return changed;
}

// Default a Numeric expression to a sized Integer type.
// For literals, check if the value fits in i32; otherwise use i64.
// For non-literal Numeric expressions, default to i32.
static void defaultNumericExpressions(Expression *expr) {
	if (!expr)
		return;
	if (expr->type.kind == Type::Kind::Numeric) {
		int size = 4; // default to i32
		if (expr->kind == Expression::Kind::Literal) {
			if (auto *intVal = std::get_if<int64_t>(&expr->literalValue)) {
				if (*intVal < INT32_MIN || *intVal > INT32_MAX)
					size = 8;
			}
		}
		expr->type = {Type::Kind::Integer, size};
	}
	for (Expression *arg : expr->arguments)
		defaultNumericExpressions(arg);
}

static void defaultNumericTypes(Section *section) {
	for (auto &[name, var] : section->variables) {
		if (var->type.kind == Type::Kind::Numeric)
			var->type = {Type::Kind::Integer, 4}; // default to i32
	}
	// Default Numeric→Integer(4) in class instantiation field types
	if (section->type == SectionType::Class) {
		auto *classSec = static_cast<ClassSection *>(section);
		for (ClassInstantiation &inst : classSec->classDefinition->instantiations) {
			for (Type &ft : inst.fieldTypes) {
				if (ft.kind == Type::Kind::Numeric)
					ft = {Type::Kind::Integer, 4};
			}
		}
	}
	// Default Numeric→Integer(4) in instantiation map keys
	if (!section->instantiations.empty()) {
		std::map<std::vector<Type>, Instantiation> updated;
		for (auto &[argTypes, inst] : section->instantiations) {
			std::vector<Type> defaultedTypes = argTypes;
			for (Type &t : defaultedTypes) {
				if (t.kind == Type::Kind::Numeric)
					t = {Type::Kind::Integer, 4};
			}
			if (inst.returnType.kind == Type::Kind::Numeric)
				inst.returnType = {Type::Kind::Integer, 4};
			updated[defaultedTypes] = std::move(inst);
		}
		section->instantiations = std::move(updated);
	}
	for (Section *child : section->children)
		defaultNumericTypes(child);
}

// Validate types after inference — check for type errors
static bool validateExpressionTypes(Expression *expr, ParseContext &context) {
	if (!expr)
		return true;

	bool valid = true;
	for (Expression *arg : expr->arguments)
		valid &= validateExpressionTypes(arg, context);

	if (expr->kind == Expression::Kind::IntrinsicCall) {
		if (isArithmeticOperator(expr->intrinsicName)) {
			if (expr->arguments.size() >= 3) {
				Type leftType = expr->arguments[1]->type;
				Type rightType = expr->arguments[2]->type;
				// Pointer arithmetic (ptr + int, ptr - int) is valid
				bool ptrArith =
					isPointerArithmeticOperator(expr->intrinsicName) && (leftType.isPointer() || rightType.isPointer());
				if (!ptrArith && leftType.isDeduced() && !leftType.isNumeric()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Cannot use " + leftType.toString() + " in arithmetic (expected a numeric type)",
						expr->arguments[1]->range
					));
					valid = false;
				}
				if (!ptrArith && rightType.isDeduced() && !rightType.isNumeric()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Cannot use " + rightType.toString() + " in arithmetic (expected a numeric type)",
						expr->arguments[2]->range
					));
					valid = false;
				}
			}
		} else if (isComparisonOperator(expr->intrinsicName)) {
			if (expr->arguments.size() >= 3) {
				Type leftType = expr->arguments[1]->type;
				Type rightType = expr->arguments[2]->type;
				if (leftType.isDeduced() && rightType.isDeduced() && !leftType.isNumeric() && !rightType.isNumeric() &&
					leftType != rightType) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error, "Cannot compare " + leftType.toString() + " with " + rightType.toString(),
						expr->range
					));
					valid = false;
				}
			}
		} else if (expr->intrinsicName == "negate") {
			if (expr->arguments.size() >= 2) {
				Type operandType = expr->arguments[1]->type;
				if (operandType.isDeduced() && !operandType.isNumeric()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error, "Cannot negate " + operandType.toString() + " (expected a numeric type)",
						expr->arguments[1]->range
					));
					valid = false;
				}
			}
		}
	}

	return valid;
}

bool inferTypes(ParseContext &context) {
	// Type inference uses fixed-point iteration: types flow through expressions
	// until no more changes occur. 64 iterations handles deeply nested expressions
	// with complex type dependencies (macros, pattern calls, arithmetic promotion).
	for (int iteration = 0; iteration < 64; iteration++) {
		bool changed = false;

		for (CodeLine *line : context.codeLines) {
			if (line->expression) {
				changed |= inferExpressionType(line->expression, context);
			}
		}

		if (!changed)
			break;
	}

	// Default remaining Numeric types to sized Integer
	for (CodeLine *line : context.codeLines) {
		if (line->expression)
			defaultNumericExpressions(line->expression);
	}
	defaultNumericTypes(context.mainSection);

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

	// Validate expression types
	for (CodeLine *line : context.codeLines) {
		if (line->expression)
			valid &= validateExpressionTypes(line->expression, context);
	}

	return valid;
}
