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
	const lsp::SourceFile *source = functionExpr->range.line ? functionExpr->range.line->sourceFile : nullptr;
	std::vector<CallableFunctionMatch> callableMatches =
		findCallableFunctionsBySignature(context.parseContext, signature, source);
	if (const auto &record = context.parseContext.recordCallableLookup) {
		context.recordDependencyRead([&](const InferenceReadOwner &owner) {
			record(owner, *source, signature, callableMatches);
		});
	}
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
	expr->type = makeFunctionType(callableInstantiation->returnType, callableInstantiation->argumentTypes);
}
