#pragma once

#include "addressProvenance.h"
#include "compiler.h"
#include "const_evaluation.inl"
#include "knownConstantState.h"
#include "numericLiteral.h"
#include <limits>
#include <tuple>

static std::string encodeDataTypeForCacheKey(const DataType &type);
static std::string encodeCompileTimeValueForCacheKey(const CompileTimeValue &value);
static bool
refineUnspecifiedClassInstantiation(const DataType &currentType, const DataType &incomingType, DataType &refinedType);

static std::optional<DataType> parseNumericTokenType(std::string_view token, bool emitSPIRV) {
	NumericLiteralParseResult parsed = parseNumericLiteral(token);
	if (!parsed)
		return std::nullopt;
	return numericLiteralType(parsed.value, emitSPIRV);
}

static Expression *prepareCompileTimeTypeReferenceExpression(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack &effectiveBindingFrameStack,
	InferenceContext *inferenceContext
) {
	ResolvedBindingLayers resolvedBinding = resolveInferenceBindingLayers(expr, bindingFrameStack, inferenceContext);
	Expression *resolved = resolvedBinding.expression;
	effectiveBindingFrameStack = std::move(resolvedBinding.bindingFrameStack);
	if (!resolved)
		return nullptr;
	if (!expandPendingTypeReferenceExpression(resolved, resolved->range.line ? resolved->range.line->section : nullptr))
		return resolved;
	ResolvedBindingLayers rebound = resolveInferenceBindingLayers(resolved, effectiveBindingFrameStack, inferenceContext);
	if (rebound.expression) {
		effectiveBindingFrameStack = std::move(rebound.bindingFrameStack);
		return rebound.expression;
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

static KnownConstantState snapshotKnownConstantsForClassInstantiation(InferenceContext *inferenceContext);
static AddressInferenceState snapshotAddressStateForClassInstantiation(InferenceContext *inferenceContext);
static void
restoreKnownConstantsForClassInstantiation(InferenceContext *inferenceContext, KnownConstantState savedKnownConstants);
static void
restoreAddressStateForClassInstantiation(InferenceContext *inferenceContext, AddressInferenceState savedAddressState);
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

#include "class_instantiation_transaction.inl"

static TypeResolutionKey buildTypeResolutionKey(const Expression *expression, const BindingFrameStack &bindingFrameStack) {
	TypeResolutionKey typeResolutionKey;
	typeResolutionKey.expression = expression;
	typeResolutionKey.bindingContext = buildBindingContext(bindingFrameStack);
	return typeResolutionKey;
}

static bool tryResolveCastResultType(const DataType &fromType, const DataType &typeArgType, DataType &outType);

static DataType
resolveKnownExpressionType(Expression *expr, const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext) {
	if (expr && expr->inferredConversion)
		return resolveKnownExpressionType(expr->inferredConversion, bindingFrameStack, inferenceContext);
	if (expr && expr->kind == Expression::Kind::PatternCall && expr->type.isDeduced() && !bindingFrameStack.hasBindings()) {
		return expr->type;
	}
	ResolvedBindingLayers resolvedBinding = resolveInferenceBindingLayers(expr, bindingFrameStack, inferenceContext);
	Expression *resolved = resolvedBinding.expression;
	BindingFrameStack effectiveBindingFrameStack = std::move(resolvedBinding.bindingFrameStack);
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
		bool emitSPIRV = activeTypeResolutionParseContext && activeTypeResolutionParseContext->options.emitSPIRV;
		if (const auto *integer = std::get_if<std::int64_t>(&resolved->literalValue))
			return numericLiteralType(NumericLiteralValue{*integer}, emitSPIRV);
		if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&resolved->literalValue))
			return numericLiteralType(NumericLiteralValue{*minimumMagnitude}, emitSPIRV);
		if (const auto *floatingPoint = std::get_if<double>(&resolved->literalValue))
			return numericLiteralType(NumericLiteralValue{*floatingPoint}, emitSPIRV);
		if (std::holds_alternative<std::string>(resolved->literalValue)) {
			DataType strType{DataType::Kind::Int, 1};
			strType.pointerDepth = 1;
			return strType;
		}
	}
	if (resolved->kind == Expression::Kind::ArrayLiteral) {
		if (resolved->arguments.empty())
			return {};
		DataType elementType = resolveKnownExpressionType(resolved->arguments[0], effectiveBindingFrameStack);
		if (!elementType.isDeduced())
			return {};
		for (size_t i = 1; i < resolved->arguments.size(); i++) {
			DataType nextType = resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack);
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
		Section *sec = resolved->range.line ? resolved->range.line->section : nullptr;
		Variable *var = sec ? sec->findVariable(varRef) : nullptr;
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
	if (resolved->kind == Expression::Kind::IntrinsicCall) {
		const IntrinsicInfo *info = findIntrinsic(resolved->intrinsicName);
		IntrinsicKind kind = intrinsicKind(resolved->intrinsicName);
		if (info) {
			switch (info->returnKind) {
			case IntrinsicReturnKind::SameAsArgs:
				if (resolved->arguments.size() == 2)
					return resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
				if (resolved->arguments.size() > 2) {
					DataType leftType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
					DataType rightType = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
					DataType result;
					if (promoteIntrinsicArithmetic(
							arithmeticIntrinsicKind(resolved->intrinsicName), leftType, rightType, result
						))
						return result;
				}
				return {};
			case IntrinsicReturnKind::SameAsInts:
				if (resolved->arguments.size() == 2) {
					DataType valueType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
					if (valueType.isInteger())
						return valueType;
					return {};
				}
				if (resolved->arguments.size() > 2) {
					DataType leftType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
					DataType rightType = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
					DataType result;
					if (DataType::promoteBitwise(leftType, rightType, result))
						return result;
				}
				return {};
			case IntrinsicReturnKind::Bool:
				if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
					DataType leftType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
					DataType rightType = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
					if (leftType.kind == DataType::Kind::Bool && rightType.kind == DataType::Kind::Bool) {
						return {DataType::Kind::Bool};
					}
					return {};
				}
				if (kind == IntrinsicKind::Not) {
					DataType valueType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
					if (valueType.kind == DataType::Kind::Bool)
						return {DataType::Kind::Bool};
					return {};
				}
				if (resolved->arguments.size() > 2) {
					DataType leftType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
					DataType rightType = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
					DataType promoted;
					DataType refinedPointerType;
					bool equality = kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual;
					bool pointerEquality =
						equality && leftType.isPointer() && rightType.isPointer() &&
						(leftType == rightType || refineUnspecifiedClassInstantiation(leftType, rightType, refinedPointerType));
					bool promotable = equality ? DataType::promoteEquality(leftType, rightType, promoted)
											   : DataType::promoteArithmetic(leftType, rightType, promoted);
					if (pointerEquality || promotable)
						return {DataType::Kind::Bool};
				}
				return {};
			case IntrinsicReturnKind::Void:
				if (kind == IntrinsicKind::If || kind == IntrinsicKind::ElseIf || kind == IntrinsicKind::LoopWhile) {
					if (resolved->arguments.size() <= 1)
						return {};
					DataType conditionType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
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
			DataType instType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
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
				Expression *propExpr =
					resolveInferenceBindingLayers(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext)
						.expression;
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
			DataType ptrType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (ptrType.isDeduced() && ptrType.isPointer())
				return ptrType.dereferenced();
		} else if (kind == IntrinsicKind::AddressOf) {
			DataType valueType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (valueType.isDeduced())
				return valueType.pointed();
		} else if (isExternalCallIntrinsicKind(kind)) {
			if (resolved->arguments.size() <= 3)
				return {};
			DataType retTypeRef = resolveKnownExpressionType(resolved->arguments[3], effectiveBindingFrameStack);
			if (retTypeRef.kind != DataType::Kind::Type || retTypeRef.referencedKind == DataType::Kind::Type ||
				retTypeRef.referencedKind == DataType::Kind::Unresolved)
				return {};
			for (size_t i = externalCallRuntimeArgumentStart(kind); i < resolved->arguments.size(); i++) {
				DataType argType = resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack);
				if (argType.kind == DataType::Kind::Type)
					return {};
			}
			return retTypeRef.toReferencedType();
		} else if (kind == IntrinsicKind::Function) {
			return {DataType::Kind::Int, 1, 1};
		} else if (kind == IntrinsicKind::Return) {
			return {DataType::Kind::Void};
		} else if (kind == IntrinsicKind::Cast) {
			DataType valueType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			DataType typeArgType = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
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
			DataType valueType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (valueType.isDeduced())
				return valueType.asTypeReference();
		} else if (kind == IntrinsicKind::ElementType) {
			DataType aggregateType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (aggregateType.kind == DataType::Kind::Type)
				aggregateType = aggregateType.toReferencedType();
			if (aggregateType.hasAggregateElementType())
				return aggregateType.aggregateElementType().asTypeReference();
		} else if (kind == IntrinsicKind::PromoteArithmeticType) {
			return {DataType::Kind::Type};
		} else if (kind == IntrinsicKind::Number) {
			return {DataType::Kind::Constraint};
		} else if (kind == IntrinsicKind::ExtractElement) {
			DataType aggregateType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (aggregateType.hasAggregateElementType())
				return aggregateType.aggregateElementType();
		} else if (kind == IntrinsicKind::InsertElement) {
			DataType aggregateType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (aggregateType.hasAggregateElementType())
				return aggregateType;
		} else if (kind == IntrinsicKind::ShaderInput) {
			DataType vectorType{DataType::Kind::Vector};
			vectorType.arraySize = 4;
			vectorType.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			return vectorType;
		} else if (kind == IntrinsicKind::TypeExtent) {
			DataType valueType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (valueType.isDeduced())
				return {DataType::Kind::Int, 4};
		} else if (kind == IntrinsicKind::SizeOf) {
			DataType typeArgType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (typeArgType.kind == DataType::Kind::Type && typeArgType.referencedKind != DataType::Kind::Type &&
				typeArgType.referencedKind != DataType::Kind::Unresolved)
				return {DataType::Kind::Int, 8};
		} else if (kind == IntrinsicKind::BuildInfo) {
			Expression *keyExpr =
				resolveInferenceBindingLayers(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext).expression;
			if (auto *key = std::get_if<std::string>(&keyExpr->literalValue))
				if (std::optional<DataType> infoType = buildInfoValueType(*key))
					return *infoType;
		} else if (kind == IntrinsicKind::TargetIs) {
			Expression *targetExpr =
				resolveInferenceBindingLayers(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext).expression;
			if (std::holds_alternative<std::string>(targetExpr->literalValue))
				return {DataType::Kind::Bool};
		} else if (kind == IntrinsicKind::ShaderStageIs) {
			Expression *shaderStageExpr =
				resolveInferenceBindingLayers(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext).expression;
			if (std::holds_alternative<std::string>(shaderStageExpr->literalValue))
				return {DataType::Kind::Bool};
		} else if (kind == IntrinsicKind::Select && activeTypeResolutionParseContext) {
			Expression *selectedBranch =
				resolveCompileTimeSelectBranch(resolved, *activeTypeResolutionParseContext, effectiveBindingFrameStack);
			if (selectedBranch)
				return resolveKnownExpressionType(selectedBranch, effectiveBindingFrameStack);
		} else if (kind == IntrinsicKind::Array) {
			if (resolved->arguments.size() == 1)
				return {DataType::Kind::Constraint};
			Expression *sizeExpr =
				resolveInferenceBindingLayers(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext).expression;
			if (auto *size = std::get_if<std::int64_t>(&sizeExpr->literalValue)) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				typeRef.referencedKind = DataType::Kind::Array;
				typeRef.arraySize = static_cast<int>(*size);
				if (resolved->arguments.size() > 2) {
					DataType elemTypeRef = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
					if (elemTypeRef.kind == DataType::Kind::Constraint)
						return {DataType::Kind::Constraint};
					if (elemTypeRef.kind == DataType::Kind::Type)
						typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
				}
				return typeRef;
			}
		} else if (kind == IntrinsicKind::Vector) {
			Expression *sizeExpr =
				resolveInferenceBindingLayers(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext).expression;
			auto *size = std::get_if<std::int64_t>(&sizeExpr->literalValue);
			if (!size || *size < 1 || *size > std::numeric_limits<int>::max())
				return {};
			int vectorSize = static_cast<int>(*size);
			DataType typeRef;
			typeRef.kind = DataType::Kind::Type;
			typeRef.referencedKind = DataType::Kind::Vector;
			typeRef.arraySize = vectorSize;
			typeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 2) {
				DataType elemTypeRef;
				DataType elementExpressionType = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
				if (elementExpressionType.kind == DataType::Kind::Constraint)
					return {DataType::Kind::Constraint};
				if (elementExpressionType.kind == DataType::Kind::Type)
					elemTypeRef = elementExpressionType.toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (kind == IntrinsicKind::Matrix) {
			Expression *rowsExpr =
				resolveInferenceBindingLayers(resolved->arguments[1], effectiveBindingFrameStack, inferenceContext).expression;
			Expression *columnsExpr =
				resolveInferenceBindingLayers(resolved->arguments[2], effectiveBindingFrameStack, inferenceContext).expression;
			auto *rowsValue = std::get_if<std::int64_t>(&rowsExpr->literalValue);
			auto *columnsValue = std::get_if<std::int64_t>(&columnsExpr->literalValue);
			if (!rowsValue || !columnsValue)
				return {};
			if (*rowsValue > std::numeric_limits<int>::max() || *columnsValue > std::numeric_limits<int>::max())
				return {};
			int rows = static_cast<int>(*rowsValue);
			int columns = static_cast<int>(*columnsValue);
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
				DataType elementExpressionType = resolveKnownExpressionType(resolved->arguments[3], effectiveBindingFrameStack);
				if (elementExpressionType.kind == DataType::Kind::Constraint)
					return {DataType::Kind::Constraint};
				if (elementExpressionType.kind == DataType::Kind::Type)
					elemTypeRef = elementExpressionType.toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (kind == IntrinsicKind::AddPointerDepth) {
			DataType typeArgType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
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
			DataType typeRefType = resolveKnownExpressionType(resolved->arguments[1], effectiveBindingFrameStack);
			if (typeRefType.kind == DataType::Kind::Type && typeRefType.referencedKind == DataType::Kind::Array) {
				DataType arrayType = typeRefType.toReferencedType();
				if (arrayType.arraySize == static_cast<int>(resolved->arguments.size()) - 2) {
					DataType elementType =
						arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
					bool allDeduced = true;
					for (size_t i = 2; i < resolved->arguments.size(); i++) {
						DataType argType = resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack);
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
					DataType argumentType = resolveKnownExpressionType(resolved->arguments[i], effectiveBindingFrameStack);
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
				DataType valueType = resolveKnownExpressionType(resolved->arguments[2], effectiveBindingFrameStack);
				if (valueType.isDeduced())
					return targetType;
			}
		}
		if (kind == IntrinsicKind::Subject && resolved->subjectSetter && resolved->subjectSetter->arguments.size() > 1)
			return resolveKnownExpressionType(resolved->subjectSetter->arguments[1], effectiveBindingFrameStack);
	}
	return resolved->type;
}

static void recordUnknownTypeConstraintFailure(InferenceContext *inferenceContext, ParseContext &parseContext, Range range);

#include "type_resolution_class.inl"
#include "type_resolution_diagnostics.inl"
#include "type_resolution_overloads.inl"

static VariableReference *
currentInstantiationParameterDefinition(const InferenceContext &context, const std::string &parameterName) {
	if (!context.currentInstantiation || !context.currentInstantiation->body ||
		!context.currentInstantiation->parameterTypesByName.contains(parameterName))
		return nullptr;
	Section *ownerSection = context.currentInstantiation->body->sourceSection;
	requireCompilerInvariant(ownerSection != nullptr, "instantiated function body has no source section");
	auto definition = ownerSection->variableDefinitions.find(parameterName);
	requireCompilerInvariant(
		definition != ownerSection->variableDefinitions.end() && definition->second,
		"instantiation parameter has no declaration identity"
	);
	return normalizeBindingReference(definition->second);
}

static bool variableReferenceIsCurrentInstantiationParameter(const InferenceContext &context, VariableReference *reference) {
	if (!reference)
		return false;
	VariableReference *parameterDefinition = currentInstantiationParameterDefinition(context, reference->name);
	return parameterDefinition && normalizeBindingReference(reference) == parameterDefinition;
}

static void recordUnknownTypeConstraintFailure(InferenceContext *inferenceContext, ParseContext &parseContext, Range range) {
	requireCompilerInvariant(inferenceContext != nullptr, "type-constraint failure requires an inference context");
	inferenceContext->fail(unknownTypeConstraintDiagnostic(parseContext, range, range.subString), 1, false);
}

#include "type_resolution_values.inl"
