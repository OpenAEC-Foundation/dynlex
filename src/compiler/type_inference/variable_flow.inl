#pragma once

#include "type_resolution.inl"

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Function *valueExpr, const DataType &valueType, ParseContext &parseContext) {
	Range diagnosticRange = valueExpr ? valueExpr->range : (var && var->definition ? var->definition->range : Range());
	Diagnostic diagnostic(
		Diagnostic::Level::Error,
		"Variable '" + var->name + "' cannot change type from " + typeToUserName(var->type, parseContext) + " to " +
			typeToUserName(valueType, parseContext),
		diagnosticRange
	);
	if (var->typeOriginRange.line) {
		diagnostic.relatedInfo.push_back(
			{"Variable '" + var->name + "' first became " + typeToUserName(var->type, parseContext) + " here",
			 var->typeOriginRange}
		);
	}
	if (!var->typeOriginFloatLiteralReplacement.empty() && valueType.kind == DataType::Kind::Float &&
		var->type.kind == DataType::Kind::Int && var->typeOriginRange.line) {
		diagnostic.quickFixes.push_back(
			{"Change '" + (std::string)var->typeOriginRange.subString + "' to '" + var->typeOriginFloatLiteralReplacement + "'",
			 var->typeOriginRange, var->typeOriginFloatLiteralReplacement}
		);
	}
	return diagnostic;
}

static Variable *findVariableInSectionTree(Section *section, const std::string &name) {
	if (!section)
		return nullptr;
	if (Variable *var = section->findVariable(name))
		return var;
	for (Section *child : section->children) {
		if (Variable *var = findVariableInSectionTree(child, name))
			return var;
	}
	return nullptr;
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
