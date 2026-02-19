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
static DataType resolveTypeThroughBindings(Expression *expr, const std::unordered_map<std::string, Expression *> &bindings) {
	Expression *resolved = resolveThroughBindings(expr, bindings);
	return resolved ? resolved->type : DataType{};
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

	DataType oldType = expr->type;

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (std::holds_alternative<int64_t>(expr->literalValue)) {
			expr->type = {DataType::Kind::Numeric};
		} else if (std::holds_alternative<double>(expr->literalValue)) {
			expr->type = {DataType::Kind::Float, 8}; // C++ double = f64
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {DataType::Kind::Integer, 1, 1};
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
						DataType result = isPointerArithmeticOperator(expr->intrinsicName)
											  ? DataType::promoteArithmetic(leftType, rightType)
											  : DataType::promote(leftType, rightType);
						if (!result.isDeduced()) {
							context.diagnostics.push_back(Diagnostic(
								Diagnostic::Level::Error, "Cannot apply '" + expr->intrinsicName + "' to non-numeric operands",
								expr->range
							));
							return false;
						}
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
						if (var && var->type.canRefineTo(valType)) {
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
					expr->type = {DataType::Kind::Integer, 8};
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
					std::string retTypeStr;
					if (auto *str = std::get_if<std::string>(&expr->arguments[3]->literalValue))
						retTypeStr = *str;
					if (!retTypeStr.empty())
						expr->type = DataType::fromString(retTypeStr);
				} else if (expr->intrinsicName == "cast") {
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[2], macroBindings);
					if (typeArgType.kind == DataType::Kind::TypeReference) {
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
						DataType::Kind refKind = DataType::Kind::Undeduced;
						int byteSize = 0;
						int ptrDepth = 0;
						if (kindStr == "int") {
							refKind = DataType::Kind::Integer;
							byteSize = 4; // default
						} else if (kindStr == "float") {
							refKind = DataType::Kind::Float;
							byteSize = 8; // default
						} else if (kindStr == "bool") {
							refKind = DataType::Kind::Bool;
						} else if (kindStr == "void") {
							refKind = DataType::Kind::Void;
						} else if (kindStr == "string") {
							// string = pointer to byte (i8*)
							refKind = DataType::Kind::Integer;
							byteSize = 1;
							ptrDepth = 1;
						}
						// Override byte size if bits argument provided
						if (expr->arguments.size() >= 3) {
							Expression *bitsExpr = resolveThroughBindings(expr->arguments[2], macroBindings);
							if (auto *bits = std::get_if<int64_t>(&bitsExpr->literalValue))
								byteSize = *bits / 8;
						}
						expr->type = {DataType::Kind::TypeReference, byteSize, ptrDepth, nullptr, -1, refKind};
					}
				} else if (expr->intrinsicName == "add pointer depth") {
					DataType typeArgType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (typeArgType.kind == DataType::Kind::TypeReference) {
						expr->type = typeArgType;
						expr->type.pointerDepth++;
					}
				} else if (expr->intrinsicName == "construct") {
					DataType typeRefType = resolveTypeThroughBindings(expr->arguments[1], macroBindings);
					if (typeRefType.kind == DataType::Kind::TypeReference && typeRefType.classDefinition) {
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
					expr->type = {DataType::Kind::TypeReference, 0, 0, classSec->classDefinition};
				} else if (matchedSection->type == SectionType::Effect) {
					// Effects: infer body, result type is Void
					// Guard against infinite recursion (e.g., `print msg` calling `print msg as a string`
					// which re-matches `print msg` when types are still undeduced)
					if (!matchedSection->inferring) {
						matchedSection->inferring = true;
						changed |= inferMacroBody(matchedSection, callBindings, context);
						matchedSection->inferring = false;
					}
					expr->type = {DataType::Kind::Void};
				} else if (matchedSection->isMacro) {
					// Code replacement: infer body, type = replacement expression type
					if (!matchedSection->inferring) {
						matchedSection->inferring = true;
						changed |= inferMacroBody(matchedSection, callBindings, context);
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
	if (expr->type.kind == DataType::Kind::Numeric) {
		int size = 4; // default to i32
		if (expr->kind == Expression::Kind::Literal) {
			if (auto *intVal = std::get_if<int64_t>(&expr->literalValue)) {
				if (*intVal < INT32_MIN || *intVal > INT32_MAX)
					size = 8;
			}
		}
		expr->type = {DataType::Kind::Integer, size};
	}
	for (Expression *arg : expr->arguments)
		defaultNumericExpressions(arg);
}

static void defaultNumericTypes(Section *section) {
	for (auto &[name, var] : section->variables) {
		if (var->type.kind == DataType::Kind::Numeric)
			var->type = {DataType::Kind::Integer, 4}; // default to i32
	}
	// Default Numeric→Integer(4) in class instantiation field types
	if (section->type == SectionType::Class) {
		auto *classSec = static_cast<ClassSection *>(section);
		for (ClassInstantiation &inst : classSec->classDefinition->instantiations) {
			for (DataType &ft : inst.fieldTypes) {
				if (ft.kind == DataType::Kind::Numeric)
					ft = {DataType::Kind::Integer, 4};
			}
		}
	}
	// Default Numeric→Integer(4) in instantiation map keys
	if (!section->instantiations.empty()) {
		std::map<std::vector<DataType>, Instantiation> updated;
		for (auto &[argTypes, inst] : section->instantiations) {
			std::vector<DataType> defaultedTypes = argTypes;
			for (DataType &t : defaultedTypes) {
				if (t.kind == DataType::Kind::Numeric)
					t = {DataType::Kind::Integer, 4};
			}
			if (inst.returnType.kind == DataType::Kind::Numeric)
				inst.returnType = {DataType::Kind::Integer, 4};
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
				DataType leftType = expr->arguments[1]->type;
				DataType rightType = expr->arguments[2]->type;
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
				DataType leftType = expr->arguments[1]->type;
				DataType rightType = expr->arguments[2]->type;
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
				DataType operandType = expr->arguments[1]->type;
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
