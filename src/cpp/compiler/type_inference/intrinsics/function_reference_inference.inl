Expression *functionExpr = resolveThroughFlexBindings(expr->arguments[1]);
requireCompilerInvariant(functionExpr != nullptr, "function intrinsic lost its argument expression during type inference");
if (!std::holds_alternative<std::string>(functionExpr->literalValue)) {
	setConfiguredTypeFailure(functionExpr->range, "function intrinsic requires string literal");
	if (!context.trial)
		context.addDiagnosticWithCurrentTrace(Diagnostic(
			context.parseContext, Diagnostic::Level::Error, "function intrinsic requires string literal", functionExpr->range
		));
	break;
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
	break;
}
if (callableMatches.size() > 1) {
	setConfiguredTypeFailure(functionExpr->range, "ambiguous function reference", "message", {{"signature", signature}});
	if (!context.trial)
		context.addDiagnosticWithCurrentTrace(Diagnostic(
			context.parseContext, Diagnostic::Level::Error, "ambiguous function reference", functionExpr->range, "signature",
			signature
		));
	break;
}
Instantiation *callableInstantiation =
	ensureCallableFunctionInstantiationInferred(callableMatches.front(), context, functionExpr->range);
if (!callableInstantiation)
	break;
expr->selectedCallableDefinition = callableMatches.front().definition;
expr->selectedCallablePathIndex = callableMatches.front().pathIndex;
expr->selectedInstantiation = callableInstantiation;
expr->type = {DataType::Kind::Int, 1};
expr->type.pointerDepth = 1;
