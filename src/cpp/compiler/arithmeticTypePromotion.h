#pragma once

#include "intrinsicInfo.h"
#include "type.h"

inline bool isFixedArrayValue(const DataType &type) { return type.kind == DataType::Kind::Array && type.pointerDepth == 0; }

inline bool isPointerArithmeticBase(const DataType &type) { return type.isPointer() || isFixedArrayValue(type); }

inline DataType pointerArithmeticBaseType(const DataType &type) {
	if (type.isPointer())
		return type;
	requireCompilerInvariant(isFixedArrayValue(type), "pointer arithmetic base is neither a pointer nor a fixed array");
	if (!type.arrayElementType)
		return {DataType::Kind::Unresolved};
	return type.arrayElementType->pointed();
}

inline int decayingArrayOperandIndex(ArithmeticIntrinsicKind operation, const DataType &leftType, const DataType &rightType) {
	if (operation == ArithmeticIntrinsicKind::Add) {
		if (isFixedArrayValue(leftType) && rightType.isInteger())
			return 1;
		if (leftType.isInteger() && isFixedArrayValue(rightType))
			return 2;
	}
	if (operation == ArithmeticIntrinsicKind::Subtract && isFixedArrayValue(leftType) && rightType.isInteger())
		return 1;
	return 0;
}

inline bool promoteIntrinsicArithmetic(
	ArithmeticIntrinsicKind operation, const DataType &leftType, const DataType &rightType, DataType &result
) {
	if (leftType.kind == DataType::Kind::Unresolved || rightType.kind == DataType::Kind::Unresolved) {
		result = {DataType::Kind::Unresolved};
		return true;
	}

	bool leftIsBase = isPointerArithmeticBase(leftType);
	bool rightIsBase = isPointerArithmeticBase(rightType);
	if (leftIsBase || rightIsBase) {
		if (operation == ArithmeticIntrinsicKind::Add) {
			if (leftIsBase && rightType.isInteger()) {
				result = pointerArithmeticBaseType(leftType);
				return true;
			}
			if (leftType.isInteger() && rightIsBase) {
				result = pointerArithmeticBaseType(rightType);
				return true;
			}
		}
		if (operation == ArithmeticIntrinsicKind::Subtract && leftIsBase && rightType.isInteger()) {
			result = pointerArithmeticBaseType(leftType);
			return true;
		}
		return false;
	}

	return DataType::promoteArithmetic(leftType, rightType, result);
}
