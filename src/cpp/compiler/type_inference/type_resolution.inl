#pragma once

#include "compiler.h"
#include "const_evaluation.inl"
#include <limits>

static std::string encodeDataTypeForCacheKey(const DataType &type);
static std::string encodeCompileTimeValueForCacheKey(const CompileTimeValue &value);

static std::optional<DataType> parseNumericTokenType(std::string_view token, bool emitSPIRV) {
	if (token.empty())
		return std::nullopt;
	bool sawDigit = false;
	bool sawDot = false;
	for (char c : token) {
		if (c >= '0' && c <= '9') {
			sawDigit = true;
			continue;
		}
		if (c == '.') {
			if (sawDot)
				return std::nullopt;
			sawDot = true;
			continue;
		}
		return std::nullopt;
	}
	if (!sawDigit)
		return std::nullopt;
	if (sawDot)
		return defaultFloatType(emitSPIRV);
	return DataType{DataType::Kind::Int, 4};
}

static Expression *prepareCompileTimeTypeReferenceExpression(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &effectiveBindingFrameStack,
	InferenceContext *inferenceContext
) {
	Expression *resolved = resolveThroughBindingsDeep(expr, bindingFrameStack, effectiveBindingFrameStack, inferenceContext);
	if (!resolved)
		return nullptr;
	if (!expandPendingTypeReferenceExpression(resolved, resolved->range.line ? resolved->range.line->section : nullptr))
		return resolved;
	BindingFrameStack reboundBindingFrameStack;
	Expression *rebound =
		resolveThroughBindingsDeep(resolved, effectiveBindingFrameStack, reboundBindingFrameStack, inferenceContext);
	if (rebound) {
		effectiveBindingFrameStack = std::move(reboundBindingFrameStack);
		return rebound;
	}
	return resolved;
}

static Expression *lookupInferenceFlexExpansion(InferenceContext *inferenceContext, Expression *expression);
static CompileTimeValue lookupCompileTimeExpressionValue(Expression *expression, InferenceContext *inferenceContext);

static CompileTimeValue resolveStoredCompileTimeValue(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext
);
static bool resolveStoredCompileTimeInteger(
	Expression *expression, const BindingFrameStack &bindingFrameStack, int &outValue, InferenceContext *inferenceContext
);
static bool resolveCompileTimeTypeReference(
	ParseContext &parseContext, Expression *expr, const BindingFrameStack &bindingFrameStack, DataType &outTypeRef,
	InferenceContext *inferenceContext, const std::vector<DataType> *constructionArgumentTypes = nullptr
);

static bool isConcreteTypeReferenceValue(const DataType &type) {
	return type.kind == DataType::Kind::Type && type.referencedKind != DataType::Kind::Type &&
		   type.referencedKind != DataType::Kind::Unresolved;
}

static bool readInferredTypeReferenceValue(Expression *expression, InferenceContext *inferenceContext, DataType &outTypeRef) {
	CompileTimeValue expressionValue = lookupCompileTimeExpressionValue(expression, inferenceContext);
	if (auto *compileTimeTypeRef = std::get_if<TypeReferenceValue>(&expressionValue)) {
		if (isConcreteTypeReferenceValue(compileTimeTypeRef->type)) {
			outTypeRef = compileTimeTypeRef->type;
			return true;
		}
	}
	if (expression && isConcreteTypeReferenceValue(expression->type)) {
		outTypeRef = expression->type;
		return true;
	}
	return false;
}

static std::unordered_map<VariableReference *, CompileTimeValue>
snapshotKnownConstantsForClassInstantiation(InferenceContext *inferenceContext);
static void restoreKnownConstantsForClassInstantiation(
	InferenceContext *inferenceContext, std::unordered_map<VariableReference *, CompileTimeValue> savedKnownConstants
);
static void
seedKnownConstantsForClassInstantiation(const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext);

static Expression *resolveCompileTimeSelectBranch(
	Expression *selectExpr, ParseContext &parseContext, const BindingFrameStack &bindingFrameStack,
	InferenceContext *inferenceContext = nullptr
) {
	(void)parseContext;
	Expression *conditionExpr = selectExpr->arguments[1];
	if (inferenceContext) {
		Expression *activeConditionExpr = conditionExpr;
		if (!inferExpression(activeConditionExpr, *inferenceContext, false, bindingFrameStack, false))
			return nullptr;
		selectExpr->arguments[1] = activeConditionExpr;
		conditionExpr = activeConditionExpr;
	}
	if (!conditionExpr)
		crashCompilerBug("compile-time select branch resolution encountered null condition expression");
	CompileTimeValue conditionValue = resolveStoredCompileTimeValue(conditionExpr, bindingFrameStack, inferenceContext);
	auto *condition = std::get_if<bool>(&conditionValue);
	if (!condition)
		return nullptr;
	return selectExpr->arguments[*condition ? 2 : 3];
}

static BindingContext buildBindingContext(const BindingFrameStack &bindingFrameStack) {
	BindingContext bindingContext;
	if constexpr (sizeof(size_t) == 8)
		bindingContext.fingerprint = 1469598103934665603ull;
	else
		bindingContext.fingerprint = 2166136261u;
	bindingFrameStack.forEachFrame([&](const BindingFrame &frame) {
		for (const auto &[bindingName, expression] : frame.bindings) {
			bindingContext.bindingEntries[bindingName] = expression;
			size_t nameHash = std::hash<std::string>{}(bindingName);
			size_t expressionHash = std::hash<const Expression *>{}(expression);
			size_t entryHash = nameHash;
			entryHash ^= expressionHash + 0x9e3779b9 + (entryHash << 6) + (entryHash >> 2);
			bindingContext.fingerprint ^= entryHash;
		}
		for (const auto &[parameterDefinition, expression] : frame.parameterBindings) {
			bindingContext.parameterBindingEntries[parameterDefinition] = expression;
			size_t definitionHash = std::hash<const VariableReference *>{}(parameterDefinition);
			size_t expressionHash = std::hash<const Expression *>{}(expression);
			size_t entryHash = definitionHash;
			entryHash ^= expressionHash + 0x9e3779b9 + (entryHash << 6) + (entryHash >> 2);
			bindingContext.fingerprint ^= entryHash;
		}
	});
	return bindingContext;
}

struct ActiveClassInstantiation {
	ParseContext *parseContext{};
	ClassDefinition *classDefinition{};
	std::string requestKey;
	int symbolicIndex = -1;
};

struct ClassInstantiationTransaction {
	ParseContext *parseContext{};
	std::unordered_map<ClassDefinition *, size_t> originalInstantiationSizes;
	std::vector<std::pair<ClassDefinition *, std::string>> insertedRequestKeys;
	bool failed = false;
};

static thread_local std::vector<ActiveClassInstantiation> activeClassInstantiations;
static thread_local std::optional<ClassInstantiationTransaction> activeClassInstantiationTransaction;
static thread_local int nextSymbolicClassInstantiationIndex = -2;
static thread_local size_t classInstantiationRollbackVersion = 0;

static std::string buildClassInstantiationRequestKey(
	const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext,
	const std::vector<DataType> *constructionArgumentTypes
) {
	std::vector<std::tuple<std::string, DataType, CompileTimeValue>> parameters;
	if (!bindingFrameStack.empty()) {
		for (const auto &[name, expression] : bindingFrameStack.topFrame().bindings) {
			DataType type = resolveKnownExpressionType(expression, bindingFrameStack, inferenceContext);
			CompileTimeValue value = resolveStoredCompileTimeValue(expression, bindingFrameStack, inferenceContext);
			parameters.push_back({name, std::move(type), std::move(value)});
		}
	}
	std::sort(parameters.begin(), parameters.end(), [](const auto &left, const auto &right) {
		return std::get<0>(left) < std::get<0>(right);
	});

	std::string key;
	for (const auto &[name, type, value] : parameters) {
		key += std::to_string(name.size()) + ":" + name;
		key += "=" + encodeDataTypeForCacheKey(type);
		key += ":" + encodeCompileTimeValueForCacheKey(value) + ";";
	}
	if (constructionArgumentTypes) {
		key += "#construct";
		for (const DataType &type : *constructionArgumentTypes)
			key += ":" + encodeDataTypeForCacheKey(type);
	}
	return key;
}

static void recordClassInstantiationAppend(ClassDefinition *classDefinition) {
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "class instantiation append has no active transaction"
	);
	activeClassInstantiationTransaction->originalInstantiationSizes.try_emplace(
		classDefinition, classDefinition->instantiations.size()
	);
}

static void cacheClassInstantiationRequest(ClassDefinition *classDefinition, const std::string &requestKey, int index) {
	auto [it, inserted] = classDefinition->instantiationIndicesByRequest.emplace(requestKey, index);
	if (!inserted) {
		requireCompilerInvariant(it->second == index, "class instantiation request resolved to inconsistent indices");
		return;
	}
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "class instantiation request cache has no active transaction"
	);
	activeClassInstantiationTransaction->insertedRequestKeys.push_back({classDefinition, requestKey});
}

static void rollbackClassInstantiationTransaction() {
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "cannot roll back a missing class instantiation transaction"
	);
	for (auto it = activeClassInstantiationTransaction->insertedRequestKeys.rbegin();
		 it != activeClassInstantiationTransaction->insertedRequestKeys.rend(); ++it) {
		it->first->instantiationIndicesByRequest.erase(it->second);
	}
	for (const auto &[classDefinition, originalSize] : activeClassInstantiationTransaction->originalInstantiationSizes) {
		requireCompilerInvariant(
			classDefinition->instantiations.size() >= originalSize,
			"class instantiation transaction observed an unexpected instantiation-list shrink"
		);
		classDefinition->instantiations.resize(originalSize);
	}
	classInstantiationRollbackVersion++;
}

static bool
replaceSymbolicClassInstantiation(DataType &type, ClassDefinition *classDefinition, int symbolicIndex, int concreteIndex) {
	bool changed = false;
	if (type.classDefinition == classDefinition && type.classInstIndex == symbolicIndex) {
		type.classInstIndex = concreteIndex;
		changed = true;
	}
	if (type.arrayElementType)
		changed =
			replaceSymbolicClassInstantiation(*type.arrayElementType, classDefinition, symbolicIndex, concreteIndex) || changed;
	return changed;
}

static void resolveStoredSymbolicClassInstantiation(ClassDefinition *classDefinition, int symbolicIndex, int concreteIndex) {
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "symbolic class instantiation has no active transaction"
	);
	for (const auto &[mutatedClassDefinition, originalSize] : activeClassInstantiationTransaction->originalInstantiationSizes) {
		for (size_t index = originalSize; index < mutatedClassDefinition->instantiations.size(); index++) {
			for (DataType &fieldType : mutatedClassDefinition->instantiations[index].fieldTypes)
				replaceSymbolicClassInstantiation(fieldType, classDefinition, symbolicIndex, concreteIndex);
		}
	}
}

struct ScopedActiveClassInstantiation {
	bool ownsTransaction;
	bool completed = false;

	ScopedActiveClassInstantiation(
		ParseContext &parseContext, ClassDefinition *classDefinition, std::string requestKey, int symbolicIndex
	)
		: ownsTransaction(activeClassInstantiations.empty()) {
		if (ownsTransaction) {
			requireCompilerInvariant(
				!activeClassInstantiationTransaction.has_value(), "class instantiation transaction leaked between resolutions"
			);
			activeClassInstantiationTransaction = ClassInstantiationTransaction{&parseContext, {}, {}, false};
		} else {
			requireCompilerInvariant(
				activeClassInstantiationTransaction && activeClassInstantiationTransaction->parseContext == &parseContext,
				"nested class instantiation crossed parse contexts"
			);
		}
		activeClassInstantiations.push_back({&parseContext, classDefinition, std::move(requestKey), symbolicIndex});
	}

	~ScopedActiveClassInstantiation() {
		requireCompilerInvariant(!activeClassInstantiations.empty(), "active class instantiation stack underflow");
		activeClassInstantiations.pop_back();
		if (!completed)
			activeClassInstantiationTransaction->failed = true;
		if (!ownsTransaction)
			return;
		requireCompilerInvariant(activeClassInstantiations.empty(), "outer class instantiation did not unwind last");
		if (activeClassInstantiationTransaction->failed)
			rollbackClassInstantiationTransaction();
		activeClassInstantiationTransaction.reset();
		nextSymbolicClassInstantiationIndex = -2;
	}
};

static TypeResolutionKey buildTypeResolutionKey(const Expression *expression, const BindingFrameStack &bindingFrameStack) {
	TypeResolutionKey typeResolutionKey;
	typeResolutionKey.expression = expression;
	typeResolutionKey.bindingContext = buildBindingContext(bindingFrameStack);
	return typeResolutionKey;
}

static bool tryResolveCastResultType(const DataType &fromType, const DataType &typeArgType, DataType &outType);

static DataType
resolveKnownExpressionType(Expression *expr, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext) {
	if (expr && expr->kind == Expression::Kind::PatternCall && expr->type.isDeduced() && !bindingFrameStack.hasBindings()) {
		return expr->type;
	}
	Expression *directResolved = resolveThroughBindings(expr, bindingFrameStack);
	if (directResolved && directResolved != expr && directResolved->type.isDeduced())
		return directResolved->type;
	BindingFrameStack effectiveBindingFrameStack;
	Expression *resolved = resolveThroughBindingsDeep(expr, bindingFrameStack, effectiveBindingFrameStack);
	if (!resolved)
		return {};
	bool dependsOnBindings = resolved != expr || effectiveBindingFrameStack.hasBindings();
	if (!bindingFrameStack.hasBindings() && !dependsOnBindings && resolved->type.isDeduced())
		return resolved->type;
	TypeResolutionKey typeResolutionKey = buildTypeResolutionKey(resolved, effectiveBindingFrameStack);
	if (containsActiveTypeResolutionKey(typeResolutionKey))
		return {};

	struct ActiveTypeResolutionGuard {
		TypeResolutionKey typeResolutionKey;

		explicit ActiveTypeResolutionGuard(TypeResolutionKey activeTypeResolutionKey)
			: typeResolutionKey(std::move(activeTypeResolutionKey)) {
			pushActiveTypeResolutionKey(typeResolutionKey);
		}
		~ActiveTypeResolutionGuard() { popActiveTypeResolutionKey(typeResolutionKey); }
	} activeGuard(std::move(typeResolutionKey));
	if (resolved->kind == Expression::Kind::Literal) {
		if (std::holds_alternative<double>(resolved->literalValue)) {
			double value = std::get<double>(resolved->literalValue);
			std::string_view literalText = resolved->range.subString;
			bool explicitlyFloat = literalText.find('.') != std::string_view::npos ||
								   literalText.find('e') != std::string_view::npos ||
								   literalText.find('E') != std::string_view::npos;
			if (!explicitlyFloat && std::trunc(value) == value)
				return {DataType::Kind::Int, 4};
			return defaultFloatType(activeTypeResolutionParseContext && activeTypeResolutionParseContext->options.emitSPIRV);
		}
		if (std::holds_alternative<std::string>(resolved->literalValue)) {
			DataType strType{DataType::Kind::Int, 1};
			strType.pointerDepth = 1;
			return strType;
		}
	}
	if (resolved->kind == Expression::Kind::ArrayLiteral) {
		if (resolved->arguments.empty())
			return {};
		DataType elementType = resolveKnownExpressionType(resolved->arguments[0], effectiveBindingFrameStack, inferenceContext);
		if (!elementType.isDeduced())
			return {};
		for (size_t i = 1; i < resolved->arguments.size(); i++) {
			DataType nextType =
				resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack, inferenceContext);
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
	if (resolved->kind == Expression::Kind::Variable && resolved->variable) {
		VariableReference *varRef = resolved->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		Section *sec = definition->range.line ? definition->range.line->section : nullptr;
		Variable *var = sec ? sec->findVariable(definition->name) : nullptr;
		if (!var && resolved->range.line)
			var =
				resolved->range.line->section ? resolved->range.line->section->findVariable(resolved->variable->name) : nullptr;
		if (var && var->type.isDeduced())
			return var->type;
		if (std::optional<DataType> numericTokenType = parseNumericTokenType(
				resolved->variable->name,
				activeTypeResolutionParseContext && activeTypeResolutionParseContext->options.emitSPIRV
			))
			return *numericTokenType;
	}
	if (resolved->kind == Expression::Kind::TypedPlaceholder && resolved->type.isDeduced())
		return resolved->type;
	PatternDefinition *selectedPatternDefinition =
		resolved->kind == Expression::Kind::PatternCall ? resolved->selectedPatternDefinition : nullptr;
	if (resolved->kind == Expression::Kind::IntrinsicCall) {
		const IntrinsicInfo *info = findIntrinsic(resolved->intrinsicName);
		IntrinsicKind kind = intrinsicKind(resolved->intrinsicName);
		if (info) {
			switch (info->returnKind) {
			case IntrinsicReturnKind::SameAsArgs:
				if (resolved->arguments.size() == 2)
					return resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
				if (resolved->arguments.size() > 2) {
					DataType leftType =
						resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
					DataType rightType =
						resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
					DataType result;
					if (DataType::promoteArithmetic(leftType, rightType, result))
						return result;
				}
				return {};
			case IntrinsicReturnKind::SameAsInts:
				if (resolved->arguments.size() == 2) {
					DataType valueType =
						resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
					if (valueType.isInteger())
						return valueType;
					return {};
				}
				if (resolved->arguments.size() > 2) {
					DataType leftType =
						resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
					DataType rightType =
						resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
					DataType result;
					if (DataType::promoteBitwise(leftType, rightType, result))
						return result;
				}
				return {};
			case IntrinsicReturnKind::Bool:
				if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
					DataType leftType =
						resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
					DataType rightType =
						resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
					if (leftType.kind == DataType::Kind::Bool && rightType.kind == DataType::Kind::Bool) {
						return {DataType::Kind::Bool};
					}
					return {};
				}
				if (kind == IntrinsicKind::Not) {
					DataType valueType =
						resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
					if (valueType.kind == DataType::Kind::Bool)
						return {DataType::Kind::Bool};
					return {};
				}
				if (resolved->arguments.size() > 2) {
					DataType leftType =
						resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
					DataType rightType =
						resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
					DataType promoted;
					bool pointerEquality = (kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) &&
										   leftType.isPointer() && rightType.isPointer() && leftType == rightType;
					if (pointerEquality || DataType::promoteArithmetic(leftType, rightType, promoted))
						return {DataType::Kind::Bool};
				}
				return {};
			case IntrinsicReturnKind::Void:
				if (kind == IntrinsicKind::If || kind == IntrinsicKind::ElseIf || kind == IntrinsicKind::LoopWhile) {
					if (resolved->arguments.size() <= 1)
						return {};
					DataType conditionType =
						resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
					if (conditionType.kind != DataType::Kind::Bool)
						return {};
				}
				return {DataType::Kind::Void};
			case IntrinsicReturnKind::Float:
				return {DataType::Kind::Float, 4};
			case IntrinsicReturnKind::Custom:
				break;
			}
		}
		if (kind == IntrinsicKind::Property) {
			DataType instType =
				resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
			if (instType.isPointer() && instType.kind == DataType::Kind::Class)
				instType = instType.dereferenced();
			std::string fieldName;
			if (activeTypeResolutionParseContext) {
				CompileTimeValue propertyValue =
					::resolveStoredCompileTimeValue(resolved->arguments[2], effectiveBindingFrameStack);
				if (const auto *propertyName = std::get_if<std::string>(&propertyValue))
					fieldName = *propertyName;
			}
			if (fieldName.empty()) {
				Expression *propExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindingFrameStack);
				fieldName = extractFieldName(propExpr);
			}
			DataType builtInPropertyType = resolveBuiltInPropertyType(instType, fieldName);
			if (builtInPropertyType.isDeduced())
				return builtInPropertyType;
			if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
				for (size_t i = 0; i < instType.classDefinition->fields.size(); i++) {
					if (instType.classDefinition->fields[i].name == fieldName)
						return instType.classDefinition->instantiations[instType.classInstIndex].fieldTypes[i];
				}
			}
		} else if (kind == IntrinsicKind::Dereference) {
			DataType ptrType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
			if (ptrType.isDeduced() && ptrType.isPointer())
				return ptrType.dereferenced();
		} else if (kind == IntrinsicKind::AddressOf) {
			DataType valueType =
				resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
			if (valueType.isDeduced())
				return valueType.pointed();
		} else if (kind == IntrinsicKind::Call) {
			if (resolved->arguments.size() <= 3)
				return {};
			DataType retTypeRef =
				resolveKnownExpressionType(resolved->arguments[3], effectiveBindingFrameStack, inferenceContext);
			if (retTypeRef.kind != DataType::Kind::Type || retTypeRef.referencedKind == DataType::Kind::Type ||
				retTypeRef.referencedKind == DataType::Kind::Unresolved)
				return {};
			for (size_t i = 4; i < resolved->arguments.size(); i++) {
				DataType argType =
					resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack, inferenceContext);
				if (argType.kind == DataType::Kind::Type)
					return {};
			}
			return retTypeRef.toReferencedType();
		} else if (kind == IntrinsicKind::Function) {
			return {DataType::Kind::Int, 1, 1};
		} else if (kind == IntrinsicKind::Return) {
			return {DataType::Kind::Void};
		} else if (kind == IntrinsicKind::Cast) {
			DataType valueType =
				resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
			DataType typeArgType =
				resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
			DataType resultType;
			if (tryResolveCastResultType(valueType, typeArgType, resultType))
				return resultType;
		} else if (kind == IntrinsicKind::Type) {
			if (!activeTypeResolutionParseContext)
				return {};
			std::string kindStr;
			CompileTimeValue kindValue = ::resolveStoredCompileTimeValue(resolved->arguments[1], effectiveBindingFrameStack);
			if (auto *str = std::get_if<std::string>(&kindValue))
				kindStr = *str;
			if (!kindStr.empty()) {
				std::optional<int> numericByteSize;
				if (resolved->arguments.size() > 2) {
					int bitCount = 0;
					if (!::resolveStoredCompileTimeInteger(resolved->arguments[2], effectiveBindingFrameStack, bitCount) ||
						bitCount <= 0 || bitCount % 8 != 0)
						return {};
					numericByteSize = bitCount / 8;
				}
				std::optional<DataType> typeRef = makeBuiltinTypeReference(
					kindStr, activeTypeResolutionParseContext && activeTypeResolutionParseContext->options.emitSPIRV,
					numericByteSize
				);
				return typeRef.value_or(DataType{});
			}
		} else if (kind == IntrinsicKind::Fix) {
			CompileTimeValue sourceValue = ::resolveStoredCompileTimeValue(resolved->arguments[1], effectiveBindingFrameStack);
			if (getCompileTimeConstraintValue(sourceValue))
				return {DataType::Kind::Constraint};
		} else if (kind == IntrinsicKind::TypeOf) {
			DataType valueType =
				resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
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
		} else if (kind == IntrinsicKind::SizeOf) {
			DataType typeArgType =
				resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
			if (typeArgType.kind == DataType::Kind::Type && typeArgType.referencedKind != DataType::Kind::Type &&
				typeArgType.referencedKind != DataType::Kind::Unresolved)
				return {DataType::Kind::Int, 8};
		} else if (kind == IntrinsicKind::BuildInfo) {
			Expression *keyExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindingFrameStack);
			if (auto *key = std::get_if<std::string>(&keyExpr->literalValue))
				if (std::optional<DataType> infoType = buildInfoValueType(*key))
					return *infoType;
		} else if (kind == IntrinsicKind::TargetIs) {
			Expression *targetExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindingFrameStack);
			if (std::holds_alternative<std::string>(targetExpr->literalValue))
				return {DataType::Kind::Bool};
		} else if (kind == IntrinsicKind::ShaderStageIs) {
			Expression *shaderStageExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindingFrameStack);
			if (std::holds_alternative<std::string>(shaderStageExpr->literalValue))
				return {DataType::Kind::Bool};
		} else if (kind == IntrinsicKind::Select && activeTypeResolutionParseContext) {
			Expression *selectedBranch =
				resolveCompileTimeSelectBranch(resolved, *activeTypeResolutionParseContext, effectiveBindingFrameStack);
			if (selectedBranch)
				return resolveKnownExpressionType(selectedBranch, effectiveBindingFrameStack, inferenceContext);
		} else if (kind == IntrinsicKind::Array) {
			Expression *sizeExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindingFrameStack);
			if (auto *size = std::get_if<double>(&sizeExpr->literalValue)) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				typeRef.referencedKind = DataType::Kind::Array;
				typeRef.arraySize = static_cast<int>(*size);
				if (resolved->arguments.size() > 2) {
					DataType elemTypeRef =
						resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
					if (elemTypeRef.kind == DataType::Kind::Constraint)
						return {DataType::Kind::Constraint};
					if (elemTypeRef.kind == DataType::Kind::Type)
						typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
				}
				return typeRef;
			}
		} else if (kind == IntrinsicKind::Vector) {
			Expression *sizeExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindingFrameStack);
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
			if (resolved->arguments.size() > 2) {
				DataType elemTypeRef;
				DataType elementExpressionType =
					resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
				if (elementExpressionType.kind == DataType::Kind::Constraint)
					return {DataType::Kind::Constraint};
				if (elementExpressionType.kind == DataType::Kind::Type)
					elemTypeRef = elementExpressionType.toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (kind == IntrinsicKind::Matrix) {
			Expression *rowsExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindingFrameStack);
			Expression *columnsExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindingFrameStack);
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
			if (resolved->arguments.size() > 3) {
				DataType elemTypeRef;
				DataType elementExpressionType =
					resolveKnownExpressionType(resolved->arguments[3], effectiveBindingFrameStack, inferenceContext);
				if (elementExpressionType.kind == DataType::Kind::Constraint)
					return {DataType::Kind::Constraint};
				if (elementExpressionType.kind == DataType::Kind::Type)
					elemTypeRef = elementExpressionType.toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (kind == IntrinsicKind::AddPointerDepth) {
			DataType typeArgType =
				resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
			if (typeArgType.kind == DataType::Kind::Constraint) {
				CompileTimeValue sourceValue =
					::resolveStoredCompileTimeValue(resolved->arguments[1], effectiveBindingFrameStack);
				if (getCompileTimeConstraintValue(sourceValue))
					return {DataType::Kind::Constraint};
			}
			if (typeArgType.kind == DataType::Kind::Type && typeArgType.referencedKind != DataType::Kind::Type &&
				typeArgType.referencedKind != DataType::Kind::Unresolved) {
				typeArgType.pointerDepth++;
				return typeArgType;
			}
		} else if (kind == IntrinsicKind::Construct) {
			DataType typeRefType =
				resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext);
			if (typeRefType.kind == DataType::Kind::Type && typeRefType.referencedKind == DataType::Kind::Array) {
				DataType arrayType = typeRefType.toReferencedType();
				if (arrayType.arraySize == static_cast<int>(resolved->arguments.size()) - 2) {
					DataType elementType =
						arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
					bool allDeduced = true;
					for (size_t i = 2; i < resolved->arguments.size(); i++) {
						DataType argType =
							resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack, inferenceContext);
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
					DataType argumentType =
						resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack, inferenceContext);
					if (!argumentType.isDeduced()) {
						allArgumentsDeduced = false;
						break;
					}
					argumentTypes.push_back(argumentType);
				}

				DataType instantiatedTypeRef;
				if (allArgumentsDeduced &&
					instantiateClassFromArgumentTypes(
						typeRefType.classDefinition, argumentTypes, instantiatedTypeRef, typeRefType.classInstIndex
					)) {
					return instantiatedTypeRef.toReferencedType();
				}

				DataType targetType = typeRefType.toReferencedType();
				if (resolved->arguments.size() == targetType.classDefinition->fields.size() + 2 &&
					targetType.classInstIndex >= 0) {
					const auto &fieldTypes = targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
					bool allCompatible = argumentTypes.size() == fieldTypes.size();
					for (size_t i = 0; allCompatible && i < fieldTypes.size(); i++) {
						if (!DataType::supportsRuntimeConversion(argumentTypes[i], fieldTypes[i]))
							allCompatible = false;
					}
					if (allCompatible)
						return targetType;
				}
			} else if (typeRefType.kind == DataType::Kind::Type && resolved->arguments.size() == 3) {
				DataType targetType = typeRefType.toReferencedType();
				DataType valueType =
					resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext);
				if (valueType.isDeduced())
					return targetType;
			}
		}
	}
	BindingFrame innerBindings;
	Expression *bodyExpr = resolved->inferredFlexExpansion;
	if (bodyExpr) {
		if (!selectedPatternDefinition || !selectedPatternDefinition->section || !selectedPatternDefinition->section->isFlex)
			crashCompilerBug("inferred flex expansion has no selected flex definition");
		collectPatternCallBindings(resolved, selectedPatternDefinition, innerBindings);
	}
	if (bodyExpr) {
		BindingFrameStack mergedBindingFrameStack = bindingFrameStack;
		materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
		pushBindingScope(mergedBindingFrameStack, std::move(innerBindings));
		return resolveKnownExpressionType(bodyExpr, mergedBindingFrameStack, inferenceContext);
	}
	return resolved->type;
}

static std::string typeToUserName(const DataType &type, ParseContext &parseContext) {
	if (type.pointerDepth == 0) {
		if (type.kind == DataType::Kind::Int && type.numericSize > 0)
			return "a " + std::to_string(type.numericSize * 8) + " bit integer";
		if (type.kind == DataType::Kind::Float && type.numericSize > 0)
			return "a " + std::to_string(type.numericSize * 8) + " bit float";
		if (type.kind == DataType::Kind::Bool)
			return "a boolean";
		if (type.kind == DataType::Kind::Void)
			return "nothing";
		if (type.kind == DataType::Kind::Constraint)
			return "a constraint";
	}
	auto it = parseContext.typeAliasNames.find(type);
	if (it != parseContext.typeAliasNames.end())
		return it->second;
	return type.toString();
}

static DataType toTypeReference(const DataType &valueType) {
	if (!valueType.isDeduced())
		return {};
	if (valueType.kind == DataType::Kind::Type)
		return valueType;
	DataType concreteValueType = valueType;
	DataType typeReference;
	typeReference.kind = DataType::Kind::Type;
	typeReference.referencedKind = concreteValueType.kind;
	typeReference.numericSize = concreteValueType.numericSize;
	typeReference.pointerDepth = concreteValueType.pointerDepth;
	typeReference.classDefinition = concreteValueType.classDefinition;
	typeReference.classInstIndex = concreteValueType.classInstIndex;
	typeReference.arraySize = concreteValueType.arraySize;
	typeReference.matrixRowCount = concreteValueType.matrixRowCount;
	if (concreteValueType.arrayElementType)
		typeReference.arrayElementType = std::make_shared<DataType>(*concreteValueType.arrayElementType);
	return typeReference;
}

static bool resolveCompileTimeTypeReference(
	ParseContext &parseContext, Expression *expr, const BindingFrameStack &bindingFrameStack, DataType &outTypeRef,
	InferenceContext *inferenceContext, const std::vector<DataType> *constructionArgumentTypes
) {
	if (!expr)
		return false;
	CompileTimeValue directValue = lookupCompileTimeExpressionValue(expr, inferenceContext);
	if (auto *compileTimeTypeRef = std::get_if<TypeReferenceValue>(&directValue)) {
		bool requiresClassCompletion = constructionArgumentTypes &&
									   compileTimeTypeRef->type.referencedKind == DataType::Kind::Class &&
									   compileTimeTypeRef->type.classInstIndex < 0;
		if (isConcreteTypeReferenceValue(compileTimeTypeRef->type) && !requiresClassCompletion) {
			outTypeRef = compileTimeTypeRef->type;
			return true;
		}
	}
	bool expressionTypeRequiresClassCompletion =
		constructionArgumentTypes && expr->type.referencedKind == DataType::Kind::Class && expr->type.classInstIndex < 0;
	if (isConcreteTypeReferenceValue(expr->type) && !expressionTypeRequiresClassCompletion) {
		outTypeRef = expr->type;
		return true;
	}
	BindingFrameStack effectiveBindingFrameStack;
	Expression *resolved =
		prepareCompileTimeTypeReferenceExpression(expr, bindingFrameStack, effectiveBindingFrameStack, inferenceContext);
	if (!resolved)
		return false;
	CompileTimeValue resolvedValue = lookupCompileTimeExpressionValue(resolved, inferenceContext);
	if (auto *compileTimeTypeRef = std::get_if<TypeReferenceValue>(&resolvedValue)) {
		bool requiresClassCompletion = constructionArgumentTypes &&
									   compileTimeTypeRef->type.referencedKind == DataType::Kind::Class &&
									   compileTimeTypeRef->type.classInstIndex < 0;
		if (isConcreteTypeReferenceValue(compileTimeTypeRef->type) && !requiresClassCompletion) {
			outTypeRef = compileTimeTypeRef->type;
			return true;
		}
	}
	bool resolvedTypeRequiresClassCompletion = constructionArgumentTypes &&
											   resolved->type.referencedKind == DataType::Kind::Class &&
											   resolved->type.classInstIndex < 0;
	if (isConcreteTypeReferenceValue(resolved->type) && !resolvedTypeRequiresClassCompletion) {
		outTypeRef = resolved->type;
		return true;
	}

	if (resolved->kind == Expression::Kind::Variable && resolved->variable)
		return false;

	if (resolved->kind == Expression::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(resolved->intrinsicName);
		if (kind == IntrinsicKind::Type) {
			DataType resolvedType = resolveKnownExpressionType(resolved, effectiveBindingFrameStack, inferenceContext);
			if (resolvedType.kind == DataType::Kind::Type) {
				outTypeRef = resolvedType;
				return true;
			}
		}
		if (kind == IntrinsicKind::AddPointerDepth) {
			DataType innerTypeRef;
			if (!resolveCompileTimeTypeReference(
					parseContext, resolved->arguments[1], effectiveBindingFrameStack, innerTypeRef, inferenceContext
				) ||
				innerTypeRef.kind != DataType::Kind::Type)
				return false;
			innerTypeRef.pointerDepth++;
			outTypeRef = innerTypeRef;
			return true;
		}
		if (kind == IntrinsicKind::Array) {
			int arraySize = 0;
			if (!resolveStoredCompileTimeInteger(
					resolved->arguments[1], effectiveBindingFrameStack, arraySize, inferenceContext
				))
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Array;
			outTypeRef.arraySize = arraySize;
			if (resolved->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, resolved->arguments[2], effectiveBindingFrameStack, elementTypeRef, inferenceContext
					) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Multiply && resolved->arguments.size() > 2) {
			DataType arrayTypeRef;
			int factor = 0;
			if (resolveCompileTimeTypeReference(
					parseContext, resolved->arguments[1], effectiveBindingFrameStack, arrayTypeRef, inferenceContext
				) &&
				arrayTypeRef.kind == DataType::Kind::Type && arrayTypeRef.referencedKind == DataType::Kind::Array &&
				resolveStoredCompileTimeInteger(resolved->arguments[2], effectiveBindingFrameStack, factor, inferenceContext) &&
				factor >= 0) {
				arrayTypeRef.arraySize *= factor;
				outTypeRef = arrayTypeRef;
				return true;
			}
			if (resolveCompileTimeTypeReference(
					parseContext, resolved->arguments[2], effectiveBindingFrameStack, arrayTypeRef, inferenceContext
				) &&
				arrayTypeRef.kind == DataType::Kind::Type && arrayTypeRef.referencedKind == DataType::Kind::Array &&
				resolveStoredCompileTimeInteger(resolved->arguments[1], effectiveBindingFrameStack, factor, inferenceContext) &&
				factor >= 0) {
				arrayTypeRef.arraySize *= factor;
				outTypeRef = arrayTypeRef;
				return true;
			}
		}
		if (kind == IntrinsicKind::Vector) {
			int vectorSize = 0;
			if (!resolveStoredCompileTimeInteger(
					resolved->arguments[1], effectiveBindingFrameStack, vectorSize, inferenceContext
				) ||
				vectorSize < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Vector;
			outTypeRef.arraySize = vectorSize;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, resolved->arguments[2], effectiveBindingFrameStack, elementTypeRef, inferenceContext
					) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Matrix) {
			int rows = 0;
			int columns = 0;
			if (!resolveStoredCompileTimeInteger(resolved->arguments[1], effectiveBindingFrameStack, rows, inferenceContext) ||
				!resolveStoredCompileTimeInteger(
					resolved->arguments[2], effectiveBindingFrameStack, columns, inferenceContext
				) ||
				rows < 1 || columns < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Matrix;
			outTypeRef.matrixRowCount = rows;
			outTypeRef.arraySize = columns;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 3) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, resolved->arguments[3], effectiveBindingFrameStack, elementTypeRef, inferenceContext
					) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Select) {
			Expression *selectedBranch =
				resolveCompileTimeSelectBranch(resolved, parseContext, effectiveBindingFrameStack, inferenceContext);
			if (selectedBranch)
				return resolveCompileTimeTypeReference(
					parseContext, selectedBranch, effectiveBindingFrameStack, outTypeRef, inferenceContext,
					constructionArgumentTypes
				);
		}
	}

	if (resolved->kind == Expression::Kind::PatternCall) {
		PatternDefinition *def = resolved->selectedPatternDefinition;
		if (!def || !def->section)
			return false;

		if (!def->section->isFlex && def->section->type == SectionType::Class) {
			BindingFrame callBindings;
			collectPatternCallBindings(resolved, def, callBindings);
			for (auto &[name, boundExpr] : callBindings.bindings) {
				Expression *resolvedExpr = resolveThroughBindings(boundExpr, effectiveBindingFrameStack);
				if (resolvedExpr)
					boundExpr = resolvedExpr;
			}
			for (auto &[parameterDefinition, boundExpr] : callBindings.parameterBindings) {
				(void)parameterDefinition;
				Expression *resolvedExpr = resolveThroughBindings(boundExpr, effectiveBindingFrameStack);
				if (resolvedExpr)
					boundExpr = resolvedExpr;
			}
			BindingFrameStack callBindingFrameStack = effectiveBindingFrameStack;
			callBindingFrameStack.pushFrame(std::move(callBindings));
			outTypeRef = instantiateBoundClassType(
				parseContext, static_cast<ClassSection *>(def->section)->classDefinition, callBindingFrameStack,
				inferenceContext, constructionArgumentTypes
			);
			return outTypeRef.kind == DataType::Kind::Type;
		}
		if (!def->section->isFlex) {
			Instantiation *selectedInstantiation = resolved->selectedInstantiation;
			if (!selectedInstantiation || !selectedInstantiation->valid || !selectedInstantiation->returnType.isDeduced())
				return false;
			DataType resolvedReturnTypeRef = toTypeReference(selectedInstantiation->returnType);
			if (resolvedReturnTypeRef.kind != DataType::Kind::Type)
				return false;
			outTypeRef = resolvedReturnTypeRef;
			return true;
		}

		BindingFrame innerBindings;
		Expression *bodyExpr = nullptr;
		Expression *inferredFlexExpansion = lookupInferenceFlexExpansion(inferenceContext, resolved);
		if (inferredFlexExpansion) {
			collectPatternCallBindings(resolved, def, innerBindings);
			bodyExpr = inferredFlexExpansion;
		}
		if (!bodyExpr)
			return false;
		BindingFrameStack callBindingFrameStack = effectiveBindingFrameStack;
		materializeFlexBindingsInCallerScope(innerBindings, callBindingFrameStack);
		callBindingFrameStack.pushFrame(std::move(innerBindings));
		return resolveCompileTimeTypeReference(
			parseContext, bodyExpr, callBindingFrameStack, outTypeRef, inferenceContext, constructionArgumentTypes
		);
	}
	return false;
}

static bool
completeBoundClassFieldType(const DataType &fieldConstraintInput, const DataType &argumentTypeInput, DataType &outFieldType) {
	DataType fieldConstraint = fieldConstraintInput;
	DataType argumentType = argumentTypeInput;
	if (!argumentType.isConcrete())
		return false;
	if (fieldConstraint.kind == DataType::Kind::Any) {
		outFieldType = argumentType;
		return true;
	}
	if (fieldConstraint.kind == DataType::Kind::Array && !fieldConstraint.isConcrete()) {
		if (argumentType.kind != DataType::Kind::Array || argumentType.arraySize != fieldConstraint.arraySize ||
			!argumentType.arrayElementType)
			return false;
		if (fieldConstraint.arrayElementType) {
			DataType completedElementType;
			if (!completeBoundClassFieldType(
					*fieldConstraint.arrayElementType, *argumentType.arrayElementType, completedElementType
				))
				return false;
			outFieldType = fieldConstraint;
			outFieldType.arrayElementType = std::make_shared<DataType>(std::move(completedElementType));
		} else {
			outFieldType = argumentType;
		}
		return outFieldType.isConcrete();
	}
	if (fieldConstraint.kind == DataType::Kind::Class && fieldConstraint.classDefinition &&
		fieldConstraint.classInstIndex < 0) {
		if (argumentType.kind != DataType::Kind::Class || argumentType.classDefinition != fieldConstraint.classDefinition)
			return false;
		outFieldType = argumentType;
		return true;
	}
	if (!fieldConstraint.isConcrete() || !DataType::supportsRuntimeConversion(argumentType, fieldConstraint))
		return false;
	outFieldType = fieldConstraint;
	return true;
}

static DataType instantiateBoundClassType(
	ParseContext &parseContext, ClassDefinition *classDef, const BindingFrameStack &bindingFrameStack,
	InferenceContext *inferenceContext, const std::vector<DataType> *constructionArgumentTypes
) {
	if (!classDef)
		return {};
	if (constructionArgumentTypes && constructionArgumentTypes->size() != classDef->fields.size())
		return {};

	std::string requestKey = buildClassInstantiationRequestKey(bindingFrameStack, inferenceContext, constructionArgumentTypes);
	auto completedRequest = classDef->instantiationIndicesByRequest.find(requestKey);
	if (completedRequest != classDef->instantiationIndicesByRequest.end()) {
		requireCompilerInvariant(
			completedRequest->second >= 0 && completedRequest->second < static_cast<int>(classDef->instantiations.size()),
			"cached class instantiation request refers to a missing instantiation"
		);
		return {DataType::Kind::Type, 0, 0, classDef, completedRequest->second, nullptr, DataType::Kind::Class};
	}
	for (auto active = activeClassInstantiations.rbegin(); active != activeClassInstantiations.rend(); ++active) {
		if (active->parseContext != &parseContext || active->classDefinition != classDef || active->requestKey != requestKey)
			continue;
		return {DataType::Kind::Type, 0, 0, classDef, active->symbolicIndex, nullptr, DataType::Kind::Class};
	}

	int symbolicIndex = nextSymbolicClassInstantiationIndex--;
	ScopedActiveClassInstantiation activeInstantiation(parseContext, classDef, requestKey, symbolicIndex);

	struct ScopedKnownConstantsOverride {
		InferenceContext *context{};
		std::unordered_map<VariableReference *, CompileTimeValue> savedKnownConstants;

		explicit ScopedKnownConstantsOverride(InferenceContext *inferenceContext)
			: context(inferenceContext), savedKnownConstants(snapshotKnownConstantsForClassInstantiation(inferenceContext)) {}

		~ScopedKnownConstantsOverride() { restoreKnownConstantsForClassInstantiation(context, std::move(savedKnownConstants)); }
	} scopedKnownConstantsOverride(inferenceContext);

	seedKnownConstantsForClassInstantiation(bindingFrameStack, inferenceContext);

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(classDef->fields.size());
	for (size_t fieldIndex = 0; fieldIndex < classDef->fields.size(); fieldIndex++) {
		FieldDefinition &field = classDef->fields[fieldIndex];
		DataType fieldType = field.declaredType;
		if (fieldType.kind == DataType::Kind::Any && !constructionArgumentTypes) {
			activeInstantiation.completed = true;
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		}
		if (fieldType.kind == DataType::Kind::Unresolved && fieldType.typeExpression) {
			expandPendingTypeReferenceExpression(
				fieldType.typeExpression, field.range.line ? field.range.line->section : nullptr
			);
			if (inferenceContext) {
				Expression *typeExpression = fieldType.typeExpression;
				resetExpressionTypes(typeExpression);
				if (!inferExpression(typeExpression, *inferenceContext, false, bindingFrameStack, false))
					return {};
				fieldType.typeExpression = typeExpression;
				DataType inferredTypeRef;
				if (!readInferredTypeReferenceValue(fieldType.typeExpression, inferenceContext, inferredTypeRef))
					return {};
				fieldType = inferredTypeRef.toReferencedType();
			} else {
				DataType resolvedTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, fieldType.typeExpression, bindingFrameStack, resolvedTypeRef, inferenceContext
					) ||
					resolvedTypeRef.kind != DataType::Kind::Type)
					return {};
				fieldType = resolvedTypeRef.toReferencedType();
			}
		}

		if (constructionArgumentTypes) {
			DataType completedFieldType;
			if (!completeBoundClassFieldType(fieldType, (*constructionArgumentTypes)[fieldIndex], completedFieldType))
				return {};
			fieldType = std::move(completedFieldType);
		} else if (!fieldType.isConcrete()) {
			activeInstantiation.completed = true;
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		}
		fieldTypes.push_back(fieldType);
	}

	int instIndex = -1;
	for (int candidateIndex = 0; candidateIndex < static_cast<int>(classDef->instantiations.size()); candidateIndex++) {
		std::vector<DataType> candidateFieldTypes = fieldTypes;
		for (DataType &candidateFieldType : candidateFieldTypes)
			replaceSymbolicClassInstantiation(candidateFieldType, classDef, symbolicIndex, candidateIndex);
		if (candidateFieldTypes != classDef->instantiations[candidateIndex].fieldTypes)
			continue;
		fieldTypes = std::move(candidateFieldTypes);
		instIndex = candidateIndex;
		break;
	}
	if (instIndex < 0) {
		int newIndex = static_cast<int>(classDef->instantiations.size());
		for (DataType &fieldType : fieldTypes)
			replaceSymbolicClassInstantiation(fieldType, classDef, symbolicIndex, newIndex);
		recordClassInstantiationAppend(classDef);
		instIndex = classDef->getOrCreateInstantiation(fieldTypes);
		requireCompilerInvariant(instIndex == newIndex, "new recursive class instantiation did not use its planned index");
	}
	resolveStoredSymbolicClassInstantiation(classDef, symbolicIndex, instIndex);
	cacheClassInstantiationRequest(classDef, requestKey, instIndex);
	activeInstantiation.completed = true;
	return {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
}

static bool instantiateClassFromArgumentTypes(
	ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef, int baseClassInstIndex
) {
	if (!classDef || classDef->fields.size() != argumentTypes.size())
		return false;

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(argumentTypes.size());
	for (size_t i = 0; i < argumentTypes.size(); i++) {
		DataType argumentType = argumentTypes[i];
		if (!argumentType.isConcrete())
			return false;
		DataType fieldConstraint = classDef->fields[i].declaredType;
		if (baseClassInstIndex >= 0 && static_cast<size_t>(baseClassInstIndex) < classDef->instantiations.size() &&
			i < classDef->instantiations[baseClassInstIndex].fieldTypes.size()) {
			fieldConstraint = classDef->instantiations[baseClassInstIndex].fieldTypes[i];
		}
		DataType fieldType;
		if (!completeBoundClassFieldType(fieldConstraint, argumentType, fieldType))
			return false;
		fieldTypes.push_back(std::move(fieldType));
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	outTypeRef = {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
	return true;
}

static bool isWholeNumberLiteral(Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::Literal || !std::holds_alternative<double>(expr->literalValue))
		return false;
	std::string_view literalText = expr->range.subString;
	if (literalText.find('.') != std::string_view::npos || literalText.find('e') != std::string_view::npos ||
		literalText.find('E') != std::string_view::npos)
		return false;
	double value = std::get<double>(expr->literalValue);
	return std::trunc(value) == value;
}

static std::string makeFloatLiteralReplacement(Expression *expr) {
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

static DataType resolveBuiltInPropertyType(const DataType &ownerType, const std::string &fieldName) {
	if (fieldName == "data" && ownerType.isBytePointer())
		return ownerType;
	return {};
}

static bool isLogicalOperandType(const DataType &type) { return type.kind == DataType::Kind::Bool; }

static bool isBitwiseOperandType(const DataType &type) { return type.isInteger(); }

static std::string extractFieldName(Expression *expr) {
	if (!expr)
		return {};
	if (auto *str = std::get_if<std::string>(&expr->literalValue))
		return *str;
	if (expr->kind == Expression::Kind::Variable && expr->variable)
		return expr->variable->name;
	return {};
}

static std::string diagnosticExpressionText(Expression *expr) {
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

struct DiagnosticExpressionSnapshot {
	Range range;
	std::string text;
};

static DiagnosticExpressionSnapshot captureDiagnosticExpressionSnapshot(Expression *expr) {
	if (!expr)
		return {};
	return {expr->range, diagnosticExpressionText(expr)};
}

static Diagnostic buildTypeFailureDiagnostic(
	const ParseContext &parseContext, const DiagnosticExpressionSnapshot &snapshot, const std::string &detail,
	const std::vector<RelatedInfo> &relatedInfo = {}
) {
	std::string expressionText = snapshot.text;
	Diagnostic diagnostic;
	if (detail.empty()) {
		diagnostic = Diagnostic(
			parseContext, Diagnostic::Level::Error, "expression type inference failed", snapshot.range, "expression",
			expressionText
		);
	} else {
		diagnostic = Diagnostic(
			parseContext, Diagnostic::Level::Error, "expression type inference failed", "with detail", snapshot.range,
			"expression", expressionText, "detail", detail
		);
	}
	diagnostic.relatedInfo = relatedInfo;
	return diagnostic;
}

static Diagnostic buildFailureDetailDiagnostic(Range range, std::string detail, std::vector<RelatedInfo> relatedInfo = {}) {
	Diagnostic diagnostic;
	diagnostic.level = Diagnostic::Level::Error;
	diagnostic.message = std::move(detail);
	diagnostic.range = range;
	diagnostic.relatedInfo = std::move(relatedInfo);
	return diagnostic;
}

static std::string formatInferenceTraceType(const DataType &type, ParseContext &parseContext) {
	if (!type.isDeduced())
		return "undeduced";
	if (type.kind == DataType::Kind::Type) {
		DataType referencedType = type.toReferencedType();
		if (referencedType.kind == DataType::Kind::Unresolved)
			return "type";
		return "type(" + typeToUserName(referencedType, parseContext) + ")";
	}
	if (type.kind == DataType::Kind::Constraint)
		return "constraint";
	return typeToUserName(type, parseContext);
}

static std::string encodeDataTypeForCacheKey(const DataType &type) {
	std::string key;
	key.reserve(96);
	key += "k";
	key += std::to_string(static_cast<int>(type.kind));
	key += "|n";
	key += std::to_string(type.numericSize);
	key += "|p";
	key += std::to_string(type.pointerDepth);
	key += "|r";
	key += std::to_string(static_cast<int>(type.referencedKind));
	key += "|a";
	key += std::to_string(type.arraySize);
	key += "|m";
	key += std::to_string(type.matrixRowCount);
	key += "|ci";
	key += std::to_string(type.classInstIndex);
	key += "|cd";
	key += std::to_string(reinterpret_cast<uintptr_t>(type.classDefinition));
	key += "|te";
	key += std::to_string(reinterpret_cast<uintptr_t>(type.typeExpression));
	key += "|e";
	if (type.arrayElementType)
		key += "(" + encodeDataTypeForCacheKey(*type.arrayElementType) + ")";
	else
		key += "()";
	return key;
}

static std::string encodeTypeConstraintForCacheKey(const TypeConstraint &constraint) {
	std::string key = constraint.isResolved() ? "r1" : "r0";
	auto appendOptional = [&](std::string_view name, const auto &value) {
		key += "|" + std::string(name);
		key += value ? std::to_string(static_cast<long long>(*value)) : "-";
	};
	appendOptional("k", constraint.kind);
	appendOptional("n", constraint.numericSize);
	appendOptional("p", constraint.pointerDepth);
	appendOptional("a", constraint.arraySize);
	appendOptional("mr", constraint.matrixRows);
	appendOptional("mc", constraint.matrixColumns);
	appendOptional("ci", constraint.classInstantiationIndex);
	key += "|cd" + std::to_string(reinterpret_cast<uintptr_t>(constraint.classDefinition));
	key += constraint.constrainsClassDefinition ? "|dc1" : "|dc0";
	key += constraint.requiresCompileTimeValue ? "|ct1" : "|ct0";
	key += "|e";
	key += constraint.elementConstraint ? "(" + encodeTypeConstraintForCacheKey(*constraint.elementConstraint) + ")" : "()";
	return key;
}

static std::string encodeCompileTimeValueForCacheKey(const CompileTimeValue &value) {
	if (std::holds_alternative<std::monostate>(value))
		return "?";
	if (const auto *number = std::get_if<double>(&value))
		return "d" + std::to_string(std::bit_cast<uint64_t>(*number));
	if (const auto *text = std::get_if<std::string>(&value))
		return "s" + std::to_string(text->size()) + ":" + *text;
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "b1" : "b0";
	if (const auto *typeRef = std::get_if<TypeReferenceValue>(&value))
		return "t" + encodeDataTypeForCacheKey(typeRef->type) + "|" + encodeTypeConstraintForCacheKey(typeRef->constraint);
	if (const auto *constraint = std::get_if<TypeConstraint>(&value))
		return "c" + encodeTypeConstraintForCacheKey(*constraint);
	return "?";
}

static bool expressionParticipatesInInferenceTrace(Expression *expr) {
	if (!expr)
		return false;
	return expr->kind == Expression::Kind::PatternCall || expr->kind == Expression::Kind::IntrinsicCall;
}

static std::string describeInferenceTraceFrame(Expression *expr, ParseContext &parseContext) {
	if (!expr)
		return "while inferring <expression>";

	std::string expressionText = diagnosticExpressionText(expr);
	if (expressionText.empty())
		expressionText = "<expression>";

	std::vector<DataType> argumentTypes;
	argumentTypes.reserve(expr->arguments.size());
	for (Expression *argument : expr->arguments)
		argumentTypes.push_back(argument ? argument->type : DataType{});
	std::string resultType = formatInferenceTraceType(expr->type, parseContext);

	if (expr->kind == Expression::Kind::IntrinsicCall) {
		std::string frame = "while inferring intrinsic '" + expr->intrinsicName + "' in '" + expressionText + "'";
		if (!argumentTypes.empty())
			frame += " with arguments [" + formatTypeList(argumentTypes, parseContext) + "]";
		frame += " -> " + resultType;
		return frame;
	}

	std::string frame = "while inferring call '" + expressionText + "'";
	if (!argumentTypes.empty())
		frame += " with arguments [" + formatTypeList(argumentTypes, parseContext) + "]";
	frame += " -> " + resultType;
	return frame;
}

static bool isValidCastRuntimeType(const DataType &type) {
	if (!type.isDeduced() || type.kind == DataType::Kind::Void || type.isMetaType())
		return false;
	if (type.kind == DataType::Kind::Class && !type.isPointer())
		return false;
	return true;
}

// Must stay in sync with codegen's ensureType conversion support.
static bool isSupportedCastConversion(const DataType &fromType, const DataType &toType) {
	return isValidCastRuntimeType(fromType) && isValidCastRuntimeType(toType) &&
		   DataType::supportsRuntimeConversion(fromType, toType);
}

static bool tryResolveCastResultType(const DataType &fromType, const DataType &typeArgType, DataType &outType) {
	if (!isValidCastRuntimeType(fromType))
		return false;
	if (typeArgType.kind != DataType::Kind::Type)
		return false;
	DataType toType = typeArgType.toReferencedType();
	if (!isSupportedCastConversion(fromType, toType))
		return false;
	outType = toType;
	return true;
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
struct TrialInstantiationSummary {
	DataType returnType{};
	CompileTimeValue returnValue{};
	InstantiationPurity purity = InstantiationPurity::Pure;
	std::unordered_set<VariableReference *> writtenGlobalReferences;
	std::unordered_map<VariableReference *, CompileTimeValue> finalGlobalConstantValues;
};

struct TrialInstantiationCacheKey {
	Section *section{};
	bool sectionHasCommittedOrdering = false;
	std::vector<DataType> argumentTypes;
	std::vector<std::pair<std::string, std::string>> compileTimeParameters;
	bool operator==(const TrialInstantiationCacheKey &) const = default;

	bool operator<(const TrialInstantiationCacheKey &other) const {
		if (section != other.section)
			return std::less<Section *>{}(section, other.section);
		return std::tie(sectionHasCommittedOrdering, argumentTypes, compileTimeParameters) <
			   std::tie(other.sectionHasCommittedOrdering, other.argumentTypes, other.compileTimeParameters);
	}
};

struct TrialInstantiationCache : std::map<TrialInstantiationCacheKey, TrialInstantiationSummary> {
	size_t validatedClassInstantiationRollbackVersion = classInstantiationRollbackVersion;
};

static bool dataTypeReferencesExistingClassInstantiations(
	const DataType &type, std::set<std::pair<ClassDefinition *, int>> &visitedInstantiations,
	bool allowActiveSymbolicInstantiation
) {
	if (type.arrayElementType && !dataTypeReferencesExistingClassInstantiations(
									 *type.arrayElementType, visitedInstantiations, allowActiveSymbolicInstantiation
								 ))
		return false;
	bool hasClassPayload = type.kind == DataType::Kind::Class ||
						   (type.kind == DataType::Kind::Type && type.referencedKind == DataType::Kind::Class);
	if (!hasClassPayload)
		return true;
	if (!type.classDefinition)
		return false;
	if (type.classInstIndex == -1)
		return true;
	if (type.classInstIndex < -1) {
		if (!allowActiveSymbolicInstantiation)
			return false;
		return std::any_of(activeClassInstantiations.begin(), activeClassInstantiations.end(), [&](const auto &active) {
			return active.classDefinition == type.classDefinition && active.symbolicIndex == type.classInstIndex;
		});
	}
	if (type.classInstIndex >= static_cast<int>(type.classDefinition->instantiations.size()))
		return false;
	auto instantiation = std::make_pair(type.classDefinition, type.classInstIndex);
	if (!visitedInstantiations.insert(instantiation).second)
		return true;
	for (const DataType &fieldType : type.classDefinition->instantiations[type.classInstIndex].fieldTypes) {
		if (!dataTypeReferencesExistingClassInstantiations(fieldType, visitedInstantiations, allowActiveSymbolicInstantiation))
			return false;
	}
	return true;
}

static bool dataTypeReferencesExistingClassInstantiations(const DataType &type) {
	std::set<std::pair<ClassDefinition *, int>> visitedInstantiations;
	return dataTypeReferencesExistingClassInstantiations(type, visitedInstantiations, false);
}

static bool dataTypeReferencesAvailableClassInstantiations(const DataType &type) {
	std::set<std::pair<ClassDefinition *, int>> visitedInstantiations;
	return dataTypeReferencesExistingClassInstantiations(type, visitedInstantiations, true);
}

static bool
trialInstantiationCacheEntryIsValid(const TrialInstantiationCacheKey &key, const TrialInstantiationSummary &summary) {
	for (const DataType &argumentType : key.argumentTypes) {
		if (!dataTypeReferencesExistingClassInstantiations(argumentType))
			return false;
	}
	if (!dataTypeReferencesExistingClassInstantiations(summary.returnType))
		return false;
	if (const auto *typeReference = std::get_if<TypeReferenceValue>(&summary.returnValue))
		return dataTypeReferencesExistingClassInstantiations(typeReference->type);
	return true;
}

static void pruneInvalidTrialInstantiationCacheEntries(TrialInstantiationCache &cache) {
	if (cache.validatedClassInstantiationRollbackVersion == classInstantiationRollbackVersion)
		return;
	for (auto entry = cache.begin(); entry != cache.end();) {
		if (!trialInstantiationCacheEntryIsValid(entry->first, entry->second)) {
			entry = cache.erase(entry);
		} else {
			entry++;
		}
	}
	cache.validatedClassInstantiationRollbackVersion = classInstantiationRollbackVersion;
}

struct InferenceContext {
	struct OperandGroupingWarning {
		Range range;
		std::string expressionText;
		std::string chosenGrouping;
		std::string alternativeGrouping;
		std::vector<RelatedInfo> relatedInfo;
	};

	struct TrialJournal {
		enum class SectionInstantiationRetargetResult {
			Updated,
			MissingSourceRecord,
			SourceWasPreexisting,
			TargetAlreadyRecorded,
		};

		struct VariableUndo {
			Variable *variable;
			DataType type;
			Range typeOriginRange;
			std::string typeOriginFloatLiteralReplacement;
		};

		struct SectionInstantiationUndo {
			Section *section;
			InstantiationKey key;
			bool existed;
			Instantiation value;
		};

		struct InstantiationPurityUndo {
			Instantiation *instantiation;
			InstantiationPurity purity;
		};

		struct InstantiationReturnTypeUndo {
			Instantiation *instantiation;
			DataType returnType;
			Range returnTypeOriginRange;
		};

		std::vector<VariableUndo> variableTypeUndo;
		std::unordered_set<Variable *> seenVariables;
		std::vector<Section *> touchedSections;
		std::unordered_set<Section *> seenSections;
		std::vector<SectionInstantiationUndo> sectionInstantiationUndo;
		std::unordered_set<std::string> seenSectionInstantiations;
		std::vector<InstantiationPurityUndo> instantiationPurityUndo;
		std::unordered_set<Instantiation *> seenInstantiationPurities;
		std::vector<InstantiationReturnTypeUndo> instantiationReturnTypeUndo;
		std::unordered_set<Instantiation *> seenInstantiationReturnTypes;

		static std::string sectionInstantiationUndoId(Section *section, const InstantiationKey &key) {
			std::string keyString = std::to_string(reinterpret_cast<uintptr_t>(section)) + "|";
			for (const DataType &type : key.argumentTypes)
				keyString += encodeDataTypeForCacheKey(type) + ";";
			keyString += "|";
			for (const auto &[name, value] : key.compileTimeParameters) {
				keyString += name + "=";
				keyString += encodeCompileTimeValueForCacheKey(value);
				keyString += ";";
			}
			return keyString;
		}

		void recordVariableWrite(Variable *var) {
			if (!var || seenVariables.contains(var))
				return;
			seenVariables.insert(var);
			variableTypeUndo.push_back({var, var->type, var->typeOriginRange, var->typeOriginFloatLiteralReplacement});
		}

		void recordTouchedSection(Section *section) {
			if (!section || seenSections.contains(section))
				return;
			seenSections.insert(section);
			touchedSections.push_back(section);
		}

		void recordSectionInstantiationWrite(Section *section, const InstantiationKey &key) {
			if (!section)
				return;
			std::string keyString = sectionInstantiationUndoId(section, key);
			if (seenSectionInstantiations.contains(keyString))
				return;
			seenSectionInstantiations.insert(keyString);
			auto it = section->instantiations.find(key);
			if (it == section->instantiations.end())
				sectionInstantiationUndo.push_back({section, key, false, {}});
			else
				sectionInstantiationUndo.push_back({section, key, true, it->second});
		}

		void recordInstantiationPurityWrite(Instantiation *instantiation) {
			if (!instantiation || seenInstantiationPurities.contains(instantiation))
				return;
			seenInstantiationPurities.insert(instantiation);
			instantiationPurityUndo.push_back({instantiation, instantiation->purity});
		}

		void recordInstantiationReturnTypeWrite(Instantiation *instantiation) {
			if (!instantiation || seenInstantiationReturnTypes.contains(instantiation))
				return;
			seenInstantiationReturnTypes.insert(instantiation);
			instantiationReturnTypeUndo.push_back(
				{instantiation, instantiation->returnType, instantiation->returnTypeOriginRange}
			);
		}

		SectionInstantiationRetargetResult
		retargetSectionInstantiationWrite(Section *section, const InstantiationKey &fromKey, const InstantiationKey &toKey) {
			if (!section)
				return SectionInstantiationRetargetResult::MissingSourceRecord;
			if (fromKey == toKey)
				return SectionInstantiationRetargetResult::Updated;
			std::string fromId = sectionInstantiationUndoId(section, fromKey);
			auto fromSeenIt = seenSectionInstantiations.find(fromId);
			if (fromSeenIt == seenSectionInstantiations.end())
				return SectionInstantiationRetargetResult::MissingSourceRecord;
			auto undoIt = std::find_if(
				sectionInstantiationUndo.begin(), sectionInstantiationUndo.end(),
				[&](const SectionInstantiationUndo &undo) {
				return undo.section == section && undo.key == fromKey;
			}
			);
			if (undoIt == sectionInstantiationUndo.end())
				return SectionInstantiationRetargetResult::MissingSourceRecord;
			if (undoIt->existed)
				return SectionInstantiationRetargetResult::SourceWasPreexisting;
			std::string toId = sectionInstantiationUndoId(section, toKey);
			if (seenSectionInstantiations.contains(toId))
				return SectionInstantiationRetargetResult::TargetAlreadyRecorded;
			seenSectionInstantiations.erase(fromSeenIt);
			seenSectionInstantiations.insert(std::move(toId));
			undoIt->key = toKey;
			return SectionInstantiationRetargetResult::Updated;
		}
	};

	ParseContext &parseContext;
	Instantiation *currentInstantiation{};
	std::vector<std::shared_ptr<InstantiatedSectionBody>> activeFlexInferenceBodies;
	std::unordered_map<VariableReference *, CompileTimeValue> currentKnownConstants;
	std::vector<std::unordered_set<VariableReference *>> loopMutationStack;
	bool typesValid = true;
	bool trial = false;
	bool suppressDiagnostics = false;
	bool suppressReinferPassDiagnostics = false;
	bool observedInProgressUndeducedInstantiation = false;
	bool allowTrialSummaryReuse = false;
	std::string typeFailureDetail;
	std::vector<RelatedInfo> typeFailureRelatedInfo;
	DiagnosticExpressionSnapshot typeFailureSnapshot;
	Diagnostic typeFailureDiagnostic;
	int typeFailurePriority = -1;
	bool hasTypeFailureDiagnostic = false;
	TrialJournal *trialJournal{};
	std::shared_ptr<TrialInstantiationCache> trialInstantiationCache;
	// Signature inference sets this signal while probing a type-constraint
	// expression. Encountering an overload whose own signature is unresolved
	// defers the probe instead of choosing a declaration-order candidate.
	std::shared_ptr<bool> unresolvedPatternConstraintSignal;
	const std::unordered_set<Expression *> *fixedGroupingRoots{};
	std::unordered_set<Expression *> *resolvedGroupingRoots{};
	bool detectGroupingAmbiguity = false;
	bool groupingAmbiguityIncomplete = false;
	std::vector<OperandGroupingWarning> *pendingOperandGroupingWarnings{};
	std::vector<Expression *> expressionStack;
	std::unordered_map<Expression *, CompileTimeValue> trialExpressionValues;
	const std::unordered_map<Expression *, CompileTimeValue> *inheritedTrialExpressionValues{};
	std::unordered_map<CodeLine *, GroupingSnapshot> trialCodeLineGroupings;

	InferenceContext(ParseContext &pc) : parseContext(pc) {}
	InferenceContext(ParseContext &pc, bool trial) : parseContext(pc), trial(trial) {}

	void addDiagnostic(Diagnostic diagnostic) {
		if (!trial && !suppressDiagnostics && !suppressReinferPassDiagnostics)
			parseContext.addDiagnostic(std::move(diagnostic));
	}

	Expression *currentExpression() const { return expressionStack.empty() ? nullptr : expressionStack.back(); }

	void pushExpression(Expression *expression) { expressionStack.push_back(expression); }

	void popExpression() {
		requireCompilerInvariant(!expressionStack.empty(), "Expression stack underflow during type inference");
		expressionStack.pop_back();
	}

	std::vector<RelatedInfo> captureInferenceTraceRelatedInfo(Expression *currentExpressionOverride = nullptr) const {
		std::vector<RelatedInfo> relatedInfo;
		if (currentExpressionOverride && expressionParticipatesInInferenceTrace(currentExpressionOverride) &&
			currentExpressionOverride->range.line) {
			relatedInfo.push_back(
				{describeInferenceTraceFrame(currentExpressionOverride, parseContext), currentExpressionOverride->range}
			);
		}
		for (auto it = expressionStack.rbegin(); it != expressionStack.rend(); ++it) {
			Expression *expression = *it;
			if (!expressionParticipatesInInferenceTrace(expression) || !expression->range.line)
				continue;
			if (expression == currentExpressionOverride)
				continue;
			relatedInfo.push_back({describeInferenceTraceFrame(expression, parseContext), expression->range});
		}
		return relatedInfo;
	}

	void appendCurrentInferenceTrace(Diagnostic &diagnostic) const {
		std::vector<RelatedInfo> trace = captureInferenceTraceRelatedInfo();
		diagnostic.relatedInfo.insert(diagnostic.relatedInfo.end(), trace.begin(), trace.end());
	}

	void addDiagnosticWithCurrentTrace(Diagnostic diagnostic) {
		appendCurrentInferenceTrace(diagnostic);
		addDiagnostic(std::move(diagnostic));
	}

	void setTypeFailure(std::string detail) {
		typesValid = false;
		if (typeFailureDetail.empty()) {
			typeFailureDetail = std::move(detail);
			typeFailureRelatedInfo = captureInferenceTraceRelatedInfo();
			typeFailureSnapshot = captureDiagnosticExpressionSnapshot(currentExpression());
		}
	}

	void fail(Diagnostic diagnostic, int priority = 1) {
		typesValid = false;
		if (hasTypeFailureDiagnostic && typeFailurePriority >= priority)
			return;
		appendCurrentInferenceTrace(diagnostic);
		typeFailureDiagnostic = std::move(diagnostic);
		typeFailurePriority = priority;
		hasTypeFailureDiagnostic = true;
	}

	void clearTypeFailure() {
		typeFailureDetail.clear();
		typeFailureRelatedInfo.clear();
		typeFailureSnapshot = {};
		typeFailureDiagnostic = Diagnostic();
		typeFailurePriority = -1;
		hasTypeFailureDiagnostic = false;
	}

	void inheritTypeFailureFrom(const InferenceContext &other) {
		if (!hasTypeFailureDiagnostic && other.hasTypeFailureDiagnostic) {
			typeFailureDiagnostic = other.typeFailureDiagnostic;
			typeFailurePriority = other.typeFailurePriority;
			hasTypeFailureDiagnostic = true;
		}
		if (!typeFailureDetail.empty() || other.typeFailureDetail.empty())
			return;
		typeFailureDetail = other.typeFailureDetail;
		typeFailureRelatedInfo = other.typeFailureRelatedInfo;
		typeFailureSnapshot = other.typeFailureSnapshot;
	}

	std::shared_ptr<TrialInstantiationCache> ensureTrialInstantiationCache() {
		if (!trialInstantiationCache)
			trialInstantiationCache = std::make_shared<TrialInstantiationCache>();
		return trialInstantiationCache;
	}

	VariableReference *normalizeReference(VariableReference *reference) const {
		if (!reference)
			return nullptr;
		return reference->definition ? reference->definition : reference;
	}

	CompileTimeValue lookupExpressionValue(Expression *expression) const {
		if (!expression)
			return {};
		if (trial) {
			auto trialIt = trialExpressionValues.find(expression);
			if (trialIt != trialExpressionValues.end())
				return trialIt->second;
			if (inheritedTrialExpressionValues) {
				auto inheritedIt = inheritedTrialExpressionValues->find(expression);
				if (inheritedIt != inheritedTrialExpressionValues->end())
					return inheritedIt->second;
			}
		}
		return getExpressionCompileTimeValue(expression);
	}

	void setExpressionValue(Expression *expression, const CompileTimeValue &value) {
		if (!expression)
			return;
		if (trial) {
			if (isCompileTimeKnown(value))
				trialExpressionValues[expression] = value;
			else
				trialExpressionValues.erase(expression);
			return;
		}
		setExpressionCompileTimeValue(expression, value);
	}

	Expression *lookupFlexExpansion(Expression *expression) const {
		return expression ? expression->inferredFlexExpansion : nullptr;
	}

	CompileTimeValue lookupKnownConstant(VariableReference *reference) const {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return {};
		auto it = currentKnownConstants.find(key);
		return it != currentKnownConstants.end() ? it->second : CompileTimeValue{};
	}

	void setKnownConstant(VariableReference *reference, const CompileTimeValue &value) {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return;
		if (isCompileTimeKnown(value))
			currentKnownConstants[key] = value;
		else
			currentKnownConstants.erase(key);
	}

	void noteWrittenGlobalReference(VariableReference *reference) {
		if (!currentInstantiation || !reference)
			return;
		VariableReference *key = normalizeReference(reference);
		if (key)
			currentInstantiation->writtenGlobalReferences.insert(key);
	}

	void pushLoopMutationScope() { loopMutationStack.emplace_back(); }

	std::unordered_set<VariableReference *> popLoopMutationScope() {
		if (loopMutationStack.empty())
			return {};
		std::unordered_set<VariableReference *> mutations = std::move(loopMutationStack.back());
		loopMutationStack.pop_back();
		if (!loopMutationStack.empty())
			loopMutationStack.back().insert(mutations.begin(), mutations.end());
		return mutations;
	}

	bool inLoopMutationScope() const { return !loopMutationStack.empty(); }

	void noteLoopMutation(VariableReference *reference) {
		if (loopMutationStack.empty())
			return;
		VariableReference *normalized = normalizeReference(reference);
		if (!normalized)
			return;
		loopMutationStack.back().insert(normalized);
	}
};

static std::unordered_map<VariableReference *, CompileTimeValue>
snapshotKnownConstantsForClassInstantiation(InferenceContext *inferenceContext) {
	return inferenceContext ? inferenceContext->currentKnownConstants
							: std::unordered_map<VariableReference *, CompileTimeValue>{};
}

static void restoreKnownConstantsForClassInstantiation(
	InferenceContext *inferenceContext, std::unordered_map<VariableReference *, CompileTimeValue> savedKnownConstants
) {
	if (inferenceContext)
		inferenceContext->currentKnownConstants = std::move(savedKnownConstants);
}

static void
seedKnownConstantsForClassInstantiation(const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext) {
	if (!inferenceContext || bindingFrameStack.empty())
		return;
	const BindingFrame &classBindings = bindingFrameStack.topFrame();
	for (const auto &[parameterDefinition, argumentExpression] : classBindings.parameterBindings) {
		if (!parameterDefinition)
			continue;
		CompileTimeValue argumentValue = resolveStoredCompileTimeValue(argumentExpression, bindingFrameStack, inferenceContext);
		inferenceContext->setKnownConstant(parameterDefinition, argumentValue);
	}
}

static Expression *lookupInferenceFlexExpansion(InferenceContext *inferenceContext, Expression *expression) {
	return inferenceContext ? inferenceContext->lookupFlexExpansion(expression)
							: (expression ? expression->inferredFlexExpansion : nullptr);
}

static CompileTimeValue lookupCompileTimeExpressionValue(Expression *expression, InferenceContext *inferenceContext) {
	if (inferenceContext)
		return inferenceContext->lookupExpressionValue(expression);
	return getExpressionCompileTimeValue(expression);
}

static Expression *resolveCompileTimeBindingForInference(
	Expression *expression, const BindingFrameStack &bindingFrameStack, BindingFrameStack *outBindingFrameStack,
	InferenceContext *inferenceContext
) {
	Expression *resolvedExpression = resolveCompileTimeBinding(expression, bindingFrameStack, outBindingFrameStack);
	if (!inferenceContext || !inferenceContext->trial || !expression || resolvedExpression != expression)
		return resolvedExpression;
	if (expression->kind != Expression::Kind::PatternCall)
		return resolvedExpression;
	Expression *trialFlexExpansion = inferenceContext->lookupFlexExpansion(expression);
	if (!trialFlexExpansion)
		return resolvedExpression;
	PatternDefinition *selectedPatternDefinition = expression->selectedPatternDefinition;
	if (!selectedPatternDefinition)
		crashCompilerBug("trial flex expansion has no selected definition");
	if (!selectedPatternDefinition || !selectedPatternDefinition->section || !selectedPatternDefinition->section->isFlex ||
		selectedPatternDefinition->section->type != SectionType::Function) {
		return resolvedExpression;
	}
	BindingFrame innerBindings;
	collectPatternCallBindings(expression, selectedPatternDefinition, innerBindings);
	materializeFlexBindingsInCallerScope(innerBindings, bindingFrameStack);
	if (outBindingFrameStack) {
		*outBindingFrameStack = bindingFrameStack;
		pushBindingScope(*outBindingFrameStack, std::move(innerBindings));
	}
	return trialFlexExpansion;
}

static CompileTimeValue resolveStoredCompileTimeValue(
	Expression *expression, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext
) {
	if (!expression)
		crashCompilerBug("compile-time value resolution received null expression");
	Expression *currentExpression = expression;
	BindingFrameStack currentBindingFrameStack = bindingFrameStack;
	constexpr size_t maxResolutionDepth = 256;
	for (size_t depth = 0; currentExpression && depth < maxResolutionDepth; depth++) {
		CompileTimeValue storedValue = lookupCompileTimeExpressionValue(currentExpression, inferenceContext);
		if (isCompileTimeKnown(storedValue))
			return storedValue;
		if (inferenceContext && currentExpression->kind == Expression::Kind::Variable && currentExpression->variable) {
			CompileTimeValue knownValue = inferenceContext->lookupKnownConstant(currentExpression->variable);
			if (isCompileTimeKnown(knownValue))
				return knownValue;
		}
		BindingFrameStack resolvedBindingFrameStack;
		Expression *resolvedExpression = resolveCompileTimeBindingForInference(
			currentExpression, currentBindingFrameStack, &resolvedBindingFrameStack, inferenceContext
		);
		if (resolvedExpression && resolvedExpression != currentExpression) {
			currentExpression = resolvedExpression;
			currentBindingFrameStack = std::move(resolvedBindingFrameStack);
			continue;
		}
		CompileTimeValue immediateValue = resolveImmediateCompileTimeValue(currentExpression);
		if (isCompileTimeKnown(immediateValue))
			return immediateValue;
		break;
	}
	return {};
}

static bool resolveStoredCompileTimeInteger(
	Expression *expression, const BindingFrameStack &bindingFrameStack, int &outValue, InferenceContext *inferenceContext
) {
	std::optional<std::int64_t> integerValue =
		getCompileTimeIntegerValue(resolveStoredCompileTimeValue(expression, bindingFrameStack, inferenceContext));
	if (!integerValue.has_value() || *integerValue < std::numeric_limits<int>::min() ||
		*integerValue > std::numeric_limits<int>::max()) {
		return false;
	}
	outValue = static_cast<int>(*integerValue);
	return true;
}

struct ScopedDiagnosticSuppression {
	InferenceContext &context;
	bool previous;

	explicit ScopedDiagnosticSuppression(InferenceContext &context) : context(context), previous(context.suppressDiagnostics) {
		context.suppressDiagnostics = true;
	}

	~ScopedDiagnosticSuppression() { context.suppressDiagnostics = previous; }
};
