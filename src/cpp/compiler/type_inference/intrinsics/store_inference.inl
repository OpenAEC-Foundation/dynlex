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

struct ActiveConstraintPath {
	std::vector<DataType> argumentTypes;
	std::vector<CompileTimeValue> argumentValues;
	std::optional<size_t> constrainedParameterIndex;
};

static std::optional<ActiveConstraintPath> activeConstraintPath(
	PatternDefinition *definition, size_t pathIndex, const Instantiation &instantiation,
	std::string_view constrainedParameter = {}
) {
	const PatternPathSignature &signature = definition->signaturePaths[pathIndex];
	if (signature.parameters.size() != instantiation.argumentTypes.size())
		return std::nullopt;
	ActiveConstraintPath result;
	result.argumentTypes = instantiation.argumentTypes;
	result.argumentValues.reserve(signature.parameters.size());
	bool matches = true;
	size_t parameterIndex = 0;
	forEachPatternParameterName(definition, pathIndex, [&](const std::string &name, PatternTreeNode *, size_t) {
		if (!matches || parameterIndex >= instantiation.argumentTypes.size()) {
			matches = false;
			parameterIndex++;
			return;
		}
		auto parameterType = instantiation.parameterTypesByName.find(name);
		if (parameterType == instantiation.parameterTypesByName.end() ||
			parameterType->second != instantiation.argumentTypes[parameterIndex]) {
			matches = false;
		}
		auto constantValue = instantiation.constantParameterValues.find(name);
		result.argumentValues.push_back(
			constantValue == instantiation.constantParameterValues.end() ? CompileTimeValue{} : constantValue->second
		);
		if (name == constrainedParameter)
			result.constrainedParameterIndex = parameterIndex;
		parameterIndex++;
	});
	if (!matches || parameterIndex != instantiation.argumentTypes.size() ||
		(!constrainedParameter.empty() && !result.constrainedParameterIndex)) {
		return std::nullopt;
	}
	return result;
}

static std::optional<ActiveConstraintPath> activeFlexConstraintPath(
	PatternDefinition *definition, size_t pathIndex, const BindingFrameStack &bindingFrameStack, InferenceContext &context
) {
	ActiveConstraintPath result;
	result.argumentTypes.reserve(definition->signaturePaths[pathIndex].parameters.size());
	result.argumentValues.reserve(definition->signaturePaths[pathIndex].parameters.size());
	bool matches = true;
	forEachPatternParameterName(definition, pathIndex, [&](const std::string &name, PatternTreeNode *, size_t) {
		if (!matches)
			return;
		VariableReference *parameterDefinition = findPatternParameterDefinition(definition, name);
		requireCompilerInvariant(parameterDefinition, "dependent flex local constraint has no parameter definition");
		ResolvedBindingLayers bound = resolveBindingReferenceWithCallerScope(parameterDefinition, nullptr, bindingFrameStack);
		if (!bound.expression) {
			matches = false;
			return;
		}
		Expression *argument = bound.expression;
		DataType argumentType = ensureExpressionTypeWithCurrentGrouping(argument, context, bound.bindingFrameStack);
		if (!argumentType.isDeduced()) {
			matches = false;
			return;
		}
		result.argumentTypes.push_back(argumentType);
		result.argumentValues.push_back(resolveStoredCompileTimeValue(argument, bound.bindingFrameStack, &context));
	});
	if (!matches || result.argumentTypes.size() != definition->signaturePaths[pathIndex].parameters.size())
		return std::nullopt;
	return result;
}

static TypeConstraint materializeDependentParameterConstraint(Section *section, Variable *variable, InferenceContext &context) {
	requireCompilerInvariant(section && variable, "dependent variable constraint has no section variable");
	while (section) {
		auto candidate = section->variables.find(variable->name);
		if (candidate != section->variables.end() && candidate->second == variable)
			break;
		section = section->parent;
	}
	requireCompilerInvariant(section, "dependent variable constraint has no owning section");
	requireCompilerInvariant(
		context.currentInstantiation, "dependent variable constraint reached a store without an active instantiation"
	);
	Instantiation &instantiation = *context.currentInstantiation;
	std::optional<TypeConstraint> materializedConstraint;
	for (PatternDefinition *definition : section->patternDefinitions) {
		for (size_t pathIndex = 0; pathIndex < definition->signaturePaths.size(); pathIndex++) {
			std::optional<ActiveConstraintPath> path =
				activeConstraintPath(definition, pathIndex, instantiation, variable->name);
			if (!path)
				continue;
			auto resolved = resolveCompiledPatternConstraint(
				definition, pathIndex, *path->constrainedParameterIndex, path->argumentTypes, path->argumentValues
			);
			requireCompilerInvariant(
				resolved.has_value(), "active instantiation could not materialize its variable constraint"
			);
			TypeConstraint candidate = resolved->effectiveConstraint();
			if (!materializedConstraint) {
				materializedConstraint = std::move(candidate);
			} else {
				requireCompilerInvariant(
					materializedConstraint->equivalentTo(candidate),
					"active instantiation paths disagree on a body-authored variable constraint"
				);
			}
		}
	}
	requireCompilerInvariant(materializedConstraint.has_value(), "dependent variable constraint has no active parameter path");
	return std::move(*materializedConstraint);
}

static TypeConstraint materializeDependentLocalConstraint(
	VariableReference *reference, InferenceContext &context, const BindingFrameStack &flexBindingFrameStack
) {
	requireCompilerInvariant(reference, "dependent local variable constraint has no source reference");
	std::optional<TypeConstraint> materializedConstraint;
	for (const DependentVariableTypeConstraint &compiled : reference->dependentTypeConstraints) {
		std::optional<ActiveConstraintPath> path;
		if (compiled.definition->section->isFlex) {
			path = activeFlexConstraintPath(compiled.definition, compiled.pathIndex, flexBindingFrameStack, context);
		} else {
			requireCompilerInvariant(
				context.currentInstantiation,
				"dependent local variable constraint reached a non-flex store without an active instantiation"
			);
			path = activeConstraintPath(compiled.definition, compiled.pathIndex, *context.currentInstantiation);
		}
		if (!path)
			continue;
		std::optional<TypeConstraint> candidate = compiled.constraint.materialize(path->argumentTypes, path->argumentValues);
		requireCompilerInvariant(candidate.has_value(), "active instantiation could not materialize a local constraint");
		if (!materializedConstraint) {
			materializedConstraint = std::move(*candidate);
		} else {
			requireCompilerInvariant(
				materializedConstraint->equivalentTo(*candidate),
				"active instantiation paths disagree on a local variable constraint"
			);
		}
	}
	requireCompilerInvariant(
		materializedConstraint.has_value(), "dependent local variable constraint has no active signature path"
	);
	return std::move(*materializedConstraint);
}

static bool validateStoreConstraint(
	const std::string &variableName, const std::string &sourceConstraintName, const Range &sourceConstraintRange,
	const TypeConstraint &constraint, const DataType &conversionType, Expression *valueExpr, DataType &valueType,
	InferenceContext &context, const BindingFrameStack &valueBindingFrameStack
) {
	CompileTimeValue assignedValue = context.lookupExpressionValue(valueExpr);
	bool accepted = constraint.accepts(valueType, isCompileTimeKnown(assignedValue));
	if (!accepted && conversionType.isDeduced() &&
		tryApplyUserConversion(valueExpr, conversionType, true, context, valueBindingFrameStack)) {
		valueType = effectiveInferredExpressionType(valueExpr);
		assignedValue = context.lookupExpressionValue(valueExpr);
		accepted = constraint.accepts(valueType, isCompileTimeKnown(assignedValue));
	}
	if (accepted)
		return true;
	if (!context.typesValid)
		return false;
	Range diagnosticRange = valueExpr->range;
	const SyntaxConfig &syntax = syntaxConfigForRange(context.parseContext, diagnosticRange);
	std::string detail = renderConfiguredMessage(
		syntax, "variable type constraint mismatch", "message",
		{{"name", variableName}, {"constraint", sourceConstraintName}, {"actual_type", typeToUserName(valueType)}}
	);
	context.setTypeFailure(detail);
	if (!context.trial) {
		Diagnostic diagnostic = buildFailureDetailDiagnostic(diagnosticRange, detail);
		diagnostic.relatedInfo.push_back(
			{renderConfiguredMessage(
				 syntax, "variable type constraint mismatch", "related declaration",
				 {{"name", variableName}, {"constraint", sourceConstraintName}}
			 ),
			 sourceConstraintRange}
		);
		context.addDiagnosticWithCurrentTrace(std::move(diagnostic));
	}
	return false;
}

static bool validateBoundParameterStoreConstraints(
	Expression *destination, BindingFrameStack bindingFrameStack, Expression *valueExpr, DataType &valueType,
	InferenceContext &context, const BindingFrameStack &valueBindingFrameStack
) {
	std::unordered_set<VariableReference *> validatedParameters;
	while (destination) {
		if (destination->kind == Expression::Kind::Variable && destination->variable) {
			VariableReference *parameterDefinition = normalizeBindingReference(destination->variable);
			const BindingFrame::ParameterConstraint *constraint =
				bindingFrameStack.lookupParameterConstraint(parameterDefinition);
			if (constraint && validatedParameters.insert(parameterDefinition).second) {
				Section *section = destination->range.line ? destination->range.line->section : nullptr;
				Variable *variable = section ? section->findVariable(destination->variable) : nullptr;
				DataType conversionType = variable && variable->declaredType.isDeduced()
											  ? variable->declaredType
											  : (variable ? variable->type : destination->type);
				if (!validateStoreConstraint(
						constraint->parameterName, constraint->sourceConstraintName, constraint->sourceRange,
						constraint->constraint, conversionType, valueExpr, valueType, context, valueBindingFrameStack
					)) {
					return false;
				}
			}
		}
		ResolvedBindingLayers bound = resolveExpressionBindingWithCallerScope(destination, bindingFrameStack);
		if (bound.expression == destination)
			break;
		destination = bound.expression;
		bindingFrameStack = std::move(bound.bindingFrameStack);
	}
	return true;
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
	if (!validateBoundParameterStoreConstraints(
			expr->arguments[1], flexBindingFrameStack, valueExpr, valueType, context, valueBindingFrameStack
		)) {
		return;
	}

	if (destinationExpr->kind == Expression::Kind::Variable && destinationExpr->variable) {
		Section *section = destinationExpr->range.line ? destinationExpr->range.line->section : nullptr;
		Variable *variable = section ? section->findVariable(destinationExpr->variable) : nullptr;
		if (!variable) {
			setInvalidStoreDestinationFailure(destinationSourceExpr, context);
			return;
		}
		if (variable->hasDeclaredTypeConstraint()) {
			VariableReference *constraintReference = variable->firstDeclaredTypeConstraintReference();
			requireCompilerInvariant(constraintReference, "typed variable has no constraint source reference");
			TypeConstraint assignmentConstraint;
			DataType assignmentType;
			if (variable->declaredTypeConstraint.isResolved()) {
				assignmentConstraint = variable->declaredTypeConstraint;
				assignmentType = variable->declaredType;
			} else if (!constraintReference->hasDependentTypeConstraint) {
				if (!constraintReference->declaredTypeConstraint.isResolved()) {
					requireCompilerInvariant(
						context.unresolvedPatternConstraintSignal != nullptr,
						"unresolved fixed variable constraint reached committed inference"
					);
					*context.unresolvedPatternConstraintSignal = true;
					return;
				}
				assignmentConstraint = constraintReference->declaredTypeConstraint;
				assignmentType = constraintReference->declaredType;
			} else if (!constraintReference->dependentTypeConstraints.empty()) {
				assignmentConstraint = materializeDependentLocalConstraint(constraintReference, context, flexBindingFrameStack);
			} else {
				assignmentConstraint = materializeDependentParameterConstraint(section, variable, context);
			}
			if (!assignmentType.isDeduced())
				assignmentType = variable->type;
			if (!validateStoreConstraint(
					variable->name, constraintReference->declaredTypeConstraintName,
					constraintReference->declaredTypeConstraintRange, assignmentConstraint, assignmentType, valueExpr,
					valueType, context, valueBindingFrameStack
				)) {
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
			Variable *ownerVariable = ownerSection ? ownerSection->findVariable(ownerExpr->variable) : nullptr;
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
