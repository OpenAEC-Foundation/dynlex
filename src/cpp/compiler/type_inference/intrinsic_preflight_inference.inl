bool ExpressionInferenceSupport::validateIntrinsic(IntrinsicKind kind, const IntrinsicInfo *info) {
	if (info) {
		if ((context.parseContext.options.emitSPIRV && info->targets != IntrinsicTargets::All) ||
			(context.parseContext.options.emitWASM && info->targets == IntrinsicTargets::Native)) {
			std::string targets = info->targets == IntrinsicTargets::Native ? "native CPU" : "native CPU or WebAssembly";
			failWithDetail(
				expr->range, "Intrinsic '" + expr->intrinsicName + "' is only available when emitting " + targets + " code", 0
			);
			return false;
		}
		for (size_t argumentIndex = 1; argumentIndex < expr->arguments.size(); argumentIndex++) {
			if (!intrinsicArgumentIsCompileTimeOnly(expr->intrinsicName, static_cast<int>(argumentIndex)))
				continue;
			Expression *argumentExpression = expr->arguments[argumentIndex];
			if (!argumentExpression)
				crashCompilerBug("intrinsic compile-time argument validation encountered null argument expression");
			ensureExpressionType(argumentExpression, context, flexBindingFrameStack);
			if (!context.typesValid)
				break;
			CompileTimeValue argumentValue = resolveStoredCompileTimeValue(argumentExpression, flexBindingFrameStack, &context);
			if (!isCompileTimeKnown(argumentValue)) {
				failCompileTimeOnlyIntrinsicArgument(argumentIndex, "compile-time known");
				break;
			}
		}
		if (!context.typesValid)
			return false;
	}
	if (isShaderRuntimeIntrinsicKind(kind) && !validateShaderRuntimeIntrinsic(expr, kind, context))
		return false;

	if (kind == IntrinsicKind::DestroyAt) {
		DataType pointerType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		if (!pointerType.isDeduced() || !pointerType.isPointer()) {
			setConfiguredTypeFailure(expr->range, "store at requires pointer");
			return false;
		}
		DataType elementType = pointerType.dereferenced();
		if (!elementType.isConcrete()) {
			setConfiguredTypeFailure(
				expr->range, "destroy at requires concrete pointee", "message", {{"type", typeToUserName(elementType)}}
			);
			return false;
		}
	}
	return true;
}
