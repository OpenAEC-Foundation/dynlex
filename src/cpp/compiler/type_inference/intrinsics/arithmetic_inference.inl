case IntrinsicReturnKind::SameAsArgs:
if (expr->arguments.size() == 2) {
	DataType operandType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (operandType.kind != DataType::Kind::Unresolved &&
		!(operandType.isNumeric() || (operandType.isVector() && operandType.vectorElementType().isNumeric()))) {
		setConfiguredTypeFailure(
			expr->range, "arithmetic operator operand invalid", "message",
			{{"operator", expr->intrinsicName}, {"value_type", typeToUserName(operandType)}}
		);
		break;
	}
	expr->type = operandType;
	ResolvedBindingLayers resolvedArgument = resolveExpressionBindingWithCallerScope(expr->arguments[1], flexBindingFrameStack);
	if (kind == IntrinsicKind::Negate && resolvedArgument.expression &&
		std::holds_alternative<MinimumSignedIntegerMagnitude>(context.lookupExpressionValue(resolvedArgument.expression)))
		expr->type = {DataType::Kind::Int, 8};
} else {
	DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
	DataType result;
	ArithmeticIntrinsicKind arithmeticOperation = arithmeticIntrinsicKind(expr->intrinsicName);
	if (!promoteIntrinsicArithmetic(arithmeticOperation, leftType, rightType, result)) {
		setConfiguredTypeFailure(
			expr->range, "incompatible operand types", "message",
			{{"left_type", typeToUserName(leftType)}, {"right_type", typeToUserName(rightType)}}
		);
		break;
	}
	int arrayOperandIndex = decayingArrayOperandIndex(arithmeticOperation, leftType, rightType);
	if (arrayOperandIndex != 0 &&
		!inferLValueAddressProvenance(expr->arguments[arrayOperandIndex], context, flexBindingFrameStack)) {
		setConfiguredTypeFailure(expr->range, "fixed array pointer arithmetic requires an addressable array");
		break;
	}
	expr->type = result;
}
break;
