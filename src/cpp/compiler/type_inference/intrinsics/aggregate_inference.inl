bool handledAggregateIntrinsic = true;
if (kind == IntrinsicKind::ShaderInput) {
	expr->type = {DataType::Kind::Vector};
	expr->type.arraySize = 4;
	expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
} else if (kind == IntrinsicKind::Number) {
	TypeConstraint numberConstraint = TypeConstraint::any();
	numberConstraint.requiresNumeric = true;
	expr->type = {DataType::Kind::Constraint};
	context.setExpressionValue(expr, numberConstraint);
} else if (kind == IntrinsicKind::ElementType) {
	DataType aggregateType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (aggregateType.kind == DataType::Kind::Type)
		aggregateType = aggregateType.toReferencedType();
	if (!aggregateType.hasAggregateElementType()) {
		setConfiguredTypeFailure(
			expr->range, "element type requires aggregate", "message",
			{{"value_type", typeToUserName(aggregateType, context.parseContext)}}
		);
	} else {
		expr->type = aggregateType.aggregateElementType().asTypeReference();
		context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
	}
} else if (kind == IntrinsicKind::PromoteArithmeticType) {
	std::optional<TypeReferenceValue> leftReference = getCompileTimeTypeReferenceValue(
		resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context)
	);
	std::optional<TypeReferenceValue> rightReference = getCompileTimeTypeReferenceValue(
		resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
	);
	if (!leftReference || !rightReference) {
		failIntrinsicArgumentRequirement(!leftReference ? 1 : 2, "a compile-time type reference");
	} else {
		DataType promoted;
		if (!DataType::promoteArithmetic(
				leftReference->type.toReferencedType(), rightReference->type.toReferencedType(), promoted
			)) {
			setConfiguredTypeFailure(
				expr->range, "incompatible operand types", "message",
				{{"left_type", typeToUserName(leftReference->type.toReferencedType(), context.parseContext)},
				 {"right_type", typeToUserName(rightReference->type.toReferencedType(), context.parseContext)}}
			);
		} else {
			expr->type = promoted.asTypeReference();
			context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
		}
	}
} else if (kind == IntrinsicKind::ExtractElement || kind == IntrinsicKind::InsertElement) {
	DataType aggregateType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	bool isSequentialAggregate =
		(aggregateType.kind == DataType::Kind::Array || aggregateType.kind == DataType::Kind::Vector) &&
		aggregateType.pointerDepth == 0 && aggregateType.arrayElementType;
	if (!isSequentialAggregate) {
		setConfiguredTypeFailure(
			expr->range, "element extraction requires aggregate", "message",
			{{"value_type", typeToUserName(aggregateType, context.parseContext)}}
		);
	} else {
		DataType indexType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
		if (!indexType.isInteger()) {
			setConfiguredTypeFailure(
				expr->range, "element extraction requires integer index", "message",
				{{"index_type", typeToUserName(indexType, context.parseContext)}}
			);
		} else {
			std::optional<std::int64_t> index = getCompileTimeIntegerValue(
				resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
			);
			if (index && (*index < 0 || *index >= aggregateType.arraySize)) {
				setConfiguredTypeFailure(
					expr->range, "element extraction index out of bounds", "message",
					{{"index", std::to_string(*index)}, {"length", std::to_string(aggregateType.arraySize)}}
				);
			} else {
				if (kind == IntrinsicKind::InsertElement) {
					DataType valueType = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
					if (!DataType::supportsRuntimeConversion(valueType, *aggregateType.arrayElementType)) {
						setConfiguredTypeFailure(
							expr->range, "element insertion value incompatible", "message",
							{{"value_type", typeToUserName(valueType, context.parseContext)},
							 {"element_type", typeToUserName(*aggregateType.arrayElementType, context.parseContext)}}
						);
					} else {
						expr->type = aggregateType;
					}
				} else {
					expr->type = *aggregateType.arrayElementType;
				}
			}
		}
	}
} else {
	handledAggregateIntrinsic = false;
}
