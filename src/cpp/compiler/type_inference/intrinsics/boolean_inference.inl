void ExpressionInferenceSupport::inferBooleanIntrinsic(IntrinsicKind kind) {
	if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
		DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
		if (kind == IntrinsicKind::And && leftType.isMetaType() && rightType.isMetaType()) {
			// Two type or constraint values: the result accepts exactly the values both accept.
			std::optional<TypeConstraint> constraints[2];
			for (size_t index = 1; index <= 2; index++) {
				constraints[index - 1] = getCompileTimeConstraintValue(
					resolveStoredCompileTimeValue(expr->arguments[index], flexBindingFrameStack, &context)
				);
				if (!constraints[index - 1] || !constraints[index - 1]->isResolved())
					break;
			}
			if (!constraints[0] || !constraints[0]->isResolved()) {
				failCompileTimeOnlyIntrinsicArgument(1, "a type or constraint value");
				return;
			}
			if (!constraints[1] || !constraints[1]->isResolved()) {
				failCompileTimeOnlyIntrinsicArgument(2, "a type or constraint value");
				return;
			}
			std::optional<TypeConstraint> combined = TypeConstraint::conjunction(*constraints[0], *constraints[1]);
			if (!combined) {
				setConfiguredTypeFailure(
					expr->range, "constraint conjunction empty", "message",
					{{"left_type", typeToUserName(*constraints[0])}, {"right_type", typeToUserName(*constraints[1])}}
				);
				return;
			}
			if (std::optional<TypeReferenceValue> typeValue =
					TypeReferenceValue::forConstraint(*combined, context.parseContext.options.emitSPIRV)) {
				expr->type = typeValue->type;
				context.setExpressionValue(expr, *typeValue);
			} else {
				expr->type = {DataType::Kind::Constraint};
				context.setExpressionValue(expr, *combined);
			}
			return;
		}
		if (!isLogicalOperandType(leftType) || !isLogicalOperandType(rightType)) {
			setConfiguredTypeFailure(
				expr->range, "logical operator operands invalid", "message",
				{{"operator", expr->intrinsicName},
				 {"left_type", typeToUserName(leftType)},
				 {"right_type", typeToUserName(rightType)}}
			);
			return;
		}
	} else if (kind == IntrinsicKind::Not) {
		DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		if (!isLogicalOperandType(valueType)) {
			setConfiguredTypeFailure(
				expr->range, "logical operator operand invalid", "message",
				{{"operator", "not"}, {"value_type", typeToUserName(valueType)}}
			);
			return;
		}
	} else {
		DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
		DataType promoted;
		DataType refinedPointerType;
		bool equality = kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual;
		bool pointerEquality =
			equality && leftType.isPointer() && rightType.isPointer() &&
			(leftType == rightType || refineUnspecifiedClassInstantiation(leftType, rightType, refinedPointerType));
		bool promotable = equality ? DataType::promoteEquality(leftType, rightType, promoted)
								   : DataType::promoteArithmetic(leftType, rightType, promoted);
		if (!pointerEquality && !promotable) {
			setConfiguredTypeFailure(
				expr->range, "incompatible operand types", "message",
				{{"left_type", typeToUserName(leftType)}, {"right_type", typeToUserName(rightType)}}
			);
			return;
		}
	}
	expr->type = {DataType::Kind::Bool};
}
