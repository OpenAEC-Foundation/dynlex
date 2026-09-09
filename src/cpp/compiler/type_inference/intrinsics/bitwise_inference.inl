case IntrinsicReturnKind::SameAsInts:
if (expr->arguments.size() == 2) {
	DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (!isBitwiseOperandType(valueType)) {
		setConfiguredTypeFailure(
			expr->range, "bitwise operator operand invalid", "message",
			{{"operator", expr->intrinsicName}, {"value_type", typeToUserName(valueType)}}
		);
		break;
	}
	expr->type = valueType;
} else {
	DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
	DataType result;
	if (!DataType::promoteBitwise(leftType, rightType, result)) {
		setConfiguredTypeFailure(
			expr->range, "bitwise operator operands invalid", "message",
			{{"operator", expr->intrinsicName},
			 {"left_type", typeToUserName(leftType)},
			 {"right_type", typeToUserName(rightType)}}
		);
		break;
	}
	expr->type = result;
}
break;
