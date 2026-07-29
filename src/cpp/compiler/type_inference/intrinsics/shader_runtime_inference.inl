static bool validateShaderRuntimeIntrinsic(Expression *expr, IntrinsicKind kind, InferenceContext &context) {
	const bool hasNamedShaderResource = kind == IntrinsicKind::ShaderInput || kind == IntrinsicKind::ShaderInterpolantInput ||
										kind == IntrinsicKind::ShaderInterpolantOutput || kind == IntrinsicKind::ShaderUniform;
	if (hasNamedShaderResource) {
		CompileTimeValue nameValue = context.lookupExpressionValue(expr->arguments[1]);
		const std::string *name = std::get_if<std::string>(&nameValue);
		if (!name || ((kind == IntrinsicKind::ShaderInterpolantInput || kind == IntrinsicKind::ShaderInterpolantOutput) &&
					  name->empty())) {
			std::string_view diagnosticKey = kind == IntrinsicKind::ShaderInput ? "shader input requires string literal"
											 : kind == IntrinsicKind::ShaderUniform
												 ? "shader uniform requires string literal"
												 : "shader interpolant requires string literal";
			context.fail(
				Diagnostic(context.parseContext, Diagnostic::Level::Error, diagnosticKey, expr->arguments[1]->range), 0, false
			);
			return false;
		}
		auto failWithDetail = [&](std::string_view diagnosticKey) {
			std::string detail = renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, expr->arguments[1]->range), diagnosticKey, "message",
				{{"name", *name}}
			);
			context.setTypeFailure(detail);
			context.fail(buildFailureDetailDiagnostic(expr->arguments[1]->range, detail), 0);
		};
		if (kind == IntrinsicKind::ShaderInput) {
			bool knownInput = *name == "FragCoord" || *name == "Position";
			if (!knownInput) {
				failWithDetail("unknown shader input");
				return false;
			}
			if (context.parseContext.options.emitSPIRV) {
				bool stageProvidesInput =
					(*name == "Position") == (context.parseContext.options.shaderStage == ParseContext::ShaderStage::Vertex);
				if (!stageProvidesInput) {
					failWithDetail("shader input unavailable");
					return false;
				}
			}
		}
		if (context.parseContext.options.emitSPIRV &&
			(kind == IntrinsicKind::ShaderInterpolantInput || kind == IntrinsicKind::ShaderInterpolantOutput)) {
			const bool isVertex = context.parseContext.options.shaderStage == ParseContext::ShaderStage::Vertex;
			const bool available = (kind == IntrinsicKind::ShaderInterpolantOutput) == isVertex;
			if (!available) {
				failWithDetail(
					kind == IntrinsicKind::ShaderInterpolantInput ? "shader interpolant input unavailable"
																  : "shader interpolant output unavailable"
				);
				return false;
			}
		}
	}
	if (!context.parseContext.options.emitSPIRV) {
		std::string detail = renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, expr->range), "shader operation unavailable", "message",
			{{"operation", expr->intrinsicName}}
		);
		context.setTypeFailure(detail);
		context.fail(buildFailureDetailDiagnostic(expr->range, detail), 0);
		return false;
	}
	return true;
}
