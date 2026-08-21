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
	if (!context.currentInstantiation || !variable)
		return;
	auto parameter = context.currentInstantiation->parameterTypesByName.find(variable->name);
	if (parameter == context.currentInstantiation->parameterTypesByName.end())
		return;
	if (context.trial) {
		requireCompilerInvariant(context.trialJournal, "trial parameter refinement requires a rollback journal");
		context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
	}
	if (variable->type == parameter->second)
		context.currentInstantiation->parameterOutputTypesByName.erase(variable->name);
	else
		context.currentInstantiation->parameterOutputTypesByName[variable->name] = variable->type;
}

static bool refineStorageExpressionType(
	Expression *expression, const DataType &candidateType, InferenceContext &context, const BindingFrameStack &bindingFrameStack
) {
	ResolvedBindingLayers resolved = resolveInferenceBindingLayers(expression, bindingFrameStack, &context);
	Expression *storage = resolved.expression;
	BindingFrameStack storageBindingFrameStack = std::move(resolved.bindingFrameStack);
	if (!storage)
		crashCompilerBug("storage type refinement lost its expression while resolving bindings");

	if (storage->kind == Expression::Kind::Variable && storage->variable) {
		Section *section = storage->range.line ? storage->range.line->section : nullptr;
		Variable *variable = section ? section->findVariable(storage->variable->name) : nullptr;
		if (!variable || !variable->type.isDeduced())
			return false;
		DataType refinedType;
		if (!mergeVariableAssignmentType(variable->type, candidateType, refinedType) || refinedType == variable->type)
			return false;
		if (context.trial && context.trialJournal)
			context.trialJournal->recordVariableWrite(variable);
		variable->type = refinedType;
		storage->type = refinedType;
		expression->type = refinedType;
		recordParameterOutputType(context, variable);
		return true;
	}

	if (storage->kind != Expression::Kind::IntrinsicCall || intrinsicKind(storage->intrinsicName) != IntrinsicKind::Property)
		return false;

	Expression *ownerExpression = storage->arguments[1];
	DataType ownerType = ensureExpressionTypeWithCurrentGrouping(ownerExpression, context, storageBindingFrameStack);
	if (ownerType.kind != DataType::Kind::Class || !ownerType.classDefinition || ownerType.classInstIndex < 0)
		return false;

	CompileTimeValue propertyValue = resolveStoredCompileTimeValue(storage->arguments[2], storageBindingFrameStack, &context);
	std::string fieldName;
	if (const auto *propertyName = std::get_if<std::string>(&propertyValue))
		fieldName = *propertyName;
	if (fieldName.empty()) {
		Expression *fieldExpression =
			resolveInferenceBindingLayers(storage->arguments[2], storageBindingFrameStack, &context).expression;
		fieldName = extractFieldName(fieldExpression);
	}
	if (fieldName.empty())
		return false;

	ClassDefinition *classDefinition = ownerType.classDefinition;
	for (size_t fieldIndex = 0; fieldIndex < classDefinition->fields.size(); fieldIndex++) {
		if (classDefinition->fields[fieldIndex].name != fieldName)
			continue;
		const DataType &currentFieldType = classDefinition->instantiations[ownerType.classInstIndex].fieldTypes[fieldIndex];
		DataType refinedFieldType;
		if (!mergeVariableAssignmentType(currentFieldType, candidateType, refinedFieldType) ||
			refinedFieldType == currentFieldType)
			return false;
		int refinedInstantiationIndex =
			getRefinedClassInstantiationIndex(context, classDefinition, ownerType.classInstIndex, fieldIndex, refinedFieldType);
		if (refinedInstantiationIndex < 0)
			return false;
		DataType refinedOwnerType = ownerType;
		refinedOwnerType.classInstIndex = refinedInstantiationIndex;
		if (!refineStorageExpressionType(ownerExpression, refinedOwnerType, context, storageBindingFrameStack))
			return false;
		storage->type = refinedFieldType;
		expression->type = refinedFieldType;
		return true;
	}
	return false;
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
	if (!mergeVariableAssignmentType(elementType, valueType, refinedElementType) || refinedElementType == elementType)
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
	if (pointerExpression->kind == Expression::Kind::Variable && pointerExpression->variable && context.currentInstantiation &&
		context.currentInstantiation->parameterTypesByName.contains(pointerExpression->variable->name))
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
