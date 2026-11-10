#pragma once

static CompileTimeValue evaluateIntegerBinaryIntrinsic(
	IntrinsicKind kind, const CompileTimeValue &leftValue, const CompileTimeValue &rightValue, const DataType &integerType
) {
	unsigned width = static_cast<unsigned>(integerType.numericSize * 8);
	auto left = getCompileTimeIntegerBits(leftValue, width);
	auto right = getCompileTimeIntegerBits(rightValue, width);
	if (!left || !right)
		return {};
	bool isUnsigned = integerType.isUnsignedInteger();
	auto result = [&](const llvm::APInt &bits) {
		return compileTimeIntegerFromBits(bits, integerType);
	};
	bool less = isUnsigned ? left->ult(*right) : left->slt(*right);
	bool greater = isUnsigned ? left->ugt(*right) : left->sgt(*right);
	switch (kind) {
	case IntrinsicKind::Equal:
		return *left == *right;
	case IntrinsicKind::NotEqual:
		return *left != *right;
	case IntrinsicKind::LessThan:
		return less;
	case IntrinsicKind::GreaterThan:
		return greater;
	case IntrinsicKind::LessThanOrEqual:
		return !greater;
	case IntrinsicKind::GreaterThanOrEqual:
		return !less;
	case IntrinsicKind::Min:
		return result(less ? *left : *right);
	case IntrinsicKind::Max:
		return result(greater ? *left : *right);
	case IntrinsicKind::BitwiseAnd:
		return result(*left & *right);
	case IntrinsicKind::BitwiseOr:
		return result(*left | *right);
	case IntrinsicKind::BitwiseXor:
		return result(*left ^ *right);
	case IntrinsicKind::ShiftLeft:
	case IntrinsicKind::ShiftRight:
		if (right->uge(width))
			return {}; // LLVM shifts by the operand width or more are poison.
		if (kind == IntrinsicKind::ShiftLeft)
			return result(left->shl(right->getZExtValue()));
		return result(isUnsigned ? left->lshr(right->getZExtValue()) : left->ashr(right->getZExtValue()));
	case IntrinsicKind::Add:
		return result(*left + *right);
	case IntrinsicKind::Subtract:
		return result(*left - *right);
	case IntrinsicKind::Multiply:
		return result(*left * *right);
	case IntrinsicKind::Divide:
	case IntrinsicKind::Modulo:
		if (right->isZero() || (!isUnsigned && left->isMinSignedValue() && right->isAllOnes()))
			return {}; // Match LLVM's undefined division cases.
		if (kind == IntrinsicKind::Divide)
			return result(isUnsigned ? left->udiv(*right) : left->sdiv(*right));
		return result(isUnsigned ? left->urem(*right) : left->srem(*right));
	default:
		return {};
	}
}
