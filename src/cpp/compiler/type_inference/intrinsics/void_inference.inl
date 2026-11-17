void ExpressionInferenceSupport::inferVoidIntrinsic(IntrinsicKind kind) {
	if (kind == IntrinsicKind::CheckDeallocation &&
		!ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack).isPointer()) {
		failIntrinsicArgumentRequirement(1, "a pointer");
		return;
	}
	CompileTimeValue conditionValue;
	DataType sectionConditionType;
	if (kind == IntrinsicKind::Return) {
		if (context.currentInstantiation) {
			if (context.trial) {
				requireCompilerInvariant(context.trialJournal, "trial return inference requires a rollback journal");
				context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
			}
			context.currentInstantiation->hasReturnIntrinsic = true;
		}
		Expression *returnValueExpression = expr->arguments.size() > 1 ? expr->arguments[1] : nullptr;
		ResolvedBindingLayers resolvedReturnValue =
			returnValueExpression ? resolveExpressionBindingWithCallerScope(returnValueExpression, flexBindingFrameStack)
								  : ResolvedBindingLayers{nullptr, flexBindingFrameStack};
		Expression *sourceReturnValueExpression = resolvedReturnValue.expression;
		ScopedRecursiveInferenceObservation returnValueObservation(context, context.currentInstantiation);
		DataType retType = returnValueExpression ? ensureExpressionType(returnValueExpression, context, flexBindingFrameStack)
												 : DataType{DataType::Kind::Void};
		if (!retType.isDeduced()) {
			if (returnValueObservation.ownerObserved() && context.currentInstantiation) {
				expr->type = {DataType::Kind::Void};
				return;
			}
			context.setTypeFailure("return value type is unresolved");
			return;
		}
		if (context.currentInstantiation) {
			if (!reconcileFunctionReturnType(
					context, expr, sourceReturnValueExpression, retType, resolvedReturnValue.bindingFrameStack
				))
				return;
		} else if (retType.kind != DataType::Kind::Void) {
			Range returnRange = context.activeFlexCallStack.empty() ? expr->range : context.activeFlexCallStack.back()->range;
			DataType programReturnType{DataType::Kind::Int, 4};
			if (!DataType::supportsRuntimeConversion(retType, programReturnType)) {
				std::string detail = renderConfiguredMessage(
					syntaxConfigForRange(context.parseContext, returnRange), "program return value invalid", "message",
					{{"value_type", typeToUserName(retType)}}
				);
				failWithDetail(returnRange, detail);
				return;
			}
		}
		if (expr->arguments.size() <= 1)
			context.setExpressionValue(expr, {});
		else
			context.setExpressionValue(expr, context.lookupExpressionValue(expr->arguments[1]));
		if (context.currentInstantiation) {
			if (context.trial) {
				requireCompilerInvariant(context.trialJournal, "trial return provenance requires a rollback journal");
				context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
			}
			AddressProvenance returnProvenance =
				returnValueExpression ? inferAddressProvenance(returnValueExpression, context, flexBindingFrameStack)
									  : AddressProvenance{};
			joinAddressProvenance(context.currentInstantiation->returnAddressProvenance, returnProvenance);
			context.currentInstantiation->hasReturnAddressProvenance = true;
			bool returnsPointer = returnValueExpression && effectiveInferredExpressionType(returnValueExpression).isPointer();
			if (returnsPointer && !context.currentInstantiation->returnPointerStorageAmbiguous) {
				std::optional<std::string> pointerStorageParameter =
					pointerStorageParameterName(returnValueExpression, context, flexBindingFrameStack);
				if (!pointerStorageParameter) {
					context.currentInstantiation->returnPointerStorageParameterName.reset();
					context.currentInstantiation->returnPointerStorageAmbiguous = true;
				} else if (!context.currentInstantiation->returnPointerStorageParameterName) {
					context.currentInstantiation->returnPointerStorageParameterName = std::move(pointerStorageParameter);
				} else if (*context.currentInstantiation->returnPointerStorageParameterName != *pointerStorageParameter) {
					context.currentInstantiation->returnPointerStorageParameterName.reset();
					context.currentInstantiation->returnPointerStorageAmbiguous = true;
				}
			}
		}
		if (!context.typesValid)
			return;
	}
	if ((kind == IntrinsicKind::If || kind == IntrinsicKind::ElseIf || kind == IntrinsicKind::LoopWhile) &&
		expr->arguments.size() > 1) {
		ScopedRecursiveInferenceObservation conditionObservation(context, context.currentInstantiation);
		DataType operandType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		if (!operandType.isDeduced() && conditionObservation.ownerObserved()) {
			// Keep traversing the body so a later return can resolve this
			// recursive condition. The owning pass retries its type check.
			expr->sectionOutcome = sectionOutcomeForIntrinsic(kind);
			expr->type = {DataType::Kind::Void};
			return;
		}
		if (!isLogicalOperandType(operandType)) {
			std::string operatorLabel = kind == IntrinsicKind::If		? "if condition"
										: kind == IntrinsicKind::ElseIf ? "else if condition"
																		: "loop while condition";
			setConfiguredTypeFailure(
				expr->range, "logical operator operand invalid", "message",
				{{"operator", operatorLabel}, {"value_type", typeToUserName(operandType)}}
			);
			return;
		}
		conditionValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
		sectionConditionType = operandType;
	}
	if ((kind == IntrinsicKind::Switch || kind == IntrinsicKind::Case) && expr->arguments.size() > 1) {
		DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		if (!valueType.isInteger()) {
			failConfigured(
				expr->arguments[1]->range,
				kind == IntrinsicKind::Switch ? "switch requires integer" : "case value must be constant integer"
			);
			return;
		}
		conditionValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
		sectionConditionType = valueType;
		if (kind == IntrinsicKind::Case &&
			(!getCompileTimeIntegerBits(conditionValue, valueType.numericSize * 8) ||
			 !isStructurallyCompileTimeConstant(expr->arguments[1], flexBindingFrameStack, context))) {
			failConfigured(expr->arguments[1]->range, "case value must be constant integer");
			return;
		}
	}
	if (context.typesValid)
		expr->sectionOutcome = sectionOutcomeForIntrinsic(kind, conditionValue, sectionConditionType);
	if (kind == IntrinsicKind::Store)
		inferStoreEffects(expr, context, flexBindingFrameStack);
	if ((kind == IntrinsicKind::StoreAt || kind == IntrinsicKind::InitializeAt) &&
		!inferPointerStoreEffects(
			expr->arguments[1], flexBindingFrameStack, expr->arguments[2], flexBindingFrameStack, expr->range, context
		))
		return;
	if (kind == IntrinsicKind::ExecuteBody && !inferSectionFlexCallerBody(expr, context))
		return;
	if (kind == IntrinsicKind::SetSubject) {
		ScopedRecursiveInferenceObservation subjectObservation(context, context.currentInstantiation);
		DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		if (!valueType.isDeduced() && subjectObservation.ownerObserved() && context.currentInstantiation) {
			context.currentSubject = InferenceContext::SubjectState::assigned(expr);
			expr->type = {DataType::Kind::Void};
			return;
		}
		if (!valueType.isDeduced() || valueType.kind == DataType::Kind::Void) {
			failWithDetail(expr->arguments[1]->range, "the subject must have a value");
			return;
		}
		recordSubjectTypeSource(expr, valueType, context, flexBindingFrameStack);
		context.currentSubject = InferenceContext::SubjectState::assigned(expr);
	}
	if (kind == IntrinsicKind::ShaderOutput) {
		DataType outputType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
		DataType shaderVector{DataType::Kind::Vector};
		shaderVector.arraySize = 4;
		shaderVector.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
		if (!DataType::supportsRuntimeConversion(outputType, shaderVector)) {
			setConfiguredTypeFailure(
				expr->range, "shader output requires four numbers", "message", {{"value_type", typeToUserName(outputType)}}
			);
			return;
		}
	}
	expr->type = {DataType::Kind::Void};
}
