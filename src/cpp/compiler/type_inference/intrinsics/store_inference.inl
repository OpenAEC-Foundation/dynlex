#pragma once

static std::string storeValueText(Expression *valueExpression) {
	if (!valueExpression || valueExpression->range.subString.empty())
		return "<expression>";
	std::string_view text = valueExpression->range.subString;
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
		start++;
	size_t end = text.size();
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
		end--;
	return std::string(text.substr(start, end - start));
}

static void setInvalidStoreDestinationFailure(Expression *destinationExpr, InferenceContext &context) {
	std::string destinationText = destinationExpr && !destinationExpr->range.subString.empty()
									  ? (std::string)destinationExpr->range.subString
									  : "<expression>";
	std::string detail = "assignment target '" + destinationText + "' is not writable";
	context.setTypeFailure(detail);
	context.fail(buildFailureDetailDiagnostic(destinationExpr->range, std::move(detail)));
}

static void inferStoreEffects(Expression *expr, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack) {
	Expression *destinationSourceExpr =
		resolveExpressionBindingWithCallerScope(expr->arguments[1], flexBindingFrameStack).expression;
	ResolvedBindingLayers resolvedDestination =
		resolveInferenceBindingLayers(expr->arguments[1], flexBindingFrameStack, &context);
	Expression *destinationExpr = resolvedDestination.expression;
	BindingFrameStack destinationBindingFrameStack = std::move(resolvedDestination.bindingFrameStack);
	ResolvedBindingLayers resolvedValue = resolveInferenceBindingLayers(expr->arguments[2], flexBindingFrameStack, &context);
	Expression *valueExpr = resolvedValue.expression;
	BindingFrameStack valueBindingFrameStack = std::move(resolvedValue.bindingFrameStack);
	ScopedRecursiveInferenceObservation valueObservation(context, context.currentInstantiation);
	DataType valueType = ensureExpressionTypeWithCurrentGrouping(valueExpr, context, valueBindingFrameStack);
	bool valueDependsOnInProgressInstantiation = valueObservation.observed();

	if (valueType.isMetaType()) {
		context.setTypeFailure("compile time type value used at runtime");
		return;
	}
	if (!valueType.isDeduced()) {
		if (valueDependsOnInProgressInstantiation && context.currentInstantiation)
			return;
		// A silent pass would let an unresolved store reach codegen. Fail the
		// pass instead, so grouping trials and reinference retry, and a final
		// diagnostic names the actual cause.
		std::string valueText = storeValueText(valueExpr);
		std::vector<RelatedInfo> unboundParameterInfo;
		appendUnboundParameterTrace(context, valueExpr, unboundParameterInfo);
		context.setTypeFailure(renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, valueExpr ? valueExpr->range : expr->range), "store value unresolved",
			"message", {{"value", valueText}}
		));
		context.insertTypeFailureCause(std::move(unboundParameterInfo));
		return;
	}
	if (!valueType.isRuntimeValueType()) {
		std::string valueText = storeValueText(valueExpr);
		context.setTypeFailure(renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, valueExpr ? valueExpr->range : expr->range), "store value not runtime",
			"message", {{"value", valueText}}
		));
		return;
	}

	if (destinationExpr->kind == Expression::Kind::Variable && destinationExpr->variable) {
		Section *section = destinationExpr->range.line ? destinationExpr->range.line->section : nullptr;
		Variable *variable = section ? section->findVariable(destinationExpr->variable->name) : nullptr;
		if (!variable) {
			setInvalidStoreDestinationFailure(destinationSourceExpr, context);
			return;
		}
		if (variable->hasDeclaredTypeConstraint()) {
			VariableReference *constraintReference = variable->firstDeclaredTypeConstraintReference();
			requireCompilerInvariant(constraintReference, "typed variable has no constraint source reference");
			requireCompilerInvariant(
				variable->declaredTypeConstraint.isResolved(), "typed variable reached assignment with an unresolved constraint"
			);
			CompileTimeValue assignedValue = context.lookupExpressionValue(valueExpr);
			bool accepted = variable->declaredTypeConstraint.accepts(valueType, isCompileTimeKnown(assignedValue));
			if (!accepted && variable->declaredType.isDeduced() &&
				tryApplyUserConversion(valueExpr, variable->declaredType, true, context, valueBindingFrameStack)) {
				valueType = effectiveInferredExpressionType(valueExpr);
				assignedValue = context.lookupExpressionValue(valueExpr);
				accepted = variable->declaredTypeConstraint.accepts(valueType, isCompileTimeKnown(assignedValue));
			}
			if (!accepted) {
				if (!context.typesValid)
					return;
				Range diagnosticRange = valueExpr->range;
				const SyntaxConfig &syntax = syntaxConfigForRange(context.parseContext, diagnosticRange);
				std::string detail = renderConfiguredMessage(
					syntax, "variable type constraint mismatch", "message",
					{{"name", variable->name},
					 {"constraint", constraintReference->declaredTypeConstraintName},
					 {"actual_type", typeToUserName(valueType)}}
				);
				context.setTypeFailure(detail);
				if (!context.trial) {
					Diagnostic diagnostic = buildFailureDetailDiagnostic(diagnosticRange, detail);
					diagnostic.relatedInfo.push_back(
						{renderConfiguredMessage(
							 syntax, "variable type constraint mismatch", "related declaration",
							 {{"name", variable->name}, {"constraint", constraintReference->declaredTypeConstraintName}}
						 ),
						 constraintReference->declaredTypeConstraintRange}
					);
					context.addDiagnosticWithCurrentTrace(std::move(diagnostic));
				}
				return;
			}
		}

		DataType mergedVariableType = valueType;
		if (variable->type.isDeduced() && !mergeVariableAssignmentType(variable->type, valueType, mergedVariableType) &&
			tryApplyUserConversion(valueExpr, variable->type, true, context, valueBindingFrameStack)) {
			valueType = effectiveInferredExpressionType(valueExpr);
			mergedVariableType = valueType;
		}
		if (!variable->type.isDeduced() || mergeVariableAssignmentType(variable->type, valueType, mergedVariableType)) {
			if (context.trial && context.trialJournal)
				context.trialJournal->recordVariableWrite(variable);
			if (!variable->type.isDeduced() || variable->type == valueType || variable->type != mergedVariableType)
				commitVariableTypeFromValue(variable, valueExpr, mergedVariableType);
			destinationExpr->type = variable->type;
			expr->arguments[1]->type = variable->type;
			CompileTimeValue assignedValue = context.lookupExpressionValue(valueExpr);
			if (variable->isGlobal)
				context.noteWrittenGlobalReference(variable->definition);
			context.setKnownConstant(variable->definition, assignedValue);
			context.setAddressProvenance(
				variable->definition, inferAddressProvenance(valueExpr, context, valueBindingFrameStack)
			);
			return;
		}

		context.setTypeFailure(renderConfiguredMessage(
			syntaxConfigForRange(context.parseContext, valueExpr ? valueExpr->range : expr->range), "variable type change",
			"message",
			{{"name", variable->name}, {"from_type", typeToUserName(variable->type)}, {"to_type", typeToUserName(valueType)}}
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
		setInvalidStoreDestinationFailure(destinationSourceExpr, context);
		return;
	}

	ResolvedBindingLayers resolvedOwner =
		resolveInferenceBindingLayers(destinationExpr->arguments[1], destinationBindingFrameStack, &context);
	Expression *ownerExpr = resolvedOwner.expression;
	BindingFrameStack ownerBindingFrameStack = std::move(resolvedOwner.bindingFrameStack);
	DataType instanceType =
		ownerExpr ? ensureExpressionTypeWithCurrentGrouping(ownerExpr, context, ownerBindingFrameStack) : DataType{};
	if (instanceType.kind != DataType::Kind::Class || !instanceType.classDefinition || instanceType.classInstIndex < 0) {
		setInvalidStoreDestinationFailure(destinationSourceExpr, context);
		return;
	}
	std::optional<AddressProvenance> ownerLValueProvenance;
	if (!instanceType.isPointer()) {
		ownerLValueProvenance = inferLValueAddressProvenance(ownerExpr, context, ownerBindingFrameStack, false);
		if (!ownerLValueProvenance) {
			setInvalidStoreDestinationFailure(destinationSourceExpr, context);
			return;
		}
	}

	Expression *propertyExpr =
		resolveInferenceBindingLayers(destinationExpr->arguments[2], destinationBindingFrameStack, &context).expression;
	std::string fieldName;
	if (auto *str = std::get_if<std::string>(&propertyExpr->literalValue))
		fieldName = *str;
	if (fieldName.empty()) {
		setInvalidStoreDestinationFailure(destinationSourceExpr, context);
		return;
	}

	ClassDefinition *classDefinition = instanceType.classDefinition;
	AddressProvenance assignedProvenance;
	auto updateOwnerAddressProvenance = [&]() {
		assignedProvenance = inferAddressProvenance(valueExpr, context, valueBindingFrameStack);
		AddressProvenance ownerStorage = instanceType.isPointer()
											 ? inferAddressProvenance(ownerExpr, context, ownerBindingFrameStack)
											 : *ownerLValueProvenance;
		std::unordered_set<VariableReference *> ownerTargets = possibleAddressTargets(context, ownerStorage);
		if (ownerStorage.unknown)
			noteUnknownAddressWrite(context);
		for (VariableReference *ownerTarget : ownerTargets) {
			AddressProvenance ownerProvenance = context.lookupAddressProvenance(ownerTarget);
			joinAddressProvenance(ownerProvenance, assignedProvenance);
			context.setAddressProvenance(ownerTarget, std::move(ownerProvenance));
			noteAddressTargetWrite(context, ownerTarget);
		}
	};
	for (size_t i = 0; i < classDefinition->fields.size(); i++) {
		if (classDefinition->fields[i].name != fieldName)
			continue;
		const DataType &currentFieldType = classDefinition->instantiations[instanceType.classInstIndex].fieldTypes[i];
		if (currentFieldType.isDeduced()) {
			DataType mergedFieldType;
			if (!mergeVariableAssignmentType(currentFieldType, valueType, mergedFieldType)) {
				if (tryApplyUserConversion(valueExpr, currentFieldType, true, context, valueBindingFrameStack)) {
					valueType = effectiveInferredExpressionType(valueExpr);
					mergedFieldType = valueType;
				} else {
					if (!context.typesValid)
						return;
					std::string destinationName = destinationSourceExpr && !destinationSourceExpr->range.subString.empty()
													  ? std::string(destinationSourceExpr->range.subString)
													  : fieldName;
					context.setTypeFailure(renderConfiguredMessage(
						syntaxConfigForRange(context.parseContext, valueExpr ? valueExpr->range : expr->range),
						"variable type change", "message",
						{{"name", destinationName},
						 {"from_type", typeToUserName(currentFieldType)},
						 {"to_type", typeToUserName(valueType)}}
					));
					if (!context.trial) {
						context.addDiagnosticWithCurrentTrace(buildAssignmentTypeChangeDiagnostic(
							destinationName, currentFieldType, {}, {}, valueExpr, valueType, context.parseContext
						));
					}
					return;
				}
			}
			if (mergedFieldType == currentFieldType) {
				updateOwnerAddressProvenance();
				return;
			}
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
		updateOwnerAddressProvenance();
		return;
	}

	setInvalidStoreDestinationFailure(destinationSourceExpr, context);
}
