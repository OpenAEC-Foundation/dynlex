void ExpressionInferenceSupport::inferExternalCallIntrinsic(IntrinsicKind kind) {
	DataType calleeType;
	if (kind == IntrinsicKind::CallPointer || kind == IntrinsicKind::Invoke) {
		calleeType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		if (calleeType.isDeduced() && !calleeType.isAddress()) {
			setConfiguredTypeFailure(expr->range, "call pointer callee must be a native pointer");
			return;
		}
	} else {
		for (size_t metadataIndex = 1; metadataIndex <= 2; metadataIndex++) {
			Expression *metadataExpression = resolveThroughFlexBindings(expr->arguments[metadataIndex]);
			if (!metadataExpression || !std::holds_alternative<std::string>(metadataExpression->literalValue)) {
				failIntrinsicArgumentRequirement(metadataIndex, "a string literal");
				return;
			}
		}
	}
	if (!context.typesValid)
		return;
	DataType returnType;
	if (kind == IntrinsicKind::Invoke) {
		if (!calleeType.isFunction()) {
			failWithDetail(expr->arguments[1]->range, "Invoking a callback requires a typed callback", 0);
			return;
		}
		returnType = calleeType.functionSignature->result;
	} else {
		size_t returnTypeArgumentIndex = externalCallReturnTypeArgumentIndex(kind);
		DataType retTypeRef = ensureExpressionType(expr->arguments[returnTypeArgumentIndex], context, flexBindingFrameStack);
		if (retTypeRef.kind != DataType::Kind::Type) {
			setConfiguredTypeFailure(expr->range, "call return type must be type reference");
			return;
		}
		returnType = retTypeRef.toReferencedType();
		if (!returnType.isConcrete() || returnType.isMetaType()) {
			setConfiguredTypeFailure(expr->range, "call return type must be concrete runtime type");
			return;
		}
		if (calleeType.isFunction() && returnType != calleeType.functionSignature->result) {
			failWithDetail(expr->range, "Callback return type does not match the call's declared return type", 0);
			return;
		}
	}
	size_t runtimeArgumentStart = externalCallRuntimeArgumentStart(kind);
	if (kind == IntrinsicKind::VariadicCall) {
		int fixedArgumentCount = 0;
		if (!resolveStoredCompileTimeInteger(expr->arguments[4], flexBindingFrameStack, fixedArgumentCount, &context)) {
			failIntrinsicArgumentRequirement(4, "a compile-time integer");
			return;
		}
		if (fixedArgumentCount < 0) {
			failWithDetail(expr->arguments[4]->range, "Intrinsic 'variadic call' fixed argument count cannot be negative", 0);
			return;
		}
		size_t callArgumentCount = expr->arguments.size() - runtimeArgumentStart;
		if (static_cast<size_t>(fixedArgumentCount) > callArgumentCount) {
			std::string argumentLabel = callArgumentCount == 1 ? " call argument" : " call arguments";
			failWithDetail(
				expr->arguments[4]->range,
				"Intrinsic 'variadic call' fixed argument count cannot exceed its " + std::to_string(callArgumentCount) +
					argumentLabel,
				0
			);
			return;
		}
	}
	if (calleeType.isFunction() &&
		expr->arguments.size() - runtimeArgumentStart != calleeType.functionSignature->parameters.size()) {
		failWithDetail(
			expr->range, "Callback expects " + std::to_string(calleeType.functionSignature->parameters.size()) + " arguments", 0
		);
		return;
	}
	for (size_t i = runtimeArgumentStart; i < expr->arguments.size(); i++) {
		DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
		if (calleeType.isFunction()) {
			const DataType &expected = calleeType.functionSignature->parameters[i - runtimeArgumentStart];
			if (argType != expected &&
				!tryApplyUserConversion(expr->arguments[i], expected, true, context, flexBindingFrameStack)) {
				if (context.typesValid)
					failWithDetail(
						expr->arguments[i]->range,
						"Callback argument " + std::to_string(i - runtimeArgumentStart + 1) + " must be " +
							typeToUserName(expected),
						0
					);
				return;
			}
			argType = expr->arguments[i]->type;
		}
		if (argType.kind == DataType::Kind::Type) {
			setConfiguredTypeFailure(expr->range, "call arguments cannot be type values");
			return;
		}
		if (argType.kind == DataType::Kind::Void) {
			failWithDetail(expr->arguments[i]->range, "call arguments must have runtime values");
			return;
		}
	}
	if (!context.typesValid)
		return;
	for (size_t i = runtimeArgumentStart; i < expr->arguments.size(); i++) {
		retainExternalAddress(context, inferAddressProvenance(expr->arguments[i], context, flexBindingFrameStack));
	}
	invalidateExternalCallWrites(context);
	expr->type = returnType;
}
