#pragma once

#include "type_resolution.inl"

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Expression *valueExpr, const DataType &valueType, ParseContext &parseContext) {
	Range diagnosticRange = valueExpr ? valueExpr->range : (var && var->definition ? var->definition->range : Range());
	Diagnostic diagnostic(
		parseContext, Diagnostic::Level::Error, "variable type change", diagnosticRange, "name", var->name, "from_type",
		typeToUserName(var->type, parseContext), "to_type", typeToUserName(valueType, parseContext)
	);
	const SyntaxConfig &syntax = syntaxConfigForRange(parseContext, diagnosticRange);
	if (var->typeOriginRange.line) {
		diagnostic.relatedInfo.push_back(
			{renderConfiguredMessage(
				 syntax, "variable type change", "related origin",
				 {{"name", var->name}, {"type", typeToUserName(var->type, parseContext)}}
			 ),
			 var->typeOriginRange}
		);
	}
	if (!var->typeOriginFloatLiteralReplacement.empty() && valueType.kind == DataType::Kind::Float &&
		var->type.kind == DataType::Kind::Int && var->typeOriginRange.line) {
		diagnostic.quickFixes.push_back(
			{renderConfiguredMessage(
				 syntax, "variable type change", "quick fix float literal",
				 {{"original", (std::string)var->typeOriginRange.subString},
				  {"replacement", var->typeOriginFloatLiteralReplacement}}
			 ),
			 var->typeOriginRange, var->typeOriginFloatLiteralReplacement}
		);
	}
	return diagnostic;
}

static int getRefinedClassInstantiationIndex(
	InferenceContext &context, ClassDefinition *classDef, int instIndex, size_t fieldIndex, const DataType &fieldType
) {
	if (!classDef || instIndex < 0 || instIndex >= (int)classDef->instantiations.size())
		return -1;
	const auto &baseFieldTypes = classDef->instantiations[instIndex].fieldTypes;
	if (fieldIndex >= baseFieldTypes.size())
		return -1;
	const DataType &declaredFieldType = classDef->fields[fieldIndex].declaredType;
	if (declaredFieldType.isDeduced() && declaredFieldType != fieldType)
		return -1;
	std::vector<DataType> refinedFieldTypes = baseFieldTypes;
	refinedFieldTypes[fieldIndex] = fieldType;
	bool instantiationExists = false;
	for (const auto &inst : classDef->instantiations) {
		if (inst.fieldTypes == refinedFieldTypes) {
			instantiationExists = true;
			break;
		}
	}
	if (!instantiationExists && context.trial && context.trialJournal)
		context.trialJournal->recordClassInstantiationAppend(classDef);
	int existingIndex = classDef->getOrCreateInstantiation(refinedFieldTypes);
	return existingIndex;
}
