// Resource references are static; the numeric call is lowered to SPIR-V image
// instructions together with the shader interface, without runtime name strings.
static CodegenResult generateShaderSampleIntrinsic(ParseContext &context, Expression *call) {
	CompileTimeValue imageValue = resolveStoredCompileTimeValue(call->arguments[1], context.flexBindingFrames);
	CompileTimeValue samplerValue = resolveStoredCompileTimeValue(call->arguments[2], context.flexBindingFrames);
	const auto *image = std::get_if<ShaderResourceValue>(&imageValue);
	const auto *sampler = std::get_if<ShaderResourceValue>(&samplerValue);
	requireCompilerInvariant(image && sampler, "validated shader sample lost its binding references");
	Range range = intrinsicDiagnosticRange(context, call);
	if (!context.registerShaderResource(*image, range) || !context.registerShaderResource(*sampler, range))
		return CodegenResult::failure();
	llvm::Value *coordinates = nullptr;
	llvm::Value *scalar = nullptr;
	if (!generateIntrinsicRuntimeValue(context, call->arguments[3], coordinates) ||
		!generateIntrinsicRuntimeValue(context, call->arguments[4], scalar))
		return CodegenResult::failure();
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	auto *type = llvm::FunctionType::get(
		getLLVMType(context, finalizedExpressionType(context, call)), {coordinates->getType(), scalar->getType()}, false
	);
	auto function = context.llvmModule->getOrInsertFunction(shaderSampleFunctionName(*image, *sampler), type);
	return builder.CreateCall(function, {coordinates, scalar}, "shader_sample");
}
