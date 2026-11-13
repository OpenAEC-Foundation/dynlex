void ExpressionInferenceSupport::inferCustomIntrinsic(IntrinsicKind kind) {
	if (kind == IntrinsicKind::ShaderImage || kind == IntrinsicKind::ShaderSampler) {
		expr->type = {kind == IntrinsicKind::ShaderImage ? DataType::Kind::ShaderImage : DataType::Kind::ShaderSampler};
		return;
	}
	if (kind == IntrinsicKind::ShaderSampleLevel) {
		expr->type = {DataType::Kind::Vector};
		expr->type.arraySize = 4;
		expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
		return;
	}
	if (kind == IntrinsicKind::ShaderVertexInput || kind == IntrinsicKind::ShaderUniform) {
		auto reference = getCompileTimeTypeReferenceValue(context.lookupExpressionValue(expr->arguments[3]));
		requireCompilerInvariant(reference.has_value(), "validated shader input is missing its type reference");
		expr->type = reference->type.toReferencedType();
		return;
	}
	if (kind == IntrinsicKind::ShaderInput || kind == IntrinsicKind::ShaderInterpolantInput) {
		inferShaderInputIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Number) {
		inferNumberIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::ElementType) {
		inferElementTypeIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::PromoteArithmeticType) {
		inferPromoteArithmeticTypeIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::ExtractElement || kind == IntrinsicKind::InsertElement) {
		inferElementIntrinsic(kind);
		return;
	}
	if (isAtomicIntrinsicKind(kind)) {
		inferAtomicIntrinsic(kind);
		return;
	}
	if (kind == IntrinsicKind::LifecycleValue) {
		inferLifecycleValueIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Subject) {
		inferSubjectIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::CommandLineArgumentCount || kind == IntrinsicKind::CommandLineArgumentValues) {
		inferCommandLineArgumentsIntrinsic(kind);
		return;
	}
	if (kind == IntrinsicKind::AddressOf) {
		inferAddressOfIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Dereference) {
		inferDereferenceIntrinsic();
		return;
	}
	if (isExternalCallIntrinsicKind(kind)) {
		inferExternalCallIntrinsic(kind);
		return;
	}
	if (kind == IntrinsicKind::Function) {
		inferFunctionIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Cast) {
		inferCastIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Type) {
		inferTypeIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::NumberRule) {
		inferNumberRuleIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Fix) {
		inferFixIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::TypeOf) {
		inferTypeOfIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::TypeExtent) {
		inferTypeExtentIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Select) {
		inferSelectIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::SizeOf) {
		inferSizeOfIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::BuildInfo) {
		inferBuildInfoIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::TargetIs) {
		inferTargetIsIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::ShaderStageIs) {
		inferShaderStageIsIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Array) {
		inferArrayIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Vector) {
		inferVectorIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Matrix) {
		inferMatrixIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::AddPointerDepth) {
		inferAddPointerDepthIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Construct) {
		inferConstructIntrinsic();
		return;
	}
	if (kind == IntrinsicKind::Property) {
		inferPropertyIntrinsic();
		return;
	}
	std::string uri = (expr && expr->range.line && expr->range.line->sourceFile) ? expr->range.line->sourceFile->uri : "";
	int line = (expr && expr->range.line) ? expr->range.line->sourceFileLineIndex + 1 : -1;
	crashUnimplementedIntrinsic("type inference", expr->intrinsicName, uri, line);
}
