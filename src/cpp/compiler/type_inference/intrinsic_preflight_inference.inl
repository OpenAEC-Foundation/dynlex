const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
if (info) {
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
		break;
}
if (isShaderRuntimeIntrinsicKind(kind)) {
	if (kind == IntrinsicKind::ShaderInput || kind == IntrinsicKind::ShaderUniform) {
		CompileTimeValue nameValue = context.lookupExpressionValue(expr->arguments[1]);
		const std::string *name = std::get_if<std::string>(&nameValue);
		if (!name) {
			std::string_view diagnosticKey = kind == IntrinsicKind::ShaderInput ? "shader input requires string literal"
																				: "shader uniform requires string literal";
			context.fail(
				Diagnostic(context.parseContext, Diagnostic::Level::Error, diagnosticKey, expr->arguments[1]->range), 0, false
			);
			break;
		}
		if (kind == IntrinsicKind::ShaderInput) {
			bool knownInput = *name == "FragCoord" || *name == "Position";
			if (!knownInput) {
				failWithDetail(
					expr->arguments[1]->range,
					renderConfiguredMessage(
						syntaxConfigForRange(context.parseContext, expr->arguments[1]->range), "unknown shader input",
						"message", {{"name", *name}}
					),
					0
				);
				break;
			}
			if (context.parseContext.options.emitSPIRV) {
				bool stageProvidesInput =
					(*name == "Position") == (context.parseContext.options.shaderStage == ParseContext::ShaderStage::Vertex);
				if (!stageProvidesInput) {
					failWithDetail(
						expr->arguments[1]->range,
						renderConfiguredMessage(
							syntaxConfigForRange(context.parseContext, expr->arguments[1]->range), "shader input unavailable",
							"message", {{"name", *name}}
						),
						0
					);
					break;
				}
			}
		}
	}
	if (!context.parseContext.options.emitSPIRV) {
		failWithDetail(
			expr->range,
			renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, expr->range), "shader operation unavailable", "message",
				{{"operation", expr->intrinsicName}}
			),
			0
		);
		break;
	}
}
