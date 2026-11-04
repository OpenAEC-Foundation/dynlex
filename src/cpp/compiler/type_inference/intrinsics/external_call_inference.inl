DataType calleeType;
if (kind == IntrinsicKind::CallPointer || kind == IntrinsicKind::Invoke) {
	if (context.parseContext.options.emitWASM || context.parseContext.options.emitSPIRV) {
		failWithDetail(expr->range, "Intrinsic 'call pointer' is only available when emitting native CPU code", 0);
		break;
	}
	calleeType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (calleeType.isDeduced() && !calleeType.isAddress()) {
		setConfiguredTypeFailure(expr->range, "call pointer callee must be a native pointer");
		break;
	}
} else {
	for (size_t metadataIndex = 1; metadataIndex <= 2; metadataIndex++) {
		Expression *metadataExpression = resolveThroughFlexBindings(expr->arguments[metadataIndex]);
		if (!metadataExpression || !std::holds_alternative<std::string>(metadataExpression->literalValue)) {
			failIntrinsicArgumentRequirement(metadataIndex, "a string literal");
			break;
		}
	}
}
if (!context.typesValid)
	break;
DataType returnType;
if (kind == IntrinsicKind::Invoke) {
	if (!calleeType.isFunction()) {
		failWithDetail(expr->arguments[1]->range, "Invoking a callback requires a typed callback", 0);
		break;
	}
	returnType = calleeType.functionSignature->result;
} else {
	size_t returnTypeArgumentIndex = externalCallReturnTypeArgumentIndex(kind);
	DataType retTypeRef = ensureExpressionType(expr->arguments[returnTypeArgumentIndex], context, flexBindingFrameStack);
	if (retTypeRef.kind != DataType::Kind::Type) {
		setConfiguredTypeFailure(expr->range, "call return type must be type reference");
		break;
	}
	returnType = retTypeRef.toReferencedType();
	if (!returnType.isConcrete() || returnType.isMetaType()) {
		setConfiguredTypeFailure(expr->range, "call return type must be concrete runtime type");
		break;
	}
	if (calleeType.isFunction() && returnType != calleeType.functionSignature->result) {
		failWithDetail(expr->range, "Callback return type does not match the call's declared return type", 0);
		break;
	}
}
size_t runtimeArgumentStart = externalCallRuntimeArgumentStart(kind);
if (kind == IntrinsicKind::VariadicCall) {
	int fixedArgumentCount = 0;
	if (!resolveStoredCompileTimeInteger(expr->arguments[4], flexBindingFrameStack, fixedArgumentCount, &context)) {
		failIntrinsicArgumentRequirement(4, "a compile-time integer");
		break;
	}
	if (fixedArgumentCount < 0) {
		failWithDetail(expr->arguments[4]->range, "Intrinsic 'variadic call' fixed argument count cannot be negative", 0);
		break;
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
		break;
	}
}
if (calleeType.isFunction() &&
	expr->arguments.size() - runtimeArgumentStart != calleeType.functionSignature->parameters.size()) {
	failWithDetail(
		expr->range, "Callback expects " + std::to_string(calleeType.functionSignature->parameters.size()) + " arguments", 0
	);
	break;
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
			break;
		}
		argType = expr->arguments[i]->type;
	}
	if (argType.kind == DataType::Kind::Type) {
		setConfiguredTypeFailure(expr->range, "call arguments cannot be type values");
		break;
	}
	if (argType.kind == DataType::Kind::Void) {
		failWithDetail(expr->arguments[i]->range, "call arguments must have runtime values");
		break;
	}
}
if (!context.typesValid)
	break;
for (size_t i = runtimeArgumentStart; i < expr->arguments.size(); i++) {
	retainExternalAddress(context, inferAddressProvenance(expr->arguments[i], context, flexBindingFrameStack));
}
invalidateExternalCallWrites(context);
expr->type = returnType;
