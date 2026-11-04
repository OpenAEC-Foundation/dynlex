#include "intrinsic_preflight_inference.inl"
#include "intrinsics/aggregate_inference.inl"
#include "intrinsics/arithmetic_inference.inl"
#include "intrinsics/atomic_inference.inl"
#include "intrinsics/bitwise_inference.inl"
#include "intrinsics/boolean_inference.inl"
#include "intrinsics/call_inference.inl"
#include "intrinsics/construction_inference.inl"
#include "intrinsics/custom_inference.inl"
#include "intrinsics/type_inference.inl"
#include "intrinsics/value_inference.inl"
#include "intrinsics/void_inference.inl"

void ExpressionInferenceSupport::inferIntrinsic() {
	const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	if (!validateIntrinsic(kind, info))
		return;
	recordIntrinsicMemoryViolation(expr, context, flexBindingFrameStack);
	if (isShaderRuntimeIntrinsicKind(kind) && !validateShaderRuntimeIntrinsic(expr, kind, context))
		return;
	if (info) {
		switch (info->returnKind) {
		case IntrinsicReturnKind::SameAsArgs:
			inferArithmeticIntrinsic(kind);
			break;
		case IntrinsicReturnKind::SameAsInts:
			inferBitwiseIntrinsic();
			break;
		case IntrinsicReturnKind::Bool:
			inferBooleanIntrinsic(kind);
			break;
		case IntrinsicReturnKind::Void:
			inferVoidIntrinsic(kind);
			break;
		case IntrinsicReturnKind::Float:
			expr->type = {DataType::Kind::Float, 4};
			break;
		case IntrinsicReturnKind::Custom:
			inferCustomIntrinsic(kind);
			break;
		}
	}
	if (context.typesValid) {
		markIntrinsicImpurityIfNeeded(expr, context, flexBindingFrameStack);
		context.setExpressionValue(expr, inferIntrinsicCompileTimeValue(expr, context, flexBindingFrameStack));
	}
}
