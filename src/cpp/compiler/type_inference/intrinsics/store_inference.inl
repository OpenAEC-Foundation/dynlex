#pragma once

static void setInvalidStoreDestinationFailure(Expression *destinationExpr, InferenceContext &context) {
	std::string destinationText = destinationExpr && !destinationExpr->range.subString.empty()
									  ? (std::string)destinationExpr->range.subString
									  : "<expression>";
	context.setTypeFailure("assignment target '" + destinationText + "' is not writable");
}

static void inferStoreEffects(Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack) {
	Expression *destinationSourceExpr = resolveThroughBindings(expr->arguments[1], flexBindingFrameStack);
	BindingFrameStack destinationBindingFrameStack;
	Expression *destinationExpr =
		resolveThroughBindingsDeep(expr->arguments[1], flexBindingFrameStack, destinationBindingFrameStack);
	BindingFrameStack valueBindingFrameStack;
	Expression *valueExpr = resolveThroughBindingsDeep(expr->arguments[2], flexBindingFrameStack, valueBindingFrameStack);
	DataType valueType = ensureExpressionTypeWithCurrentGrouping(valueExpr, context, valueBindingFrameStack);

	if (valueType.kind == DataType::Kind::Type) {
		context.setTypeFailure("compile time type value used at runtime");
		return;
	}
	if (!valueType.isDeduced())
		return;

	if (destinationExpr->kind == Expression::Kind::Variable && destinationExpr->variable) {
		Section *section = destinationExpr->range.line ? destinationExpr->range.line->section : nullptr;
		Variable *variable = section ? section->findVariable(destinationExpr->variable->name) : nullptr;
		if (!variable) {
			setInvalidStoreDestinationFailure(destinationExpr, context);
			return;
		}

		if (!variable->type.isDeduced() || isVariableAssignmentCompatible(variable->type, valueType)) {
			if (context.trial && context.trialJournal)
				context.trialJournal->recordVariableWrite(variable);
			if (!variable->type.isDeduced() || variable->type == valueType)
				commitVariableTypeFromValue(variable, valueExpr, valueType);
			destinationExpr->type = variable->type;
			CompileTimeValue assignedValue = context.lookupExpressionValue(valueExpr);
			if (!context.trial)
				destinationExpr->compileTimeValue = assignedValue;
			if (variable->isGlobal)
				context.noteWrittenGlobalReference(variable->definition);
			context.setKnownConstant(variable->definition, assignedValue);
			if (context.inLoopMutationScope()) {
				context.noteLoopMutation(variable->definition);
				context.setKnownConstant(variable->definition, {});
			}
			return;
		}

		context.setTypeFailure(renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, valueExpr ? valueExpr->range : expr->range), "variable type change",
			"message",
			{{"name", variable->name},
			 {"from_type", typeToUserName(variable->type, context.parseContext)},
			 {"to_type", typeToUserName(valueType, context.parseContext)}}
		));
		if (!context.trial) {
			context.addDiagnosticWithCurrentTrace(
				buildVariableTypeChangeDiagnostic(variable, valueExpr, valueType, context.parseContext)
			);
		}
		return;
	}

	if (destinationExpr->kind != Expression::Kind::IntrinsicCall ||
		intrinsicKind(destinationExpr->intrinsicName) != IntrinsicKind::Property) {
		setInvalidStoreDestinationFailure(destinationExpr, context);
		return;
	}

	BindingFrameStack resolvedBindingFrameStack = flexBindingFrameStack;
	destinationBindingFrameStack.forEachFrame([&](const BindingFrame &frame) {
		pushBindingScope(resolvedBindingFrameStack, frame);
	});
	BindingFrameStack ignoredBindingFrameStack;
	Expression *ownerExpr =
		resolveThroughBindingsDeep(destinationExpr->arguments[1], resolvedBindingFrameStack, ignoredBindingFrameStack);
	DataType instanceType =
		ownerExpr ? concretizeClassType(ensureExpressionTypeWithCurrentGrouping(ownerExpr, context, resolvedBindingFrameStack))
				  : DataType{};
	if (instanceType.kind != DataType::Kind::Class || !instanceType.classDefinition || instanceType.classInstIndex < 0) {
		setInvalidStoreDestinationFailure(destinationExpr, context);
		return;
	}

	Expression *propertyExpr = resolveThroughBindings(destinationExpr->arguments[2], resolvedBindingFrameStack);
	std::string fieldName;
	if (auto *str = std::get_if<std::string>(&propertyExpr->literalValue))
		fieldName = *str;
	if (fieldName.empty()) {
		setInvalidStoreDestinationFailure(destinationExpr, context);
		return;
	}

	ClassDefinition *classDefinition = instanceType.classDefinition;
	for (size_t i = 0; i < classDefinition->fields.size(); i++) {
		if (classDefinition->fields[i].name != fieldName)
			continue;
		const DataType &currentFieldType = classDefinition->instantiations[instanceType.classInstIndex].fieldTypes[i];
		if (currentFieldType.isDeduced()) {
			if (isVariableAssignmentCompatible(currentFieldType, valueType))
				return;
			std::string destinationName = destinationSourceExpr && !destinationSourceExpr->range.subString.empty()
											  ? std::string(destinationSourceExpr->range.subString)
											  : fieldName;
			context.setTypeFailure(renderConfiguredMessage(
				syntaxConfigForRange(context.parseContext, valueExpr ? valueExpr->range : expr->range), "variable type change",
				"message",
				{{"name", destinationName},
				 {"from_type", typeToUserName(currentFieldType, context.parseContext)},
				 {"to_type", typeToUserName(valueType, context.parseContext)}}
			));
			if (!context.trial) {
				context.addDiagnosticWithCurrentTrace(buildAssignmentTypeChangeDiagnostic(
					destinationName, currentFieldType, {}, {}, valueExpr, valueType, context.parseContext
				));
			}
			return;
		}
		int refinedInstIndex =
			getRefinedClassInstantiationIndex(context, classDefinition, instanceType.classInstIndex, i, valueType);
		if (refinedInstIndex < 0)
			return;
		if (ownerExpr && ownerExpr->kind == Expression::Kind::Variable && ownerExpr->variable) {
			Section *ownerSection = ownerExpr->range.line ? ownerExpr->range.line->section : nullptr;
			Variable *ownerVariable = ownerSection ? ownerSection->findVariable(ownerExpr->variable->name) : nullptr;
			if (ownerVariable && ownerVariable->type.kind == DataType::Kind::Class &&
				ownerVariable->type.classDefinition == classDefinition) {
				if (context.trial && context.trialJournal)
					context.trialJournal->recordVariableWrite(ownerVariable);
				ownerVariable->type.classInstIndex = refinedInstIndex;
			}
		}
		return;
	}

	setInvalidStoreDestinationFailure(destinationExpr, context);
}
