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
