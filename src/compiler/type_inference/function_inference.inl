#pragma once

#include "const_evaluation.inl"
#include "type_resolution.inl"

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

#include "variable_flow.inl"

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
			if (!inferFunction(arg, context, false, macroBindings))
				return;
			expr->arguments[i] = arg;
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
