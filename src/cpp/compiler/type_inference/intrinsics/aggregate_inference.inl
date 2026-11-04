void ExpressionInferenceSupport::inferShaderInputIntrinsic() {
	expr->type = {DataType::Kind::Vector};
	expr->type.arraySize = 4;
	expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
}

void ExpressionInferenceSupport::inferNumberIntrinsic() {
	TypeConstraint numberConstraint = TypeConstraint::any();
	numberConstraint.requiresNumeric = true;
	expr->type = {DataType::Kind::Constraint};
	context.setExpressionValue(expr, numberConstraint);
}

void ExpressionInferenceSupport::inferElementTypeIntrinsic() {
	DataType aggregateType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (aggregateType.kind == DataType::Kind::Type)
		aggregateType = aggregateType.toReferencedType();
	if (!aggregateType.hasAggregateElementType()) {
		setConfiguredTypeFailure(
			expr->range, "element type requires aggregate", "message", {{"value_type", typeToUserName(aggregateType)}}
		);
	} else {
		expr->type = aggregateType.aggregateElementType().asTypeReference();
		context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
	}
}

void ExpressionInferenceSupport::inferPromoteArithmeticTypeIntrinsic() {
	std::optional<TypeReferenceValue> leftReference =
		getCompileTimeTypeReferenceValue(resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context));
	std::optional<TypeReferenceValue> rightReference =
		getCompileTimeTypeReferenceValue(resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context));
	if (!leftReference || !rightReference) {
		failIntrinsicArgumentRequirement(!leftReference ? 1 : 2, "a compile-time type reference");
	} else {
		DataType promoted;
		if (!DataType::promoteArithmetic(
				leftReference->type.toReferencedType(), rightReference->type.toReferencedType(), promoted
			)) {
			setConfiguredTypeFailure(
				expr->range, "incompatible operand types", "message",
				{{"left_type", typeToUserName(leftReference->type.toReferencedType())},
				 {"right_type", typeToUserName(rightReference->type.toReferencedType())}}
			);
		} else {
			expr->type = promoted.asTypeReference();
			context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
		}
	}
}

void ExpressionInferenceSupport::inferElementIntrinsic(IntrinsicKind kind) {
	DataType aggregateType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (aggregateType.kind == DataType::Kind::ArgumentPack && kind == IntrinsicKind::ExtractElement) {
		int index = 0;
		if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, index, &context)) {
			failIntrinsicArgumentRequirement(2, "a compile-time integer when indexing an argument pack");
		} else if (index < 0 || index >= aggregateType.arraySize) {
			setConfiguredTypeFailure(
				expr->range, "element extraction index out of bounds", "message",
				{{"index", std::to_string(index)}, {"length", std::to_string(aggregateType.arraySize)}}
			);
		} else {
			expr->type = {DataType::Kind::ArgumentReference};
			if (!context.staticLoopFrames.empty() &&
				!isStructurallyCompileTimeConstant(expr->arguments[2], flexBindingFrameStack, context))
				context.staticLoopFrames.back().required = true;
		}
	} else {
		bool isSequentialAggregate =
			(aggregateType.kind == DataType::Kind::Array || aggregateType.kind == DataType::Kind::Vector) &&
			aggregateType.pointerDepth == 0 && aggregateType.arrayElementType;
		if (!isSequentialAggregate) {
			setConfiguredTypeFailure(
				expr->range, "element extraction requires aggregate", "message", {{"value_type", typeToUserName(aggregateType)}}
			);
		} else {
			DataType indexType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
			if (!indexType.isInteger()) {
				setConfiguredTypeFailure(
					expr->range, "element extraction requires integer index", "message",
					{{"index_type", typeToUserName(indexType)}}
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
								{{"value_type", typeToUserName(valueType)},
								 {"element_type", typeToUserName(*aggregateType.arrayElementType)}}
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
	}
}
