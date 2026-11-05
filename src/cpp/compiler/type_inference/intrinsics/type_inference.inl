void ExpressionInferenceSupport::inferTypeIntrinsic() {
	// @intrinsic("type", kindString[, bits])
	std::string kindStr;
	CompileTimeValue kindValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
	if (auto *str = std::get_if<std::string>(&kindValue))
		kindStr = *str;
	if (kindStr.empty()) {
		failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type kind string");
		return;
	}
	std::optional<DataType> typeRefType = makeBuiltinTypeReference(kindStr, context.parseContext.options.emitSPIRV);
	if (!typeRefType) {
		failWithDetail(
			expr->arguments[1]->range,
			"Unknown compile-time type kind '" + kindStr + "' in intrinsic '" + expr->intrinsicName + "'", 0
		);
		return;
	}
	std::optional<int> numericByteSize;
	if (expr->arguments.size() > 2) {
		int bitCount = 0;
		if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, bitCount, &context) || bitCount <= 0 ||
			bitCount % 8 != 0) {
			failIntrinsicArgumentRequirement(2, "a positive integer divisible by 8");
			return;
		}
		numericByteSize = bitCount / 8;
		typeRefType = makeBuiltinTypeReference(kindStr, context.parseContext.options.emitSPIRV, numericByteSize);
		if (!typeRefType) {
			failIntrinsicArgumentRequirement(2, "a numeric type kind that accepts a bit width");
			return;
		}
	}
	TypeReferenceValue typeRefValue =
		TypeReferenceValue::builtin(kindStr, context.parseContext.options.emitSPIRV, numericByteSize);
	expr->type = typeRefValue.type;
	context.setExpressionValue(expr, typeRefValue);
}

void ExpressionInferenceSupport::inferNumberRuleIntrinsic() {
	// @intrinsic("number rule", properties): the number properties a scope rule fills in.
	std::optional<TypeConstraint> properties =
		getCompileTimeConstraintValue(resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context));
	if (!properties || !properties->isResolved()) {
		failCompileTimeOnlyIntrinsicArgument(1, "a type or constraint value");
		return;
	}
	TypeConstraint number = TypeConstraint::any();
	number.requiresNumeric = true;
	std::optional<TypeConstraint> rule = TypeConstraint::conjunction(number, *properties);
	if (!rule) {
		setConfiguredTypeFailure(
			expr->range, "number rule requires number properties", "message", {{"properties", typeToUserName(*properties)}}
		);
		return;
	}
	expr->type = {DataType::Kind::Constraint};
	context.setExpressionValue(expr, *rule);
}

void ExpressionInferenceSupport::inferFixIntrinsic() {
	CompileTimeValue sourceValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
	std::optional<TypeConstraint> constraint = getCompileTimeConstraintValue(sourceValue);
	if (!constraint) {
		failCompileTimeOnlyIntrinsicArgument(1, "a type or constraint value");
		return;
	}
	constraint->requiresCompileTimeValue = true;
	expr->type = {DataType::Kind::Constraint};
	context.setExpressionValue(expr, *constraint);
}

void ExpressionInferenceSupport::inferTypeOfIntrinsic() {
	DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (valueType.isDeduced()) {
		expr->type = valueType.asTypeReference();
		context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
	}
}

void ExpressionInferenceSupport::inferTypeExtentIntrinsic() {
	DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	int dimension = 0;
	if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, dimension, &context)) {
		failIntrinsicArgumentRequirement(2, "a compile-time integer");
		return;
	}
	std::optional<int> extent = valueType.extent(dimension);
	if (!extent) {
		setConfiguredTypeFailure(expr->range, "type extent invalid");
		return;
	}
	expr->type = {DataType::Kind::Int, 4};
	context.setExpressionValue(expr, static_cast<std::int64_t>(*extent));
}

void ExpressionInferenceSupport::inferSizeOfIntrinsic() {
	Expression *typeExpression = expr->arguments[1];
	if (!inferExpression(typeExpression, context, false, flexBindingFrameStack))
		return;
	expr->arguments[1] = typeExpression;
	DataType typeArgType;
	if (!resolveCompileTimeTypeReference(
			context.parseContext, expr->arguments[1], flexBindingFrameStack, typeArgType, &context
		) ||
		typeArgType.kind != DataType::Kind::Type) {
		failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type reference");
		return;
	}
	DataType valueType = typeArgType.toReferencedType();
	if (!typeArgType.toReferencedType().isConcrete() && context.currentInstantiation) {
		// A later write in the same inference chain can refine an
		// unspecified class layout. Preserve the known result type and
		// revisit this body after that refinement reaches the caller.
		observeUnresolvedRecursiveDependency(context);
		expr->type = {DataType::Kind::Int, 8};
		return;
	}
	if (!valueType.isConcrete() || !valueType.isRuntimeValueType()) {
		if (valueType.kind == DataType::Kind::Class && valueType.classDefinition && valueType.classInstIndex < 0) {
			auto unknownMember = std::ranges::find_if(valueType.classDefinition->fields, [](const FieldDefinition &field) {
				return field.declaredType.kind == DataType::Kind::Any;
			});
			if (unknownMember != valueType.classDefinition->fields.end()) {
				setConfiguredTypeFailure(
					typeExpression->range, "class size unknown member type", "message",
					{{"type", typeToUserName(valueType)}, {"member", unknownMember->name}}
				);
				return;
			}
		}
		setConfiguredTypeFailure(expr->range, "size of type invalid");
		return;
	}
	expr->type = {DataType::Kind::Int, 8};
}

void ExpressionInferenceSupport::inferBuildInfoIntrinsic() {
	Expression *keyExpr = expr->arguments[1];
	if (!inferExpression(keyExpr, context, false, flexBindingFrameStack))
		return;
	expr->arguments[1] = keyExpr;
	CompileTimeValue keyValue = context.lookupExpressionValue(keyExpr);
	auto *key = std::get_if<std::string>(&keyValue);
	if (!key) {
		setConfiguredTypeFailure(expr->range, "build info key must be string literal");
		return;
	}
	std::optional<DataType> infoType = buildInfoValueType(*key);
	if (!infoType) {
		setConfiguredTypeFailure(expr->range, "unknown build info key", "message", {{"key", std::string(*key)}});
		return;
	}
	expr->type = *infoType;
}

void ExpressionInferenceSupport::inferTargetIsIntrinsic() {
	Expression *targetExpr = expr->arguments[1];
	if (!inferExpression(targetExpr, context, false, flexBindingFrameStack))
		return;
	expr->arguments[1] = targetExpr;
	CompileTimeValue targetValue = context.lookupExpressionValue(targetExpr);
	auto *targetName = std::get_if<std::string>(&targetValue);
	if (!targetName) {
		setConfiguredTypeFailure(expr->range, "build target must be string literal");
		return;
	}
	if (!evaluateTargetIs(context.parseContext, *targetName).has_value()) {
		setConfiguredTypeFailure(expr->range, "unknown build target", "message", {{"target", std::string(*targetName)}});
		return;
	}
	expr->type = {DataType::Kind::Bool};
}

void ExpressionInferenceSupport::inferShaderStageIsIntrinsic() {
	Expression *shaderStageExpr = expr->arguments[1];
	if (!inferExpression(shaderStageExpr, context, false, flexBindingFrameStack))
		return;
	expr->arguments[1] = shaderStageExpr;
	CompileTimeValue shaderStageValue = context.lookupExpressionValue(shaderStageExpr);
	auto *shaderStageName = std::get_if<std::string>(&shaderStageValue);
	if (!shaderStageName) {
		setConfiguredTypeFailure(expr->range, "shader stage must be string literal");
		return;
	}
	if (!evaluateShaderStageIs(context.parseContext, *shaderStageName).has_value()) {
		setConfiguredTypeFailure(expr->range, "unknown shader stage", "message", {{"stage", std::string(*shaderStageName)}});
		return;
	}
	expr->type = {DataType::Kind::Bool};
}

void ExpressionInferenceSupport::inferAddPointerDepthIntrinsic() {
	DataType typeArgType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (typeArgType.kind == DataType::Kind::Constraint) {
		CompileTimeValue sourceValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
		std::optional<TypeConstraint> constraint = getCompileTimeConstraintValue(sourceValue);
		if (!constraint || (constraint->kind &&
							(*constraint->kind == DataType::Kind::Type || *constraint->kind == DataType::Kind::Constraint))) {
			setConfiguredTypeFailure(expr->range, "pointer to type invalid");
			return;
		}
		constraint->pointerDepth = constraint->pointerDepth.value_or(0) + 1;
		expr->type = {DataType::Kind::Constraint};
		context.setExpressionValue(expr, *constraint);
		return;
	}
	if (typeArgType.kind != DataType::Kind::Type) {
		failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type or constraint reference");
		return;
	}
	if (typeArgType.referencedKind == DataType::Kind::Type || typeArgType.referencedKind == DataType::Kind::Unresolved) {
		setConfiguredTypeFailure(expr->range, "pointer to type invalid");
		return;
	}
	expr->type = typeArgType;
	expr->type.pointerDepth++;
	std::optional<TypeReferenceValue> sourceTypeValue =
		getCompileTimeTypeReferenceValue(resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context));
	if (!sourceTypeValue)
		crashCompilerBug("pointer type shaping is missing its source type-reference value");
	sourceTypeValue->type = expr->type;
	sourceTypeValue->constraint.pointerDepth = sourceTypeValue->constraint.pointerDepth.value_or(0) + 1;
	context.setExpressionValue(expr, *sourceTypeValue);
}
