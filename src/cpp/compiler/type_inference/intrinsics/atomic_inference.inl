if (isAtomicIntrinsicKind(kind)) {
	if (context.parseContext.options.emitWASM || context.parseContext.options.emitSPIRV) {
		failWithDetail(expr->range, "Atomic operations are only available when emitting native CPU code", 0);
		break;
	}
	const size_t orderIndex = expr->arguments.size() - 1;
	Expression *orderExpression = resolveThroughFlexBindings(expr->arguments[orderIndex]);
	if (!orderExpression || !std::holds_alternative<std::string>(orderExpression->literalValue)) {
		failIntrinsicArgumentRequirement(orderIndex, "a memory-order string literal");
		break;
	}
	std::optional<AtomicMemoryOrder> order = parseAtomicMemoryOrder(std::get<std::string>(orderExpression->literalValue));
	if (!order) {
		failWithDetail(orderExpression->range, "Atomic memory order must be relaxed, acquire, release, acq_rel, or seq_cst", 0);
		break;
	}
	if (!atomicOrderIsValidFor(kind, *order)) {
		failWithDetail(orderExpression->range, "Atomic memory order is invalid for this operation", 0);
		break;
	}
	DataType pointerType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (!pointerType.isDeduced() || !pointerType.isPointer()) {
		failWithDetail(expr->arguments[1]->range, "Atomic operation requires a pointer to a concrete scalar", 0);
		break;
	}
	DataType valueType = pointerType.dereferenced();
	bool scalar = valueType.isPointer() || valueType.kind == DataType::Kind::Bool ||
		((valueType.kind == DataType::Kind::Int || valueType.kind == DataType::Kind::UInt || valueType.kind == DataType::Kind::Float) &&
		 (valueType.numericSize == 1 || valueType.numericSize == 2 || valueType.numericSize == 4 || valueType.numericSize == 8));
	if (!valueType.isConcrete() || !scalar || typeHasManagedLifecycle(valueType)) {
		failWithDetail(expr->arguments[1]->range, "Atomic operation requires a supported concrete scalar pointee", 0);
		break;
	}
	if (kind == IntrinsicKind::AtomicFetchAdd || kind == IntrinsicKind::AtomicFetchSub) {
		if (!valueType.isInteger()) {
			failWithDetail(expr->arguments[1]->range, "Atomic fetch add and fetch sub require an integer pointee", 0);
			break;
		}
	}
	if (kind != IntrinsicKind::AtomicLoad) {
		DataType operandType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
		if (operandType != valueType) {
			failWithDetail(expr->arguments[2]->range, "Atomic operation value type must exactly match its pointer pointee", 0);
			break;
		}
		applyStoreThroughAddress(
			context, inferAddressProvenance(expr->arguments[1], context, flexBindingFrameStack), expr->arguments[2],
			flexBindingFrameStack
		);
	}
	expr->type = kind == IntrinsicKind::AtomicStore ? DataType{DataType::Kind::Void} : valueType;
	context.setExpressionValue(expr, {});
	break;
}

#include "aggregate_inference.inl"
