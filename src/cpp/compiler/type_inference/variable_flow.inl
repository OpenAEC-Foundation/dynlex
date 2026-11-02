#pragma once

#include "type_resolution.inl"

static Diagnostic buildAssignmentTypeChangeDiagnostic(
	const std::string &name, const DataType &currentType, Range currentTypeOriginRange,
	const std::string &currentTypeOriginFloatLiteralReplacement, Expression *valueExpr, const DataType &valueType,
	ParseContext &parseContext
) {
	Range diagnosticRange = valueExpr ? valueExpr->range : currentTypeOriginRange;
	Diagnostic diagnostic(
		parseContext, Diagnostic::Level::Error, "variable type change", diagnosticRange, "name", name, "from_type",
		typeToUserName(currentType), "to_type", typeToUserName(valueType)
	);
	const SyntaxConfig &syntax = syntaxConfigForRange(parseContext, diagnosticRange);
	if (currentTypeOriginRange.line) {
		diagnostic.relatedInfo.push_back(
			{renderConfiguredMessage(
				 syntax, "variable type change", "related origin", {{"name", name}, {"type", typeToUserName(currentType)}}
			 ),
			 currentTypeOriginRange}
		);
	}
	if (!currentTypeOriginFloatLiteralReplacement.empty() && valueType.kind == DataType::Kind::Float &&
		currentType.kind == DataType::Kind::Int && currentTypeOriginRange.line) {
		diagnostic.quickFixes.push_back(
			{renderConfiguredMessage(
				 syntax, "variable type change", "quick fix float literal",
				 {{"original", (std::string)currentTypeOriginRange.subString},
				  {"replacement", currentTypeOriginFloatLiteralReplacement}}
			 ),
			 currentTypeOriginRange, currentTypeOriginFloatLiteralReplacement}
		);
	}
	return diagnostic;
}

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Expression *valueExpr, const DataType &valueType, ParseContext &parseContext) {
	requireCompilerInvariant(var != nullptr, "variable type-change diagnostic requires a variable");
	Range originRange = var->typeOriginRange.line ? var->typeOriginRange : (var->definition ? var->definition->range : Range());
	return buildAssignmentTypeChangeDiagnostic(
		var->name, var->type, originRange, var->typeOriginFloatLiteralReplacement, valueExpr, valueType, parseContext
	);
}

static int getRefinedClassInstantiationIndex(
	InferenceContext &context, ClassDefinition *classDef, int instIndex, size_t fieldIndex, const DataType &fieldType
) {
	if (!classDef || instIndex < 0 || instIndex >= (int)classDef->instantiations.size())
		return -1;
	const auto &baseFieldTypes = classDef->instantiations[instIndex].fieldTypes;
	if (fieldIndex >= baseFieldTypes.size())
		return -1;
	DataType refinedFieldType = fieldType;
	if (baseFieldTypes[fieldIndex].isDeduced()) {
		if (!mergeVariableAssignmentType(baseFieldTypes[fieldIndex], fieldType, refinedFieldType))
			return -1;
		if (refinedFieldType == baseFieldTypes[fieldIndex])
			return instIndex;
	}
	const DataType &declaredFieldType = classDef->fields[fieldIndex].declaredType;
	if (declaredFieldType.isDeduced() && !isVariableAssignmentCompatible(declaredFieldType, fieldType))
		return -1;
	std::vector<DataType> refinedFieldTypes = baseFieldTypes;
	refinedFieldTypes[fieldIndex] = refinedFieldType;
	bool instantiationExists = std::any_of(
		classDef->instantiations.begin(), classDef->instantiations.end(),
		[&](const ClassInstantiation &instantiation) {
		return instantiation.fieldTypes == refinedFieldTypes;
	}
	);
	if (!instantiationExists && context.trial && context.trialJournal)
		context.trialJournal->recordClassInstantiationAppend(classDef);
	int existingIndex = classDef->getOrCreateInstantiation(refinedFieldTypes);
	return existingIndex;
}

static void recordParameterOutputType(InferenceContext &context, Variable *variable) {
	if (!variableIsCurrentInstantiationParameter(context, variable))
		return;
	auto parameter = context.currentInstantiation->parameterTypesByName.find(variable->name);
	requireCompilerInvariant(
		parameter != context.currentInstantiation->parameterTypesByName.end(),
		"current instantiation parameter is missing its input type"
	);
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial parameter refinement requires a rollback journal");
		context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
	}
	if (variable->type == parameter->second)
		context.currentInstantiation->parameterOutputTypesByName.erase(variable->name);
	else
		context.currentInstantiation->parameterOutputTypesByName[variable->name] = variable->type;
}

#include "storage_type_refinement.inl"

static bool refineStorageExpressionType(
	Expression *expression, const DataType &candidateType, InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	ResolvedBindingLayers resolved = resolveInferenceBindingLayers(expression, bindingFrameStack, &context);
	Expression *storage = resolved.expression;
	requireCompilerInvariant(storage != nullptr, "storage type refinement lost its expression");
	auto view = storageTypeView(storage, context, resolved.bindingFrameStack);
	if (!view)
		return false;
	const auto &root = context.typeRefinements.read().roots.at(view->root);
	Variable *variable = root.variable;
	DataType refined = root.type;
	if (!refineProjectedStorageType(refined, view->projections, 0, candidateType, context) ||
		!refineStorageRootType(view->root, variable, refined, context))
		return false;
	storage->type = projectStorageType(context.typeRefinements.read().roots.at(view->root).type, view->projections);
	expression->type = storage->type;
	return true;
}

static bool refinePointerStoragePointeeType(
	Expression *pointerExpression, const DataType &valueType, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack
) {
	ResolvedBindingLayers resolved = resolveInferenceBindingLayers(pointerExpression, bindingFrameStack, &context);
	Expression *pointerStorage = resolved.expression;
	BindingFrameStack pointerBindingFrameStack = std::move(resolved.bindingFrameStack);
	if (!pointerStorage)
		crashCompilerBug("pointer storage refinement lost its expression while resolving bindings");
	if (pointerStorage->inferredPointerStorage)
		return refinePointerStoragePointeeType(
			pointerStorage->inferredPointerStorage, valueType, context, pointerBindingFrameStack
		);

	DataType pointerType = effectiveInferredExpressionType(pointerStorage);
	if (!pointerType.isPointer())
		return false;
	DataType elementType = pointerType.dereferenced();
	DataType refinedElementType;
	if (!mergeVariableAssignmentType(elementType, valueType, refinedElementType))
		return false;
	DataType refinedPointerType = refinedElementType.pointed();

	if (pointerStorage->kind == Expression::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(pointerStorage->intrinsicName);
		if (kind == IntrinsicKind::Add || kind == IntrinsicKind::Subtract) {
			for (size_t argumentIndex = 1; argumentIndex < pointerStorage->arguments.size(); argumentIndex++) {
				Expression *argument = pointerStorage->arguments[argumentIndex];
				if (argument && effectiveInferredExpressionType(argument).isPointer() &&
					refinePointerStoragePointeeType(argument, valueType, context, pointerBindingFrameStack)) {
					pointerStorage->type = refinedPointerType;
					pointerExpression->type = refinedPointerType;
					return true;
				}
			}
			return false;
		}
		if (kind == IntrinsicKind::AddressOf)
			return refineStorageExpressionType(
				pointerStorage->arguments[1], refinedElementType, context, pointerBindingFrameStack
			);
	}
	return refineStorageExpressionType(pointerStorage, refinedPointerType, context, pointerBindingFrameStack);
}

static std::optional<std::string>
pointerStorageParameterName(Expression *expression, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	ResolvedBindingLayers resolved = resolveInferenceBindingLayers(expression, bindingFrameStack, &context);
	Expression *pointerExpression = resolved.expression;
	BindingFrameStack pointerBindingFrameStack = std::move(resolved.bindingFrameStack);
	if (!pointerExpression || !effectiveInferredExpressionType(pointerExpression).isPointer())
		return std::nullopt;
	if (pointerExpression->inferredPointerStorage)
		return pointerStorageParameterName(pointerExpression->inferredPointerStorage, context, pointerBindingFrameStack);
	if (pointerExpression->kind == Expression::Kind::Variable &&
		variableReferenceIsCurrentInstantiationParameter(context, pointerExpression->variable))
		return pointerExpression->variable->name;
	if (pointerExpression->kind != Expression::Kind::IntrinsicCall)
		return std::nullopt;
	IntrinsicKind kind = intrinsicKind(pointerExpression->intrinsicName);
	if (kind != IntrinsicKind::Add && kind != IntrinsicKind::Subtract)
		return std::nullopt;
	for (size_t argumentIndex = 1; argumentIndex < pointerExpression->arguments.size(); argumentIndex++) {
		Expression *argument = pointerExpression->arguments[argumentIndex];
		if (!argument || !effectiveInferredExpressionType(argument).isPointer())
			continue;
		if (std::optional<std::string> parameter = pointerStorageParameterName(argument, context, pointerBindingFrameStack))
			return parameter;
	}
	return std::nullopt;
}
