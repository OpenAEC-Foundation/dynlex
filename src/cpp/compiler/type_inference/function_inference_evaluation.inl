#include "llvm/IR/Module.h"

static bool mergeSelectBranchTypes(const DataType &trueTypeInput, const DataType &falseTypeInput, DataType &outType) {
	DataType trueType = concretizeClassType(trueTypeInput);
	DataType falseType = concretizeClassType(falseTypeInput);
	if (!trueType.isDeduced() || !falseType.isDeduced())
		return false;
	if (trueType == falseType) {
		outType = trueType;
		return true;
	}
	if (trueType.kind == DataType::Kind::Type && falseType.kind == DataType::Kind::Type) {
		outType = {DataType::Kind::Type};
		outType.referencedKind = DataType::Kind::Type;
		return true;
	}
	if (trueType.isNumeric() && falseType.isNumeric())
		return DataType::promoteArithmetic(trueType, falseType, outType);
	if (trueType.isVector() && falseType.isVector() && trueType.vectorSize() == falseType.vectorSize())
		return DataType::promoteArithmetic(trueType, falseType, outType);
	if (trueType.isMatrix() && falseType.isMatrix() && trueType.matrixRows() == falseType.matrixRows() &&
		trueType.matrixColumns() == falseType.matrixColumns()) {
		return DataType::promoteArithmetic(trueType, falseType, outType);
	}
	return false;
}

static void commitVariableTypeFromValue(Variable *var, Expression *valueExpr, const DataType &valueType) {
	if (!var)
		return;
	var->type = concretizeClassType(valueType);
	var->typeOriginRange = valueExpr ? valueExpr->range : Range();
	var->typeOriginFloatLiteralReplacement = makeFloatLiteralReplacement(valueExpr);
}

static bool isVariableAssignmentCompatible(const DataType &targetType, const DataType &valueType) {
	DataType concreteTargetType = targetType;
	DataType concreteValueType = valueType;
	if (!concreteTargetType.isDeduced() || !concreteValueType.isDeduced())
		return false;
	if (concreteTargetType == concreteValueType)
		return true;
	return concreteTargetType.kind == DataType::Kind::Int && concreteValueType.kind == DataType::Kind::Int &&
		   concreteTargetType.pointerDepth == 0 && concreteValueType.pointerDepth == 0;
}

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Expression *valueExpr, const DataType &valueType, ParseContext &parseContext);
static Diagnostic buildAssignmentTypeChangeDiagnostic(
	const std::string &name, const DataType &currentType, Range currentTypeOriginRange,
	const std::string &currentTypeOriginFloatLiteralReplacement, Expression *valueExpr, const DataType &valueType,
	ParseContext &parseContext
);
static int getRefinedClassInstantiationIndex(
	InferenceContext &context, ClassDefinition *classDef, int instIndex, size_t fieldIndex, const DataType &fieldType
);

static std::optional<double> parseCompileTimeNumericToken(std::string_view token) {
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
	try {
		return std::stod(std::string(token));
	} catch (...) {
		return std::nullopt;
	}
}

static std::int64_t compileTimeBitwiseNot(std::int64_t value) {
	return static_cast<std::int64_t>(~static_cast<std::uint64_t>(value));
}

static std::int64_t compileTimeShiftLeft(std::int64_t value, unsigned amount) {
	return static_cast<std::int64_t>(static_cast<std::uint64_t>(value) << amount);
}

static std::int64_t compileTimeShiftRight(std::int64_t value, unsigned amount) {
	if (amount == 0)
		return value;
	std::uint64_t bits = static_cast<std::uint64_t>(value);
	bits >>= amount;
	if (value < 0)
		bits |= (~std::uint64_t{0}) << (64 - amount);
	return static_cast<std::int64_t>(bits);
}

static std::int64_t normalizeSignedIntegerToType(std::int64_t value, const DataType &type) {
	requireCompilerInvariant(type.isInteger() && type.numericSize > 0, "integer type has no concrete width");
	unsigned bitCount = static_cast<unsigned>(type.numericSize * 8);
	requireCompilerInvariant(bitCount <= 64, "compile-time integer is wider than 64 bits");
	if (bitCount == 64)
		return value;
	std::uint64_t mask = (std::uint64_t{1} << bitCount) - 1;
	std::uint64_t truncated = static_cast<std::uint64_t>(value) & mask;
	std::uint64_t signBit = std::uint64_t{1} << (bitCount - 1);
	if (!(truncated & signBit))
		return static_cast<std::int64_t>(truncated);
	std::uint64_t magnitude = ((~truncated) & mask) + 1;
	return -static_cast<std::int64_t>(magnitude);
}

template <typename ReadArgumentValueFn, typename ReadStoredValueFn>
static CompileTimeValue evaluatePureIntrinsicCompileTimeValue(
	Expression *expr, ParseContext &parseContext, ReadArgumentValueFn &&readArgumentValue, ReadStoredValueFn &&readStoredValue
) {
	if (!expr)
		return {};
	auto requireArgument = [&](size_t index, std::string_view intrinsicName) -> Expression * {
		if (expr->arguments.size() <= index || !expr->arguments[index]) {
			crashCompilerBug(
				std::string("intrinsic '") + std::string(intrinsicName) +
				"' is missing an argument while reading compile-time value"
			);
		}
		return expr->arguments[index];
	};
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	if (kind == IntrinsicKind::BuildInfo) {
		CompileTimeValue keyValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (auto *key = std::get_if<std::string>(&keyValue))
			return currentBuildInfoValue(parseContext, *key);
		return {};
	}
	if (kind == IntrinsicKind::TargetIs) {
		CompileTimeValue targetValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (auto *targetName = std::get_if<std::string>(&targetValue))
			if (std::optional<bool> result = evaluateTargetIs(parseContext, *targetName))
				return *result;
		return {};
	}
	if (kind == IntrinsicKind::ShaderStageIs) {
		CompileTimeValue shaderStageValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (auto *shaderStageName = std::get_if<std::string>(&shaderStageValue))
			if (std::optional<bool> result = evaluateShaderStageIs(parseContext, *shaderStageName))
				return *result;
		return {};
	}
	if (kind == IntrinsicKind::SizeOf) {
		CompileTimeValue typeValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		auto *typeRef = std::get_if<TypeReferenceValue>(&typeValue);
		if (!typeRef || typeRef->type.kind != DataType::Kind::Type)
			return {};
		DataType valueType = typeRef->type.toReferencedType();
		if (valueType.kind == DataType::Kind::Class && valueType.classDefinition && valueType.classInstIndex < 0 &&
			!valueType.classDefinition->instantiations.empty()) {
			valueType.classInstIndex = 0;
		}
		requireCompilerInvariant(
			parseContext.llvmModule && parseContext.llvmContext, "size inference requires an initialized target layout"
		);
		return static_cast<double>(valueType.getByteSize(parseContext.llvmModule->getDataLayout(), *parseContext.llvmContext));
	}
	if (kind == IntrinsicKind::Select) {
		CompileTimeValue conditionValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		CompileTimeValue leftValue = readArgumentValue(requireArgument(2, expr->intrinsicName));
		CompileTimeValue rightValue = readArgumentValue(requireArgument(3, expr->intrinsicName));
		(void)leftValue;
		(void)rightValue;
		auto *condition = std::get_if<bool>(&conditionValue);
		if (!condition)
			return {};
		return readArgumentValue(requireArgument(*condition ? 2 : 3, expr->intrinsicName));
	}
	if (kind == IntrinsicKind::Cast && expr->arguments.size() > 2) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (!isCompileTimeKnown(value))
			return {};
		CompileTimeValue typeValue = readArgumentValue(requireArgument(2, expr->intrinsicName));
		auto *typeRef = std::get_if<TypeReferenceValue>(&typeValue);
		if (!typeRef || typeRef->type.kind != DataType::Kind::Type)
			return {};
		DataType targetType = typeRef->type.toReferencedType();
		if (targetType.kind == DataType::Kind::Bool) {
			std::optional<bool> truthy = compileTimeTruthiness(value);
			return truthy.has_value() ? CompileTimeValue(*truthy) : CompileTimeValue{};
		}
		if (!targetType.isNumeric())
			return {};
		std::optional<double> numericValue;
		if (const auto *number = std::get_if<double>(&value))
			numericValue = *number;
		else if (const auto *boolean = std::get_if<bool>(&value))
			numericValue = *boolean ? 1.0 : 0.0;
		if (!numericValue)
			return {};
		if (targetType.kind == DataType::Kind::Int) {
			if (std::trunc(*numericValue) != *numericValue || *numericValue < static_cast<double>(INT64_MIN) ||
				*numericValue >= -static_cast<double>(INT64_MIN)) {
				return {};
			}
			return static_cast<double>(normalizeSignedIntegerToType(static_cast<std::int64_t>(*numericValue), targetType));
		}
		return *numericValue;
	}
	if (kind == IntrinsicKind::Type || kind == IntrinsicKind::Fix || kind == IntrinsicKind::TypeOf ||
		kind == IntrinsicKind::Array || kind == IntrinsicKind::Vector || kind == IntrinsicKind::Matrix ||
		kind == IntrinsicKind::AddPointerDepth) {
		return readStoredValue(expr);
	}

	if (kind == IntrinsicKind::Not) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		auto *boolean = std::get_if<bool>(&value);
		return boolean ? CompileTimeValue(!*boolean) : CompileTimeValue{};
	}
	if (kind == IntrinsicKind::BitwiseNot) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		std::optional<std::int64_t> integerValue = getCompileTimeIntegerValue(value);
		return integerValue.has_value() ? CompileTimeValue(static_cast<double>(compileTimeBitwiseNot(*integerValue)))
										: CompileTimeValue{};
	}
	if (kind == IntrinsicKind::Negate) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		auto *number = std::get_if<double>(&value);
		return number ? CompileTimeValue(-*number) : CompileTimeValue{};
	}

	if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or || kind == IntrinsicKind::Equal ||
		kind == IntrinsicKind::NotEqual || kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr ||
		kind == IntrinsicKind::BitwiseXor || kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight ||
		kind == IntrinsicKind::Add || kind == IntrinsicKind::Subtract || kind == IntrinsicKind::Multiply ||
		kind == IntrinsicKind::Divide || kind == IntrinsicKind::Modulo || kind == IntrinsicKind::LessThan ||
		kind == IntrinsicKind::GreaterThan || kind == IntrinsicKind::LessThanOrEqual ||
		kind == IntrinsicKind::GreaterThanOrEqual) {
		CompileTimeValue leftValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		CompileTimeValue rightValue = readArgumentValue(requireArgument(2, expr->intrinsicName));
		if (!isCompileTimeKnown(leftValue) || !isCompileTimeKnown(rightValue))
			return {};

		if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
			auto *leftBool = std::get_if<bool>(&leftValue);
			auto *rightBool = std::get_if<bool>(&rightValue);
			if (!leftBool || !rightBool)
				return {};
			return kind == IntrinsicKind::And ? CompileTimeValue(*leftBool && *rightBool)
											  : CompileTimeValue(*leftBool || *rightBool);
		}

		if (kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) {
			bool result = false;
			if (auto *leftText = std::get_if<std::string>(&leftValue)) {
				if (auto *rightText = std::get_if<std::string>(&rightValue))
					result = *leftText == *rightText;
				else
					return {};
			} else if (auto *leftBool = std::get_if<bool>(&leftValue)) {
				if (auto *rightBool = std::get_if<bool>(&rightValue))
					result = *leftBool == *rightBool;
				else
					return {};
			} else {
				auto *leftNumber = std::get_if<double>(&leftValue);
				auto *rightNumber = std::get_if<double>(&rightValue);
				if (!leftNumber || !rightNumber)
					return {};
				result = *leftNumber == *rightNumber;
			}
			return kind == IntrinsicKind::Equal ? CompileTimeValue(result) : CompileTimeValue(!result);
		}

		if (kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor ||
			kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight) {
			std::optional<std::int64_t> leftInteger = getCompileTimeIntegerValue(leftValue);
			std::optional<std::int64_t> rightInteger = getCompileTimeIntegerValue(rightValue);
			if (!leftInteger.has_value() || !rightInteger.has_value())
				return {};
			if (kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight) {
				if (*rightInteger < 0 || *rightInteger >= 64)
					return {};
				unsigned shiftAmount = static_cast<unsigned>(*rightInteger);
				std::int64_t result = kind == IntrinsicKind::ShiftLeft ? compileTimeShiftLeft(*leftInteger, shiftAmount)
																	   : compileTimeShiftRight(*leftInteger, shiftAmount);
				return static_cast<double>(result);
			}
			std::uint64_t leftBits = static_cast<std::uint64_t>(*leftInteger);
			std::uint64_t rightBits = static_cast<std::uint64_t>(*rightInteger);
			std::uint64_t result = kind == IntrinsicKind::BitwiseAnd  ? (leftBits & rightBits)
								   : kind == IntrinsicKind::BitwiseOr ? (leftBits | rightBits)
																	  : (leftBits ^ rightBits);
			return static_cast<double>(static_cast<std::int64_t>(result));
		}

		auto *leftNumber = std::get_if<double>(&leftValue);
		auto *rightNumber = std::get_if<double>(&rightValue);
		if (!leftNumber || !rightNumber)
			return {};
		if (kind == IntrinsicKind::Add)
			return *leftNumber + *rightNumber;
		if (kind == IntrinsicKind::Subtract)
			return *leftNumber - *rightNumber;
		if (kind == IntrinsicKind::Multiply)
			return *leftNumber * *rightNumber;
		if (kind == IntrinsicKind::Divide)
			return *rightNumber == 0.0 ? CompileTimeValue{} : CompileTimeValue(*leftNumber / *rightNumber);
		if (kind == IntrinsicKind::Modulo)
			return *rightNumber == 0.0 ? CompileTimeValue{} : CompileTimeValue(std::fmod(*leftNumber, *rightNumber));
		if (kind == IntrinsicKind::LessThan)
			return *leftNumber < *rightNumber;
		if (kind == IntrinsicKind::GreaterThan)
			return *leftNumber > *rightNumber;
		if (kind == IntrinsicKind::LessThanOrEqual)
			return *leftNumber <= *rightNumber;
		if (kind == IntrinsicKind::GreaterThanOrEqual)
			return *leftNumber >= *rightNumber;
	}

	return {};
}

static CompileTimeValue
inferIntrinsicCompileTimeValue(Expression *expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	(void)bindingFrameStack;
	if (!expr)
		return {};
	if (intrinsicKind(expr->intrinsicName) == IntrinsicKind::Return && expr->arguments.size() > 1)
		return context.lookupExpressionValue(expr->arguments[1]);
	if (intrinsicKind(expr->intrinsicName) == IntrinsicKind::Subject && expr->subjectSetter &&
		expr->subjectSetter->arguments.size() > 1)
		return context.lookupExpressionValue(expr->subjectSetter->arguments[1]);
	return evaluatePureIntrinsicCompileTimeValue(expr, context.parseContext, [&](Expression *argumentExpression) {
		return context.lookupExpressionValue(argumentExpression);
	}, [&](Expression *expression) {
		return context.lookupExpressionValue(expression);
	});
}

static Variable *findExecutionSectionVariable(Section *section, const std::string &name) {
	if (!section)
		return nullptr;
	auto it = section->variables.find(name);
	return it != section->variables.end() ? it->second : nullptr;
}

static Expression::SectionOutcome sectionOutcomeForIntrinsic(
	IntrinsicKind kind, const CompileTimeValue &conditionValue = {}, const DataType &conditionType = {}
) {
	Expression::SectionOutcome outcome;
	outcome.conditionType = conditionType;
	switch (kind) {
	case IntrinsicKind::If:
		outcome.kind = Expression::SectionOutcome::Kind::Conditional;
		outcome.conditionValue = conditionValue;
		break;
	case IntrinsicKind::ElseIf:
		outcome.kind = Expression::SectionOutcome::Kind::AlternativeConditional;
		outcome.conditionValue = conditionValue;
		break;
	case IntrinsicKind::Else:
		outcome.kind = Expression::SectionOutcome::Kind::Alternative;
		break;
	case IntrinsicKind::LoopWhile:
		outcome.kind = Expression::SectionOutcome::Kind::Loop;
		outcome.conditionValue = conditionValue;
		break;
	case IntrinsicKind::Switch:
		outcome.kind = Expression::SectionOutcome::Kind::Switch;
		outcome.conditionValue = conditionValue;
		break;
	case IntrinsicKind::Case:
		outcome.kind = Expression::SectionOutcome::Kind::Case;
		outcome.conditionValue = conditionValue;
		break;
	case IntrinsicKind::DefaultCase:
		outcome.kind = Expression::SectionOutcome::Kind::DefaultCase;
		break;
	case IntrinsicKind::Return:
		outcome.kind = Expression::SectionOutcome::Kind::FunctionReturn;
		break;
	default:
		break;
	}
	return outcome;
}

struct PureExpressionExecutionResult {
	CompileTimeValue value;
	bool returned = false;
	Expression::SectionOutcome sectionOutcome;
};

struct PureSectionFlexBodyExecutionFrame {
	Section *definitionSection{};
	InstantiatedSectionBody *definitionBody{};
	Section *bodySection{};
	InstantiatedSectionBody *instantiatedBody{};
	BindingFrameStack callerBindings;
	bool bodyExecuted = false;
};

struct PureExecutionState {
	ParseContext &parseContext;
	InferenceContext *inferenceContext{};
	std::vector<std::pair<Section *, std::vector<CompileTimeValue>>> activeCalls;
	std::vector<InstantiatedSectionBody *> activeBodies;
	std::vector<PureSectionFlexBodyExecutionFrame> sectionFlexBodyFrames;
	std::vector<Section *> activeFlexDefinitionStack;
	std::vector<Section *> flexCallSiteSectionStack;

	PureExecutionState(ParseContext &context, InferenceContext *inference)
		: parseContext(context), inferenceContext(inference) {}
};

struct PureExecutionFrame {
	Instantiation *instantiation{};
	std::unordered_map<VariableReference *, CompileTimeValue> localValues;
};

static Expression *resolvePureExecutionBinding(
	Expression *expr, const BindingFrameStack &bindingFrameStack, BindingFrameStack *outBindingFrameStack = nullptr
) {
	if (outBindingFrameStack)
		*outBindingFrameStack = bindingFrameStack;
	if (expr && expr->kind == Expression::Kind::Pending && expr->patternReference) {
		auto &elements = expr->patternReference->patternElements;
		if (elements.empty())
			elements = getPatternElements(expr->patternReference->pattern.text);
		if (elements.size() == 1 &&
			(elements[0].type == PatternElement::Type::Variable || elements[0].type == PatternElement::Type::VariableLike)) {
			if (Expression *boundExpression = bindingFrameStack.lookup(elements[0].text))
				return boundExpression;
		}
	}
	return resolveVariableBindingAcrossFrames(expr, bindingFrameStack);
}

static PatternDefinition *selectedDefinitionForPureExecution(Expression *expr, const Instantiation *) {
	if (!expr || expr->kind != Expression::Kind::PatternCall)
		return nullptr;
	return expr->selectedPatternDefinition;
}

static void setPureExecutionLocalValue(PureExecutionFrame &frame, VariableReference *reference, const CompileTimeValue &value) {
	VariableReference *normalized = normalizeBindingReference(reference);
	if (!normalized)
		return;
	frame.localValues[normalized] = value;
}

static bool
lookupPureExecutionLocalValue(const PureExecutionFrame &frame, VariableReference *reference, CompileTimeValue &outValue) {
	VariableReference *normalized = normalizeBindingReference(reference);
	if (!normalized)
		return false;
	auto it = frame.localValues.find(normalized);
	if (it == frame.localValues.end())
		return false;
	outValue = it->second;
	return true;
}

template <typename ReadArgumentValueFn>
static bool collectKnownCallArgumentValues(
	Expression *expr, PatternDefinition *definition, ReadArgumentValueFn &&readArgumentValue,
	std::vector<std::pair<std::string, Expression *>> &outBindings,
	std::vector<std::pair<std::string, CompileTimeValue>> &outValues
) {
	outBindings.clear();
	outValues.clear();
	collectPatternCallBindingPairs(expr, definition, outBindings);
	outValues.reserve(outBindings.size());
	for (const auto &[parameterName, argumentExpression] : outBindings) {
		CompileTimeValue argumentValue = readArgumentValue(argumentExpression);
		if (!isCompileTimeKnown(argumentValue))
			return false;
		outValues.push_back({parameterName, argumentValue});
	}
	return true;
}

static std::vector<CompileTimeValue>
compileTimeArgumentValueVector(const std::vector<std::pair<std::string, CompileTimeValue>> &argumentValues) {
	std::vector<CompileTimeValue> values;
	values.reserve(argumentValues.size());
	for (const auto &[name, value] : argumentValues) {
		(void)name;
		values.push_back(value);
	}
	return values;
}

static PureExpressionExecutionResult evaluatePureExpression(
	Expression *expr, PureExecutionState &state, PureExecutionFrame &frame, const BindingFrameStack &bindingFrameStack
);

static PureExpressionExecutionResult executePureSection(
	PureExecutionState &state, PureExecutionFrame &frame, Section *section, InstantiatedSectionBody *body,
	Expression *openingExpression, const BindingFrameStack &bindingFrameStack
);

#include "function_inference_pure_flex.inl"

static CompileTimeValue pureExecutionImmediateValue(Expression *expr) {
	if (!expr)
		return {};
	switch (expr->kind) {
	case Expression::Kind::Literal:
		if (const auto *number = std::get_if<double>(&expr->literalValue))
			return *number;
		if (const auto *text = std::get_if<std::string>(&expr->literalValue))
			return *text;
		return {};
	case Expression::Kind::Variable:
		if (expr->variable) {
			if (std::optional<double> numericLiteral = parseCompileTimeNumericToken(expr->variable->name))
				return *numericLiteral;
		}
		return {};
	case Expression::Kind::Pending:
		if (expr->patternReference) {
			auto &elements = expr->patternReference->patternElements;
			if (elements.empty())
				elements = getPatternElements(expr->patternReference->pattern.text);
			if (elements.size() == 1 && (elements[0].type == PatternElement::Type::Variable ||
										 elements[0].type == PatternElement::Type::VariableLike)) {
				if (std::optional<double> numericLiteral = parseCompileTimeNumericToken(elements[0].text))
					return *numericLiteral;
			}
		}
		return {};
	default:
		return {};
	}
}

static CompileTimeValue pureExecutionStoredValue(Expression *expr, const PureExecutionState &state) {
	return state.inferenceContext ? state.inferenceContext->lookupExpressionValue(expr) : getExpressionCompileTimeValue(expr);
}

static CompileTimeValue executePureInstantiationReturnValue(
	PureExecutionState &state, Section *section, Instantiation &instantiation,
	const std::vector<std::pair<std::string, CompileTimeValue>> &argumentValues
) {
	if (!section || instantiation.purity != InstantiationPurity::Pure || instantiation.inferring ||
		instantiation.needsReinfer || !instantiation.valid || !instantiation.returnType.isDeduced())
		return {};
	std::vector<CompileTimeValue> argumentValueKey = compileTimeArgumentValueVector(argumentValues);
	auto cachedIt = instantiation.pureReturnValuesByArguments.find(argumentValueKey);
	if (cachedIt != instantiation.pureReturnValuesByArguments.end())
		return cachedIt->second;
	for (const auto &[activeSection, activeArguments] : state.activeCalls) {
		if (activeSection == section && activeArguments == argumentValueKey)
			return {};
	}
	state.activeCalls.push_back({section, argumentValueKey});
	PureExecutionFrame frame;
	frame.instantiation = &instantiation;
	requireCompilerInvariant(
		static_cast<bool>(instantiation.body), "pure execution encountered an instantiation without an inferred body"
	);
	requireCompilerInvariant(
		instantiation.body->sourceSection == section, "pure execution encountered an instantiation body for another section"
	);
	for (const auto &[parameterName, value] : argumentValues) {
		Variable *parameterVariable = findExecutionSectionVariable(section, parameterName);
		if (!parameterVariable || !parameterVariable->definition)
			continue;
		setPureExecutionLocalValue(frame, parameterVariable->definition, value);
	}
	PureExpressionExecutionResult executionResult{};
	bool hasImplicitDefinitionValue = false;
	section->forEachDefinitionBodySection([&](Section *bodySection) {
		InstantiatedSectionBody *activeBody =
			bodySection == section ? instantiation.body.get() : instantiation.body->bodyForChild(bodySection);
		requireCompilerInvariant(activeBody, "pure execution could not find the inferred definition body");
		executionResult = executePureSection(state, frame, bodySection, activeBody, nullptr, {});
		hasImplicitDefinitionValue = !executionResult.returned &&
									 (bodySection->type == SectionType::Get || bodySection->type == SectionType::Replacement) &&
									 isCompileTimeKnown(executionResult.value);
		return !executionResult.returned;
	});
	state.activeCalls.pop_back();
	if ((!executionResult.returned && !hasImplicitDefinitionValue) || !isCompileTimeKnown(executionResult.value))
		return {};
	instantiation.pureReturnValuesByArguments.emplace(std::move(argumentValueKey), executionResult.value);
	return executionResult.value;
}

static PureExpressionExecutionResult evaluatePureExpression(
	Expression *expr, PureExecutionState &state, PureExecutionFrame &frame, const BindingFrameStack &bindingFrameStack
) {
	if (!expr)
		return {};
	BindingFrameStack resolvedBindingFrameStack;
	Expression *resolvedExpression = resolvePureExecutionBinding(expr, bindingFrameStack, &resolvedBindingFrameStack);
	if (resolvedExpression && resolvedExpression != expr)
		return evaluatePureExpression(resolvedExpression, state, frame, resolvedBindingFrameStack);

	CompileTimeValue immediateValue = pureExecutionImmediateValue(expr);
	if (isCompileTimeKnown(immediateValue))
		return {immediateValue, false, {}};

	switch (expr->kind) {
	case Expression::Kind::Literal:
	case Expression::Kind::TypedPlaceholder:
	case Expression::Kind::Pending:
	case Expression::Kind::ArrayLiteral:
		return {pureExecutionStoredValue(expr, state), false, {}};

	case Expression::Kind::Variable: {
		CompileTimeValue localValue{};
		if (lookupPureExecutionLocalValue(frame, expr->variable, localValue))
			return {localValue, false, {}};
		return {pureExecutionStoredValue(expr, state), false, {}};
	}

	case Expression::Kind::IntrinsicCall: {
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		if (kind == IntrinsicKind::ExecuteBody)
			return executePureSectionFlexCallerBody(expr, state, frame);
		if (kind == IntrinsicKind::Return) {
			if (expr->arguments.size() <= 1)
				return {{}, true, {}};
			PureExpressionExecutionResult valueResult =
				evaluatePureExpression(expr->arguments[1], state, frame, bindingFrameStack);
			valueResult.returned = true;
			return valueResult;
		}
		if (kind == IntrinsicKind::Store) {
			if (expr->arguments.size() <= 2)
				crashCompilerBug("store intrinsic missing destination or value during pure execution");
			BindingFrameStack destinationBindingFrameStack;
			Expression *destinationExpression =
				resolvePureExecutionBinding(expr->arguments[1], bindingFrameStack, &destinationBindingFrameStack);
			PureExpressionExecutionResult valueResult =
				evaluatePureExpression(expr->arguments[2], state, frame, bindingFrameStack);
			if (valueResult.returned)
				crashCompilerBug("store value evaluation returned unexpectedly during pure execution");
			if (!destinationExpression || destinationExpression->kind != Expression::Kind::Variable ||
				!destinationExpression->variable) {
				return {};
			}
			setPureExecutionLocalValue(frame, destinationExpression->variable, valueResult.value);
			return {};
		}

		std::unordered_map<Expression *, CompileTimeValue> argumentValues;
		for (size_t i = 1; i < expr->arguments.size(); i++) {
			PureExpressionExecutionResult argumentResult =
				evaluatePureExpression(expr->arguments[i], state, frame, bindingFrameStack);
			if (argumentResult.returned)
				crashCompilerBug("intrinsic argument evaluation returned unexpectedly during pure execution");
			argumentValues[expr->arguments[i]] = argumentResult.value;
		}
		PureExpressionExecutionResult result{
			evaluatePureIntrinsicCompileTimeValue(
				expr, state.parseContext,
				[&](Expression *argumentExpression) {
			auto it = argumentValues.find(argumentExpression);
			return it != argumentValues.end() ? it->second : CompileTimeValue{};
		},
				[&](Expression *expression) {
			return pureExecutionStoredValue(expression, state);
		}
			),
			false,
			{},
		};
		CompileTimeValue conditionValue;
		DataType conditionType;
		if ((kind == IntrinsicKind::If || kind == IntrinsicKind::ElseIf || kind == IntrinsicKind::LoopWhile ||
			 kind == IntrinsicKind::Switch || kind == IntrinsicKind::Case) &&
			expr->arguments.size() > 1) {
			conditionValue = argumentValues[expr->arguments[1]];
			conditionType = expr->arguments[1]->type;
		}
		result.sectionOutcome = sectionOutcomeForIntrinsic(kind, conditionValue, conditionType);
		return result;
	}

	case Expression::Kind::PatternCall: {
		PatternDefinition *selectedDefinition = selectedDefinitionForPureExecution(expr, frame.instantiation);
		if (!selectedDefinition || !selectedDefinition->section)
			crashCompilerBug("pure execution encountered a pattern call without a selected definition");
		Section *matchedSection = selectedDefinition->section;
		if (matchedSection->type == SectionType::Class && !matchedSection->isFlex)
			return {pureExecutionStoredValue(expr, state), false, {}};
		if (matchedSection->isFlex)
			return executePureFlexCall(expr, selectedDefinition, state, frame, bindingFrameStack);
		std::vector<std::pair<std::string, Expression *>> parameterBindings;
		std::vector<std::pair<std::string, CompileTimeValue>> argumentValues;
		if (!collectKnownCallArgumentValues(expr, selectedDefinition, [&](Expression *argumentExpression) {
			return evaluatePureExpression(argumentExpression, state, frame, bindingFrameStack).value;
		}, parameterBindings, argumentValues)) {
			return {};
		}
		Instantiation *selectedInstantiation = expr->selectedInstantiation;
		if (!selectedInstantiation)
			crashCompilerBug("pure execution encountered a non-flex call without its selected instantiation");
		return {executePureInstantiationReturnValue(state, matchedSection, *selectedInstantiation, argumentValues), false, {}};
	}
	}

	crashCompilerBug("unhandled expression kind during pure execution");
}

static PureExpressionExecutionResult executePureSectionBodyOnce(
	PureExecutionState &state, PureExecutionFrame &frame, Section *section, InstantiatedSectionBody *body,
	const BindingFrameStack &bindingFrameStack
) {
	requireCompilerInvariant(body && body->sourceSection == section, "pure execution received the wrong inferred section body");
	ScopedPureActiveBody activeBody(state, body);
	auto executeOpenedSection = [&](CodeLine *line, Expression *lineExpression) -> PureExpressionExecutionResult {
		if (!line || !line->sectionOpening || dynamic_cast<DefinitionSection *>(line->sectionOpening))
			return {};
		if (lineExpression && lineExpression->sectionBodyInferred)
			return {};
		InstantiatedSectionBody *openedBody = body->bodyForChild(line->sectionOpening);
		requireCompilerInvariant(openedBody, "pure execution could not find an inferred child section body");
		return executePureSection(state, frame, line->sectionOpening, openedBody, lineExpression, bindingFrameStack);
	};

	PureExpressionExecutionResult lastResult{};
	for (size_t i = 0; i < section->codeLines.size(); i++) {
		CodeLine *line = section->codeLines[i];
		Expression *lineExpression = body->lineExpression(i);
		if (lineExpression && line->sectionOpening && !dynamic_cast<DefinitionSection *>(line->sectionOpening) &&
			lineExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Conditional) {
			size_t chainEnd = i;
			while (chainEnd + 1 < section->codeLines.size()) {
				CodeLine *next = section->codeLines[chainEnd + 1];
				if (!next->sectionOpening || dynamic_cast<DefinitionSection *>(next->sectionOpening))
					break;
				Expression *nextExpression = body->lineExpression(chainEnd + 1);
				if (!nextExpression)
					break;
				Expression::SectionOutcome::Kind nextKind = nextExpression->sectionOutcome.kind;
				if (nextKind != Expression::SectionOutcome::Kind::AlternativeConditional &&
					nextKind != Expression::SectionOutcome::Kind::Alternative) {
					break;
				}
				chainEnd++;
			}

			std::optional<size_t> selectedBranch;
			for (size_t k = i; k <= chainEnd; k++) {
				Expression *branchExpression = body->lineExpression(k);
				PureExpressionExecutionResult headerResult =
					evaluatePureExpression(branchExpression, state, frame, bindingFrameStack);
				if (headerResult.returned)
					crashCompilerBug("branch header returned unexpectedly during pure execution");
				Expression::SectionOutcome::Kind branchKind = headerResult.sectionOutcome.kind;
				if (branchKind == Expression::SectionOutcome::Kind::Alternative) {
					selectedBranch = k;
					break;
				}
				requireCompilerInvariant(
					branchKind == (k == i ? Expression::SectionOutcome::Kind::Conditional
										  : Expression::SectionOutcome::Kind::AlternativeConditional),
					"pure branch execution encountered invalid section-outcome metadata"
				);
				auto *condition = std::get_if<bool>(&headerResult.sectionOutcome.conditionValue);
				if (!condition)
					return {};
				if (*condition) {
					selectedBranch = k;
					break;
				}
			}
			if (selectedBranch.has_value()) {
				PureExpressionExecutionResult branchResult =
					executeOpenedSection(section->codeLines[*selectedBranch], body->lineExpression(*selectedBranch));
				if (branchResult.returned)
					return branchResult;
			}
			i = chainEnd;
			continue;
		}

		if (lineExpression) {
			PureExpressionExecutionResult lineResult = evaluatePureExpression(lineExpression, state, frame, bindingFrameStack);
			if (lineResult.returned)
				return lineResult;
			lastResult = lineResult;
		}

		PureExpressionExecutionResult openedSectionResult = executeOpenedSection(line, lineExpression);
		if (openedSectionResult.returned)
			return openedSectionResult;
	}
	return lastResult;
}

static PureExpressionExecutionResult executePureSection(
	PureExecutionState &state, PureExecutionFrame &frame, Section *section, InstantiatedSectionBody *body,
	Expression *openingExpression, const BindingFrameStack &bindingFrameStack
) {
	if (!section)
		return {};
	requireCompilerInvariant(body && body->sourceSection == section, "pure execution received the wrong inferred section body");
	if (openingExpression && openingExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Switch) {
		PureExpressionExecutionResult headerResult = evaluatePureExpression(openingExpression, state, frame, bindingFrameStack);
		if (headerResult.returned)
			crashCompilerBug("switch selector evaluation returned unexpectedly during pure execution");
		std::optional<std::int64_t> selector = getCompileTimeIntegerValue(headerResult.sectionOutcome.conditionValue);
		if (!selector)
			return {};
		DataType selectorType = headerResult.sectionOutcome.conditionType;
		requireCompilerInvariant(selectorType.isInteger(), "pure switch selector has no integer type");
		selector = normalizeSignedIntegerToType(*selector, selectorType);
		ScopedPureActiveBody activeBody(state, body);
		std::optional<size_t> defaultLineIndex;
		for (size_t index = 0; index < section->codeLines.size(); index++) {
			Expression *caseExpression = body->lineExpression(index);
			PureExpressionExecutionResult caseResult = evaluatePureExpression(caseExpression, state, frame, bindingFrameStack);
			if (caseResult.returned)
				crashCompilerBug("switch case evaluation returned unexpectedly during pure execution");
			if (caseResult.sectionOutcome.kind == Expression::SectionOutcome::Kind::DefaultCase) {
				defaultLineIndex = index;
				continue;
			}
			requireCompilerInvariant(
				caseResult.sectionOutcome.kind == Expression::SectionOutcome::Kind::Case,
				"pure switch execution encountered invalid case metadata"
			);
			std::optional<std::int64_t> caseValue = getCompileTimeIntegerValue(caseResult.sectionOutcome.conditionValue);
			if (!caseValue || normalizeSignedIntegerToType(*caseValue, selectorType) != selector)
				continue;
			CodeLine *caseLine = section->codeLines[index];
			InstantiatedSectionBody *caseBody = body->bodyForChild(caseLine->sectionOpening);
			return executePureSection(state, frame, caseLine->sectionOpening, caseBody, caseExpression, bindingFrameStack);
		}
		if (!defaultLineIndex)
			return {};
		CodeLine *defaultLine = section->codeLines[*defaultLineIndex];
		InstantiatedSectionBody *defaultBody = body->bodyForChild(defaultLine->sectionOpening);
		return executePureSection(
			state, frame, defaultLine->sectionOpening, defaultBody, body->lineExpression(*defaultLineIndex), bindingFrameStack
		);
	}
	if (!openingExpression || openingExpression->sectionOutcome.kind != Expression::SectionOutcome::Kind::Loop)
		return executePureSectionBodyOnce(state, frame, section, body, bindingFrameStack);
	constexpr size_t maxPureLoopIterations = 100000;
	for (size_t iteration = 0; iteration < maxPureLoopIterations; iteration++) {
		PureExpressionExecutionResult headerResult = evaluatePureExpression(openingExpression, state, frame, bindingFrameStack);
		if (headerResult.returned)
			crashCompilerBug("loop condition evaluation returned unexpectedly during pure execution");
		requireCompilerInvariant(
			headerResult.sectionOutcome.kind == Expression::SectionOutcome::Kind::Loop,
			"pure loop execution encountered invalid section-outcome metadata"
		);
		auto *condition = std::get_if<bool>(&headerResult.sectionOutcome.conditionValue);
		if (!condition)
			return {};
		if (!*condition)
			return {};
		PureExpressionExecutionResult bodyResult = executePureSectionBodyOnce(state, frame, section, body, bindingFrameStack);
		if (bodyResult.returned)
			return bodyResult;
	}
	return {};
}

static CompileTimeValue evaluatePureFunctionCallReturnValue(
	Expression *expr, PatternDefinition *definition, Section *section, Instantiation &instantiation, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack
) {
	if (!expr || !definition || !section || instantiation.purity != InstantiationPurity::Pure || instantiation.inferring ||
		!instantiation.valid || instantiation.needsReinfer || !instantiation.returnType.isDeduced())
		return {};
	std::vector<std::pair<std::string, Expression *>> parameterBindings;
	std::vector<std::pair<std::string, CompileTimeValue>> argumentValues;
	if (!collectKnownCallArgumentValues(expr, definition, [&](Expression *argumentExpression) {
		return resolveStoredCompileTimeValue(argumentExpression, bindingFrameStack, &context);
	}, parameterBindings, argumentValues)) {
		return {};
	}
	PureExecutionState executionState{context.parseContext, &context};
	return executePureInstantiationReturnValue(executionState, section, instantiation, argumentValues);
}

static Instantiation *ensureCallableFunctionInstantiationInferred(
	PatternDefinition *definition, InferenceContext &context, const Range &referenceRange
) {
	if (!definition || !definition->section || definition->section->type != SectionType::Function ||
		definition->section->isFlex) {
		context.setTypeFailure("function reference requires a non-flex function");
		return nullptr;
	}
	if (context.parseContext.options.emitSPIRV) {
		context.setTypeFailure("function references are unavailable for SPIR-V targets");
		return nullptr;
	}
	std::vector<std::pair<std::string, DataType>> parameters;
	collectCallableFunctionParameters(definition, parameters);
	std::vector<std::unique_ptr<Expression>> ownedArguments;
	std::vector<std::pair<std::string, Expression *>> parameterBindings;
	std::vector<DataType> argumentTypes;
	ownedArguments.reserve(parameters.size());
	parameterBindings.reserve(parameters.size());
	argumentTypes.reserve(parameters.size());
	for (const auto &[parameterName, parameterType] : parameters) {
		if (!parameterType.isDeduced()) {
			context.setTypeFailure("function reference requires a concrete type for parameter '" + parameterName + "'");
			return nullptr;
		}
		if (parameterType.isMetaType() || parameterType.kind == DataType::Kind::Void) {
			context.setTypeFailure("function reference parameter '" + parameterName + "' is not a runtime value");
			return nullptr;
		}
		DefinitionPatternElement *parameterElement = findParameterElement(definition->patternElements, parameterName);
		requireCompilerInvariant(parameterElement != nullptr, "callable parameter has no definition element");
		if (patternParameterRequiresCompileTimeValue(*parameterElement, parameterType)) {
			context.setTypeFailure("function reference cannot bind fixed parameter '" + parameterName + "'");
			return nullptr;
		}
		auto argument = std::make_unique<Expression>();
		argument->kind = Expression::Kind::TypedPlaceholder;
		argument->type = parameterType;
		argument->range = referenceRange;
		parameterBindings.push_back({parameterName, argument.get()});
		argumentTypes.push_back(parameterType);
		ownedArguments.push_back(std::move(argument));
	}
	if (!ensureSectionInstantiationInferred(
			context.parseContext, definition->section, definition, parameterBindings, argumentTypes, {},
			context.currentInstantiation, &context
		)) {
		return nullptr;
	}
	InstantiationKey key{.argumentTypes = argumentTypes, .compileTimeParameters = {}};
	auto instantiation = definition->section->instantiations.find(key);
	requireCompilerInvariant(
		instantiation != definition->section->instantiations.end(),
		"callable inference did not retain its selected instantiation"
	);
	if (context.trial)
		context.trialCallableInstantiations[definition] = &instantiation->second;
	else
		definition->callableInstantiation = &instantiation->second;
	return &instantiation->second;
}

static CompileTimeValue
inferVariableCompileTimeValue(Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack) {
	if (!expr || !expr->variable)
		return {};
	CompileTimeValue computedValue{};
	Expression *boundExpression = flexBindingFrameStack.lookup(expr->variable);
	if (boundExpression && boundExpression != expr) {
		computedValue = resolveStoredCompileTimeValue(boundExpression, flexBindingFrameStack, &context);
		if (isCompileTimeKnown(computedValue)) {
			context.setExpressionValue(boundExpression, computedValue);
		}
		if (!isCompileTimeKnown(computedValue) && boundExpression->kind == Expression::Kind::Variable &&
			boundExpression->variable) {
			Section *boundSection = boundExpression->range.line ? boundExpression->range.line->section : nullptr;
			Variable *boundVariable = boundSection ? boundSection->findVariable(boundExpression->variable->name) : nullptr;
			if (!boundVariable) {
				computedValue = boundExpression->variable->name;
				context.setExpressionValue(boundExpression, computedValue);
			}
		}
	}
	if (!isCompileTimeKnown(computedValue))
		computedValue = context.lookupKnownConstant(expr->variable);
	if (!isCompileTimeKnown(computedValue) && context.currentInstantiation) {
		if (context.currentInstantiation->requiredCompileTimeParameters.contains(expr->variable->name)) {
			auto it = context.currentInstantiation->constantParameterValues.find(expr->variable->name);
			if (it != context.currentInstantiation->constantParameterValues.end())
				computedValue = it->second;
		}
	}
	if (!isCompileTimeKnown(computedValue)) {
		if (std::optional<double> numericToken = parseCompileTimeNumericToken(expr->variable->name))
			computedValue = *numericToken;
	}
	if (!isCompileTimeKnown(computedValue) && expr->type.kind == DataType::Kind::Type)
		computedValue = TypeReferenceValue::exact(expr->type);
	return computedValue;
}

#include "intrinsics/store_inference.inl"
#include "variable_flow.inl"
