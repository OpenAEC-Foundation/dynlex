void ExpressionInferenceSupport::inferLifecycleValueIntrinsic() {
	Section *sourceSection = expr->range.line ? expr->range.line->section : nullptr;
	while (sourceSection && sourceSection->type != SectionType::Retain && sourceSection->type != SectionType::Release) {
		sourceSection = sourceSection->parent;
	}
	if (!sourceSection) {
		failWithDetail(expr->range, "lifecycle value is only available in retain and release sections");
		return;
	}
	requireCompilerInvariant(
		context.currentInstantiation && context.currentInstantiation->argumentTypes.size() == 1,
		"lifecycle section inference is missing its managed value type"
	);
	expr->type = context.currentInstantiation->argumentTypes.front();
}

void ExpressionInferenceSupport::inferSubjectIntrinsic() {
	if (context.currentSubject.ambiguous) {
		failWithDetail(expr->range, "the subject depends on which control-flow path executes");
		return;
	}
	if (!context.currentSubject.setter) {
		failWithDetail(expr->range, "no subject has been set");
		return;
	}
	expr->subjectSetter = context.currentSubject.setter;
	if (expr->subjectSetter->arguments.size() <= 1)
		crashCompilerBug("subject assignment is missing its value expression");
	expr->type = ensureExpressionType(expr->subjectSetter->arguments[1], context, flexBindingFrameStack);
}

void ExpressionInferenceSupport::inferCommandLineArgumentsIntrinsic(IntrinsicKind kind) {
	if (context.parseContext.options.noMain) {
		failWithDetail(expr->range, "Command-line arguments require a generated main function", 0);
		return;
	}
	if (context.parseContext.options.emitWASM || context.parseContext.options.emitSPIRV) {
		failWithDetail(expr->range, "Command-line arguments are unavailable for this target", 0);
		return;
	}
	if (kind == IntrinsicKind::CommandLineArgumentCount) {
		expr->type = {DataType::Kind::Int, 4};
	} else {
		expr->type = {DataType::Kind::Int, 1};
		expr->type.pointerDepth = 2;
	}
}

void ExpressionInferenceSupport::inferAddressOfIntrinsic() {
	DataType varType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (varType.isDeduced())
		expr->type = varType.pointed();
}

void ExpressionInferenceSupport::inferDereferenceIntrinsic() {
	DataType ptrType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (ptrType.isDeduced() && ptrType.isPointer())
		expr->type = ptrType.dereferenced();
}

void ExpressionInferenceSupport::inferCastIntrinsic() {
	DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	DataType typeArgType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
	if (typeArgType.kind != DataType::Kind::Type) {
		failCompileTimeOnlyIntrinsicArgument(2, "a compile-time type reference");
		return;
	}
	DataType castResultType;
	DataType requestedType = typeArgType.toReferencedType();
	if (!tryResolveCastResultType(valueType, typeArgType, castResultType)) {
		if (tryApplyUserConversion(expr->arguments[1], requestedType, false, context, flexBindingFrameStack)) {
			expr->type = requestedType;
			context.setExpressionValue(expr, context.lookupExpressionValue(expr->arguments[1]));
			return;
		}
		if (!context.typesValid)
			return;
		if (!isValidCastRuntimeType(valueType)) {
			setConfiguredTypeFailure(
				expr->range, "invalid cast source type", "message", {{"source_type", typeToUserName(valueType)}}
			);
			return;
		}
		setConfiguredTypeFailure(
			expr->range, "unsupported cast", "message",
			{{"from_type", typeToUserName(valueType)}, {"to_type", typeToUserName(requestedType)}}
		);
		return;
	}
	expr->type = castResultType;
}

void ExpressionInferenceSupport::inferSelectIntrinsic() {
	DataType conditionType = expr->arguments[1]->type;
	if (!conditionType.isDeduced())
		conditionType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (!isLogicalOperandType(conditionType)) {
		setConfiguredTypeFailure(
			expr->range, "logical operator operand invalid", "message",
			{{"operator", "select condition"}, {"value_type", typeToUserName(conditionType)}}
		);
		return;
	}
	CompileTimeValue conditionValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
	if (auto *condition = std::get_if<bool>(&conditionValue)) {
		size_t selectedArgumentIndex = *condition ? 2 : 3;
		DataType selectedType = expr->arguments[selectedArgumentIndex]->type;
		if (!selectedType.isDeduced())
			selectedType = ensureExpressionType(expr->arguments[selectedArgumentIndex], context, flexBindingFrameStack);
		if (selectedType.isDeduced())
			expr->type = selectedType;
		context.setExpressionValue(expr, context.lookupExpressionValue(expr->arguments[selectedArgumentIndex]));
		return;
	}
	DataType trueType = expr->arguments[2]->type;
	if (!trueType.isDeduced())
		trueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
	DataType falseType = expr->arguments[3]->type;
	if (!falseType.isDeduced())
		falseType = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
	DataType mergedType;
	if (!mergeSelectBranchTypes(trueType, falseType, mergedType)) {
		setConfiguredTypeFailure(
			expr->range, "incompatible operand types", "message",
			{{"left_type", typeToUserName(trueType)}, {"right_type", typeToUserName(falseType)}}
		);
		return;
	}
	expr->type = mergedType;
}
