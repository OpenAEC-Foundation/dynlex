#pragma once
#include "compileTimeFloatingValue.h"
#include "llvm/ADT/APFloat.h"

static CompileTimeValue evaluateFloatingBinaryIntrinsic(
	IntrinsicKind kind, const CompileTimeValue &leftValue, const CompileTimeValue &rightValue, const DataType &type
) {
	unsigned width = type.numericSize * 8;
	auto leftNumber = getCompileTimeFloatingValue(leftValue, width);
	auto rightNumber = getCompileTimeFloatingValue(rightValue, width);
	requireCompilerInvariant(leftNumber && rightNumber, "constant floating-point operation has no numeric operands");
	llvm::APFloat left = width == 32 ? llvm::APFloat(static_cast<float>(*leftNumber)) : llvm::APFloat(*leftNumber);
	llvm::APFloat right = width == 32 ? llvm::APFloat(static_cast<float>(*rightNumber)) : llvm::APFloat(*rightNumber);
	auto comparison = left.compare(right);
	constexpr auto rounding = llvm::APFloat::rmNearestTiesToEven;
	switch (kind) {
	case IntrinsicKind::Equal:
		return comparison == llvm::APFloat::cmpEqual;
	case IntrinsicKind::NotEqual:
		return comparison != llvm::APFloat::cmpEqual && comparison != llvm::APFloat::cmpUnordered;
	case IntrinsicKind::LessThan:
		return comparison == llvm::APFloat::cmpLessThan;
	case IntrinsicKind::GreaterThan:
		return comparison == llvm::APFloat::cmpGreaterThan;
	case IntrinsicKind::LessThanOrEqual:
		return comparison == llvm::APFloat::cmpLessThan || comparison == llvm::APFloat::cmpEqual;
	case IntrinsicKind::GreaterThanOrEqual:
		return comparison == llvm::APFloat::cmpGreaterThan || comparison == llvm::APFloat::cmpEqual;
	case IntrinsicKind::Add:
		left.add(right, rounding);
		break;
	case IntrinsicKind::Subtract:
		left.subtract(right, rounding);
		break;
	case IntrinsicKind::Multiply:
		left.multiply(right, rounding);
		break;
	case IntrinsicKind::Divide:
		if (right.isZero())
			return {};
		left.divide(right, rounding);
		break;
	case IntrinsicKind::Modulo:
		if (right.isZero())
			return {};
		left.mod(right);
		break;
	case IntrinsicKind::Min:
		left = llvm::minnum(left, right);
		break;
	case IntrinsicKind::Max:
		left = llvm::maxnum(left, right);
		break;
	default:
		return {};
	}
	return width == 32 ? static_cast<double>(left.convertToFloat()) : left.convertToDouble();
}
