#include "shader_resources_inference.inl"

static bool validateShaderRuntimeIntrinsic(Expression *expr, IntrinsicKind kind, InferenceContext &context) {
	if ((kind == IntrinsicKind::ShaderImage || kind == IntrinsicKind::ShaderSampler ||
		 kind == IntrinsicKind::ShaderSampleLevel || kind == IntrinsicKind::ShaderSampleCompare) &&
		!validateShaderResourceIntrinsic(expr, kind, context))
		return false;
	const bool hasNamedShaderResource = kind == IntrinsicKind::ShaderInput || kind == IntrinsicKind::ShaderVertexInput ||
										kind == IntrinsicKind::ShaderInterpolantInput ||
										kind == IntrinsicKind::ShaderInterpolantOutput || kind == IntrinsicKind::ShaderUniform;
	if (hasNamedShaderResource) {
		CompileTimeValue nameValue = context.lookupExpressionValue(expr->arguments[1]);
		const std::string *name = std::get_if<std::string>(&nameValue);
		if (!name || ((kind == IntrinsicKind::ShaderInterpolantInput || kind == IntrinsicKind::ShaderInterpolantOutput ||
					   kind == IntrinsicKind::ShaderUniform || kind == IntrinsicKind::ShaderVertexInput) &&
					  name->empty())) {
			std::string_view diagnosticKey =
				kind == IntrinsicKind::ShaderInput		   ? "shader input requires string literal"
				: kind == IntrinsicKind::ShaderVertexInput ? "shader vertex input requires string literal"
				: kind == IntrinsicKind::ShaderUniform	   ? "shader uniform requires string literal"
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
					context.parseContext.options.shaderStage != ParseContext::ShaderStage::Compute &&
					(*name == "Position") == (context.parseContext.options.shaderStage == ParseContext::ShaderStage::Vertex);
				if (!stageProvidesInput) {
					failWithDetail("shader input unavailable");
					return false;
				}
			}
		}
		if (kind == IntrinsicKind::ShaderUniform) {
			CompileTimeValue bindingValue = context.lookupExpressionValue(expr->arguments[2]);
			std::optional<std::int64_t> binding = getCompileTimeIntegerValue(bindingValue);
			if (!binding || *binding < 0 || static_cast<std::uint64_t>(*binding) > UINT32_MAX) {
				std::string detail = renderConfiguredMessage(
					syntaxConfigForRange(context.parseContext, expr->arguments[2]->range), "shader uniform binding invalid",
					"message"
				);
				context.setTypeFailure(detail);
				context.fail(buildFailureDetailDiagnostic(expr->arguments[2]->range, detail), 0);
				return false;
			}
			auto reference = getCompileTimeTypeReferenceValue(context.lookupExpressionValue(expr->arguments[3]));
			if (!reference || !shaderUniformLayout(reference->type.toReferencedType())) {
				failWithDetail("shader uniform type invalid");
				return false;
			}
		}
		if (kind == IntrinsicKind::ShaderVertexInput) {
			auto location = getCompileTimeIntegerValue(context.lookupExpressionValue(expr->arguments[2]));
			if (!location || *location < 0 || static_cast<std::uint64_t>(*location) > UINT32_MAX) {
				failWithDetail("shader vertex input location invalid");
				return false;
			}
			auto reference = getCompileTimeTypeReferenceValue(context.lookupExpressionValue(expr->arguments[3]));
			DataType valueType = reference ? reference->type.toReferencedType() : DataType{};
			DataType scalar = valueType;
			bool shape = !valueType.isPointer();
			if (valueType.kind == DataType::Kind::Vector) {
				shape = shape && valueType.arraySize >= 2 && valueType.arraySize <= 4;
				scalar = valueType.aggregateElementType();
			}
			bool numeric = scalar.kind == DataType::Kind::Float || scalar.kind == DataType::Kind::Int ||
						   scalar.kind == DataType::Kind::UInt;
			if (!reference || !shape || !numeric || scalar.isPointer() || scalar.numericSize != 4) {
				failWithDetail("shader vertex input type invalid");
				return false;
			}
			if (context.parseContext.options.emitSPIRV &&
				context.parseContext.options.shaderStage != ParseContext::ShaderStage::Vertex) {
				failWithDetail("shader vertex input unavailable");
				return false;
			}
		}
		if (context.parseContext.options.emitSPIRV &&
			(kind == IntrinsicKind::ShaderInterpolantInput || kind == IntrinsicKind::ShaderInterpolantOutput)) {
			const bool isVertex = context.parseContext.options.shaderStage == ParseContext::ShaderStage::Vertex;
			const bool available = context.parseContext.options.shaderStage != ParseContext::ShaderStage::Compute &&
								   (kind == IntrinsicKind::ShaderInterpolantOutput) == isVertex;
			if (!available) {
				failWithDetail(
					kind == IntrinsicKind::ShaderInterpolantInput ? "shader interpolant input unavailable"
																  : "shader interpolant output unavailable"
				);
				return false;
			}
		}
	}
	if (context.parseContext.options.emitSPIRV &&
		context.parseContext.options.shaderStage == ParseContext::ShaderStage::Compute &&
		(kind == IntrinsicKind::ShaderOutput || kind == IntrinsicKind::ShaderUniform)) {
		std::string detail = renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, expr->range), "shader operation unavailable for stage", "message",
			{{"operation", expr->intrinsicName}}
		);
		context.setTypeFailure(detail);
		context.fail(buildFailureDetailDiagnostic(expr->range, detail), 0);
		return false;
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
