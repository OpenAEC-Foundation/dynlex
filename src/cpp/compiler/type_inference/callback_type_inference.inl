static bool isCallbackTypeIntrinsic(IntrinsicKind kind) {
	return kind == IntrinsicKind::FunctionType || kind == IntrinsicKind::FunctionResultType ||
		   kind == IntrinsicKind::FunctionParameterType;
}

static void inferCallbackTypeIntrinsic(Expression *expression, InferenceContext &context, const BindingFrameStack &bindings) {
	IntrinsicKind kind = intrinsicKind(expression->intrinsicName);
	auto fail = [&](std::string message) {
		context.fail(buildFailureDetailDiagnostic(expression->range, std::move(message)));
	};
	if (kind == IntrinsicKind::FunctionType) {
		if (expression->arguments.size() == 1) {
			TypeConstraint constraint = TypeConstraint::any();
			constraint.kind = DataType::Kind::Function;
			constraint.pointerDepth = 0;
			expression->type = {DataType::Kind::Constraint};
			context.setExpressionValue(expression, constraint);
			return;
		}
		std::vector<DataType> types;
		for (size_t index = 1; index < expression->arguments.size(); index++) {
			Expression *&argument = expression->arguments[index];
			ensureExpressionType(argument, context, bindings);
			auto reference = getCompileTimeTypeReferenceValue(resolveStoredCompileTimeValue(argument, bindings, &context));
			if (!reference) {
				fail("A callback signature requires concrete type arguments");
				return;
			}
			DataType type = reference->type.toReferencedType();
			if (!type.isConcrete() || type.isMetaType() || (index > 1 && !type.isRuntimeValueType())) {
				fail("A callback signature requires runtime parameter types and a runtime result type or nothing");
				return;
			}
			types.push_back(type);
		}
		DataType result = types.front();
		types.erase(types.begin());
		expression->type = makeFunctionType(result, std::move(types)).asTypeReference();
		context.setExpressionValue(expression, TypeReferenceValue::exact(expression->type));
		return;
	}
	DataType type = ensureExpressionType(expression->arguments[1], context, bindings);
	if (type.kind == DataType::Kind::Type)
		type = type.toReferencedType();
	if (!type.isFunction() || !concreteFunctionSignature(type.functionSignature)) {
		fail("Inspecting a callback signature requires a typed callback");
		return;
	}
	DataType selected = type.functionSignature->result;
	if (kind == IntrinsicKind::FunctionParameterType) {
		int index = 0;
		if (!resolveStoredCompileTimeInteger(expression->arguments[2], bindings, index, &context) || index < 0 ||
			static_cast<size_t>(index) >= type.functionSignature->parameters.size()) {
			fail("The callback parameter index is outside its signature");
			return;
		}
		selected = type.functionSignature->parameters[static_cast<size_t>(index)];
	}
	expression->type = selected.asTypeReference();
	context.setExpressionValue(expression, TypeReferenceValue::exact(expression->type));
}
