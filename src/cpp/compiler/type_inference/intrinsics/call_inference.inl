void ExpressionInferenceSupport::inferExternalCallIntrinsic(IntrinsicKind kind) {
	if (kind == IntrinsicKind::CallPointer) {
		DataType calleeType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		if (calleeType.isDeduced() && !calleeType.isPointer()) {
			setConfiguredTypeFailure(expr->range, "call pointer callee must be a native pointer");
			return;
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
		return;
	size_t returnTypeArgumentIndex = externalCallReturnTypeArgumentIndex(kind);
	DataType retTypeRef = ensureExpressionType(expr->arguments[returnTypeArgumentIndex], context, flexBindingFrameStack);
	if (retTypeRef.kind != DataType::Kind::Type) {
		setConfiguredTypeFailure(expr->range, "call return type must be type reference");
		return;
	}
	if (retTypeRef.kind == DataType::Kind::Type &&
		(!retTypeRef.toReferencedType().isDeduced() || retTypeRef.toReferencedType().isMetaType())) {
		setConfiguredTypeFailure(expr->range, "call return type must be concrete runtime type");
		return;
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
	for (size_t i = runtimeArgumentStart; i < expr->arguments.size(); i++) {
		DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
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
		return;
	for (size_t i = runtimeArgumentStart; i < expr->arguments.size(); i++) {
		retainExternalAddress(context, inferAddressProvenance(expr->arguments[i], context, flexBindingFrameStack));
	}
	invalidateExternalCallWrites(context);
	if (retTypeRef.kind == DataType::Kind::Type)
		expr->type = retTypeRef.toReferencedType();
}

void ExpressionInferenceSupport::inferFunctionIntrinsic() {
	Expression *functionExpr = resolveThroughFlexBindings(expr->arguments[1]);
	requireCompilerInvariant(functionExpr != nullptr, "function intrinsic lost its argument expression during type inference");
	if (!std::holds_alternative<std::string>(functionExpr->literalValue)) {
		setConfiguredTypeFailure(functionExpr->range, "function intrinsic requires string literal");
		if (!context.trial)
			context.addDiagnosticWithCurrentTrace(Diagnostic(
				context.parseContext, Diagnostic::Level::Error, "function intrinsic requires string literal",
				functionExpr->range
			));
		return;
	}
	std::string signature = std::get<std::string>(functionExpr->literalValue);
	std::vector<CallableFunctionMatch> callableMatches = findCallableFunctionsBySignature(
		context.parseContext, signature, functionExpr->range.line ? functionExpr->range.line->sourceFile : nullptr
	);
	if (callableMatches.empty()) {
		setConfiguredTypeFailure(functionExpr->range, "unknown function reference", "message", {{"signature", signature}});
		if (!context.trial)
			context.addDiagnosticWithCurrentTrace(Diagnostic(
				context.parseContext, Diagnostic::Level::Error, "unknown function reference", functionExpr->range, "signature",
				signature
			));
		return;
	}
	if (callableMatches.size() > 1) {
		setConfiguredTypeFailure(functionExpr->range, "ambiguous function reference", "message", {{"signature", signature}});
		if (!context.trial)
			context.addDiagnosticWithCurrentTrace(Diagnostic(
				context.parseContext, Diagnostic::Level::Error, "ambiguous function reference", functionExpr->range,
				"signature", signature
			));
		return;
	}
	Instantiation *callableInstantiation =
		ensureCallableFunctionInstantiationInferred(callableMatches.front(), context, functionExpr->range);
	if (!callableInstantiation)
		return;
	expr->selectedCallableDefinition = callableMatches.front().definition;
	expr->selectedCallablePathIndex = callableMatches.front().pathIndex;
	expr->selectedInstantiation = callableInstantiation;
	expr->type = {DataType::Kind::Int, 1};
	expr->type.pointerDepth = 1;
}
