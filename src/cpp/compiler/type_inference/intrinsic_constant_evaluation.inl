#include "compileTimeFloatingValue.h"
#include "function_inference_float_evaluation.inl"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"

static CompileTimePointer normalizeCompileTimePointer(std::uint64_t address, const ParseContext &context) {
	requireCompilerInvariant(context.llvmModule != nullptr, "constant pointer evaluation requires the target layout");
	unsigned width = context.llvmModule->getDataLayout().getPointerSizeInBits();
	requireCompilerInvariant(width > 0 && width <= 64, "unsupported constant pointer width");
	return {width == 64 ? address : address & ((std::uint64_t{1} << width) - 1)};
}

template <typename ReadArgumentValueFn, typename ReadStoredValueFn>
static CompileTimeValue evaluatePureIntrinsicCompileTimeValue(
	Expression *expr, ParseContext &parseContext, ReadArgumentValueFn &&readArgumentValueFn, ReadStoredValueFn &&readStoredValue
) {
	if (!expr)
		return {};
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	auto readArgumentValue = [&](Expression *argument) {
		return readArgumentValueFn(argument);
	};
	auto requireArgument = [&](size_t index, std::string_view intrinsicName) -> Expression * {
		if (expr->arguments.size() <= index || !expr->arguments[index]) {
			crashCompilerBug(
				std::string("intrinsic '") + std::string(intrinsicName) +
				"' is missing an argument while reading compile-time value"
			);
		}
		return expr->arguments[index];
	};
	if (kind == IntrinsicKind::Function || kind == IntrinsicKind::DeferredBody) {
		requireCompilerInvariant(
			expr->selectedCallableDefinition && expr->selectedCallablePathIndex && expr->selectedInstantiation,
			"constant callable has no inferred function identity"
		);
		return CompileTimeFunction{
			expr->selectedCallableDefinition, *expr->selectedCallablePathIndex, expr->selectedInstantiation
		};
	}
	if (kind == IntrinsicKind::Construct && expr->arguments.size() == 2) {
		if (expr->type.isAddress())
			return CompileTimePointer{};
		if (expr->type.isInteger())
			return compileTimeIntegerFromBits(llvm::APInt(expr->type.numericSize * 8, 0), expr->type);
		if (expr->type.kind == DataType::Kind::Bool)
			return false;
		if (expr->type.kind == DataType::Kind::Float)
			return 0.0;
	}
	if (kind == IntrinsicKind::ExtractElement && expr->arguments[1]->type.kind == DataType::Kind::ArgumentPack) {
		CompileTimeValue aggregate = readArgumentValue(expr->arguments[1]);
		const auto *pack = std::get_if<ArgumentPackValue>(&aggregate);
		auto index = getCompileTimeIntegerValue(readArgumentValue(expr->arguments[2]));
		requireCompilerInvariant(
			pack && index && *index >= 0 && static_cast<size_t>(*index) < pack->elements.size(),
			"validated argument pack extraction lost its compile-time operands"
		);
		return pack->elements[static_cast<size_t>(*index)];
	}
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
		if (!valueType.isConcrete())
			return {};
		requireCompilerInvariant(
			parseContext.llvmModule && parseContext.llvmContext, "size inference requires an initialized target layout"
		);
		return static_cast<std::int64_t>(
			valueType.getByteSize(parseContext.llvmModule->getDataLayout(), *parseContext.llvmContext)
		);
	}
	if (auto conditional = conditionalOperands(kind); conditional && !expr->type.isMetaType()) {
		CompileTimeValue conditionValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		const bool *condition = std::get_if<bool>(&conditionValue);
		if (!condition)
			return {};
		size_t selected = conditional->selected(*condition);
		return selected ? readArgumentValue(requireArgument(selected, expr->intrinsicName)) : CompileTimeValue(*condition);
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
		if (const auto *function = std::get_if<CompileTimeFunction>(&value))
			return targetType.isAddress() ? CompileTimeValue(*function) : CompileTimeValue{};
		if (const auto *pointer = std::get_if<CompileTimePointer>(&value)) {
			if (targetType.isAddress())
				return *pointer;
			if (targetType.isInteger())
				return compileTimeIntegerFromBits(llvm::APInt(64, pointer->address), targetType);
			return {};
		}
		if (targetType.isAddress()) {
			unsigned width = parseContext.llvmModule->getDataLayout().getPointerSizeInBits();
			auto integer = getCompileTimeIntegerBits(value, expr->arguments[1]->type.numericSize * 8);
			return integer
					   ? CompileTimeValue(normalizeCompileTimePointer(integer->zextOrTrunc(width).getZExtValue(), parseContext))
					   : CompileTimeValue{};
		}
		if (targetType.kind == DataType::Kind::Bool) {
			std::optional<bool> truthy = compileTimeTruthiness(value);
			return truthy.has_value() ? CompileTimeValue(*truthy) : CompileTimeValue{};
		}
		if (!targetType.isNumeric())
			return {};
		if (const auto *number = std::get_if<double>(&value); number && targetType.isInteger()) {
			llvm::APSInt integer(targetType.numericSize * 8, targetType.isUnsignedInteger());
			bool exact;
			auto status = llvm::APFloat(*number).convertToInteger(integer, llvm::APFloat::rmTowardZero, &exact);
			if (status & llvm::APFloat::opInvalidOp)
				return {};
			return compileTimeIntegerFromBits(integer, targetType);
		}
		if (targetType.isInteger()) {
			auto integerValue = getCompileTimeIntegerBits(value, targetType.numericSize * 8);
			return integerValue ? compileTimeIntegerFromBits(*integerValue, targetType) : CompileTimeValue{};
		}
		if (const auto *boolean = std::get_if<bool>(&value))
			return *boolean ? 1.0 : 0.0;
		std::optional<double> numericValue = getCompileTimeFloatingValue(value, targetType.numericSize * 8);
		return numericValue ? CompileTimeValue(*numericValue) : CompileTimeValue{};
	}
	if (kind == IntrinsicKind::FunctionType || kind == IntrinsicKind::FunctionResultType ||
		kind == IntrinsicKind::FunctionParameterType || kind == IntrinsicKind::Type || kind == IntrinsicKind::Fix ||
		kind == IntrinsicKind::TypeOf || kind == IntrinsicKind::ElementType || kind == IntrinsicKind::PromoteArithmeticType ||
		kind == IntrinsicKind::Number || kind == IntrinsicKind::NumberRule || kind == IntrinsicKind::TypeExtent ||
		kind == IntrinsicKind::Array || kind == IntrinsicKind::Vector || kind == IntrinsicKind::Matrix ||
		kind == IntrinsicKind::AddPointerDepth || (kind == IntrinsicKind::And && expr->type.isMetaType())) {
		return readStoredValue(expr);
	}

	if (kind == IntrinsicKind::Not) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		auto *boolean = std::get_if<bool>(&value);
		return boolean ? CompileTimeValue(!*boolean) : CompileTimeValue{};
	}
	if (kind == IntrinsicKind::Abs || isRoundingIntrinsicKind(kind)) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		DataType elementType = expr->type.numericElementType();
		if (elementType.isUnsignedInteger() || (elementType.isInteger() && isRoundingIntrinsicKind(kind)))
			return value;
		if (elementType.isInteger()) {
			auto integer = getCompileTimeIntegerBits(value, elementType.numericSize * 8);
			return integer ? compileTimeIntegerFromBits(integer->abs(), elementType) : CompileTimeValue{};
		}
		const auto *number = std::get_if<double>(&value);
		if (!number)
			return {};
		double operand = elementType.numericSize == 4 ? static_cast<float>(*number) : *number;
		switch (kind) {
		case IntrinsicKind::Abs:
			return std::fabs(operand);
		case IntrinsicKind::Floor:
			return std::floor(operand);
		case IntrinsicKind::Ceil:
			return std::ceil(operand);
		case IntrinsicKind::Round:
			return std::round(operand);
		default:
			crashCompilerBug("unexpected rounding intrinsic during constant evaluation");
		}
	}
	if (kind == IntrinsicKind::BitwiseNot) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		auto integer = getCompileTimeIntegerBits(value, expr->type.numericSize * 8);
		return integer ? compileTimeIntegerFromBits(~*integer, expr->type) : CompileTimeValue{};
	}
	if (kind == IntrinsicKind::Negate) {
		CompileTimeValue value = readArgumentValue(requireArgument(1, expr->intrinsicName));
		if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
			return std::numeric_limits<std::int64_t>::min();
		if (expr->type.isInteger()) {
			auto integer = getCompileTimeIntegerBits(value, expr->type.numericSize * 8);
			return integer ? compileTimeIntegerFromBits(-*integer, expr->type) : CompileTimeValue{};
		}
		if (const auto *number = std::get_if<double>(&value))
			return -*number;
		return {};
	}

	if (kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual || kind == IntrinsicKind::BitwiseAnd ||
		kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor || kind == IntrinsicKind::ShiftLeft ||
		kind == IntrinsicKind::ShiftRight || kind == IntrinsicKind::Add || kind == IntrinsicKind::Subtract ||
		kind == IntrinsicKind::Multiply || kind == IntrinsicKind::Divide || kind == IntrinsicKind::Modulo ||
		kind == IntrinsicKind::LessThan || kind == IntrinsicKind::GreaterThan || kind == IntrinsicKind::LessThanOrEqual ||
		kind == IntrinsicKind::GreaterThanOrEqual || kind == IntrinsicKind::Min || kind == IntrinsicKind::Max) {
		CompileTimeValue leftValue = readArgumentValue(requireArgument(1, expr->intrinsicName));
		CompileTimeValue rightValue = readArgumentValue(requireArgument(2, expr->intrinsicName));
		if (!isCompileTimeKnown(leftValue) || !isCompileTimeKnown(rightValue))
			return {};

		DataType leftType = expr->arguments[1]->type;
		DataType rightType = expr->arguments[2]->type;
		if ((kind == IntrinsicKind::Add || kind == IntrinsicKind::Subtract) && expr->type.isPointer()) {
			const auto *pointer = std::get_if<CompileTimePointer>(leftType.isPointer() ? &leftValue : &rightValue);
			unsigned width = parseContext.llvmModule->getDataLayout().getPointerSizeInBits();
			auto offset = getCompileTimeIntegerBits(leftType.isPointer() ? rightValue : leftValue, width);
			DataType element = expr->type.dereferenced();
			if (!pointer || !offset || !element.isConcrete())
				return {};
			std::uint64_t bytes = offset->getZExtValue() *
								  element.getByteSize(parseContext.llvmModule->getDataLayout(), *parseContext.llvmContext);
			return normalizeCompileTimePointer(
				kind == IntrinsicKind::Add ? pointer->address + bytes : pointer->address - bytes, parseContext
			);
		}
		DataType integerType;
		bool integerOperation =
			leftType.isInteger() && rightType.isInteger() &&
			((kind == IntrinsicKind::BitwiseAnd || kind == IntrinsicKind::BitwiseOr || kind == IntrinsicKind::BitwiseXor ||
			  kind == IntrinsicKind::ShiftLeft || kind == IntrinsicKind::ShiftRight)
				 ? DataType::promoteBitwise(leftType, rightType, integerType)
				 : DataType::promoteArithmetic(leftType, rightType, integerType));
		if (integerOperation)
			return evaluateIntegerBinaryIntrinsic(kind, leftValue, rightValue, integerType);
		DataType floatingType;
		if (DataType::promoteArithmetic(leftType, rightType, floatingType) && floatingType.kind == DataType::Kind::Float)
			return evaluateFloatingBinaryIntrinsic(kind, leftValue, rightValue, floatingType);

		if (kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) {
			if (const auto *left = std::get_if<CompileTimePointer>(&leftValue)) {
				const auto *right = std::get_if<CompileTimePointer>(&rightValue);
				if (!right)
					return {};
				return kind == IntrinsicKind::Equal ? left->address == right->address : left->address != right->address;
			}
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
				return {};
			}
			return kind == IntrinsicKind::Equal ? CompileTimeValue(result) : CompileTimeValue(!result);
		}
	}

	for (size_t index = 1; index < expr->arguments.size(); index++)
		(void)readArgumentValue(requireArgument(index, expr->intrinsicName));
	return {};
}
