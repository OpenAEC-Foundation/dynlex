static bool validateShaderResourceIntrinsic(Expression *expr, IntrinsicKind kind, InferenceContext &context) {
	auto fail = [&](size_t argument, std::string_view key) {
		std::string detail = renderConfiguredMessage(syntaxConfigForRange(context.parseContext, expr->range), key, "message");
		context.setTypeFailure(detail);
		context.fail(buildFailureDetailDiagnostic(expr->arguments[argument]->range, detail), 0);
		return false;
	};
	if (kind == IntrinsicKind::ShaderImage || kind == IntrinsicKind::ShaderSampler) {
		CompileTimeValue name = context.lookupExpressionValue(expr->arguments[1]);
		const auto *text = std::get_if<std::string>(&name);
		if (!text || text->empty())
			return fail(1, "shader resource name invalid");
		auto binding = getCompileTimeIntegerValue(context.lookupExpressionValue(expr->arguments[2]));
		if (!binding || *binding < 0 || static_cast<std::uint64_t>(*binding) > UINT32_MAX)
			return fail(2, "shader resource binding invalid");
		size_t flagIndex = kind == IntrinsicKind::ShaderImage ? 4 : 3;
		CompileTimeValue flag = context.lookupExpressionValue(expr->arguments[flagIndex]);
		if (!std::holds_alternative<bool>(flag))
			return fail(flagIndex, "shader resource flag invalid");
		if (kind == IntrinsicKind::ShaderImage) {
			CompileTimeValue dimension = context.lookupExpressionValue(expr->arguments[3]);
			const auto *shape = std::get_if<std::string>(&dimension);
			if (!shape || (*shape != "2d" && *shape != "cube"))
				return fail(3, "shader image dimension invalid");
			if (*shape == "cube" && std::get<bool>(flag))
				return fail(3, "shader depth image dimension invalid");
		}
		return true;
	}
	CompileTimeValue imageValue = context.lookupExpressionValue(expr->arguments[1]);
	CompileTimeValue samplerValue = context.lookupExpressionValue(expr->arguments[2]);
	const auto *image = std::get_if<ShaderResourceValue>(&imageValue);
	const auto *sampler = std::get_if<ShaderResourceValue>(&samplerValue);
	if (!image || !image->isImage())
		return fail(1, "shader sample requires image");
	if (!sampler || sampler->isImage())
		return fail(2, "shader sample requires sampler");
	bool comparison = kind == IntrinsicKind::ShaderSampleCompare;
	if ((image->kind == ShaderResourceKind::Depth2D) != comparison ||
		(sampler->kind == ShaderResourceKind::ComparisonSampler) != comparison)
		return fail(1, "shader sample resources incompatible");
	DataType coordinates = expr->arguments[3]->type;
	int components = image->kind == ShaderResourceKind::ImageCube ? 3 : 2;
	if (!coordinates.isVector() || coordinates.vectorSize() != components ||
		coordinates.vectorElementType() != DataType(DataType::Kind::Float, 4))
		return fail(3, "shader sample coordinates invalid");
	if (expr->arguments[4]->type != DataType(DataType::Kind::Float, 4))
		return fail(4, "shader sample scalar invalid");
	return true;
}
