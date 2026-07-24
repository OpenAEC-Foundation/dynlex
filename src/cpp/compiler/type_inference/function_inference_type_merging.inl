static bool
refineUnspecifiedClassInstantiation(const DataType &currentType, const DataType &incomingType, DataType &refinedType) {
	if (ClassDefinition::typeStructurallyRefines(incomingType, currentType)) {
		refinedType = incomingType;
		return true;
	}
	if (ClassDefinition::typeStructurallyRefines(currentType, incomingType)) {
		refinedType = currentType;
		return true;
	}
	return false;
}

static bool mergeSelectBranchTypes(const DataType &trueTypeInput, const DataType &falseTypeInput, DataType &outType) {
	DataType trueType = trueTypeInput;
	DataType falseType = falseTypeInput;
	if (!trueType.isDeduced() || !falseType.isDeduced())
		return false;
	if (trueType == falseType) {
		outType = trueType;
		return true;
	}
	if (trueType.kind == DataType::Kind::Type && falseType.kind == DataType::Kind::Type) {
		outType = {DataType::Kind::Type};
		outType.referencedKind = DataType::Kind::Type;
		return true;
	}
	if (trueType.isNumeric() && falseType.isNumeric())
		return DataType::promoteArithmetic(trueType, falseType, outType);
	if (trueType.isVector() && falseType.isVector() && trueType.vectorSize() == falseType.vectorSize())
		return DataType::promoteArithmetic(trueType, falseType, outType);
	if (trueType.isMatrix() && falseType.isMatrix() && trueType.matrixRows() == falseType.matrixRows() &&
		trueType.matrixColumns() == falseType.matrixColumns()) {
		return DataType::promoteArithmetic(trueType, falseType, outType);
	}
	if (refineUnspecifiedClassInstantiation(trueType, falseType, outType))
		return true;
	return false;
}

static void commitVariableTypeFromValue(Variable *var, Expression *valueExpr, const DataType &valueType) {
	if (!var)
		return;
	var->type = valueType;
	var->typeOriginRange = valueExpr ? valueExpr->range : Range();
	var->typeOriginFloatLiteralReplacement = makeFloatLiteralReplacement(valueExpr);
}

static bool mergeVariableAssignmentType(const DataType &targetType, const DataType &valueType, DataType &mergedType) {
	if (!targetType.isDeduced() || !valueType.isDeduced())
		return false;
	if (targetType == valueType) {
		mergedType = targetType;
		return true;
	}
	if (refineUnspecifiedClassInstantiation(targetType, valueType, mergedType))
		return true;
	if (targetType.kind == DataType::Kind::Int && valueType.kind == DataType::Kind::Int && targetType.pointerDepth == 0 &&
		valueType.pointerDepth == 0) {
		mergedType = targetType;
		return true;
	}
	return false;
}

static bool isVariableAssignmentCompatible(const DataType &targetType, const DataType &valueType) {
	DataType mergedType;
	return mergeVariableAssignmentType(targetType, valueType, mergedType);
}

static Diagnostic
buildVariableTypeChangeDiagnostic(Variable *var, Expression *valueExpr, const DataType &valueType, ParseContext &parseContext);
static Diagnostic buildAssignmentTypeChangeDiagnostic(
	const std::string &name, const DataType &currentType, Range currentTypeOriginRange,
	const std::string &currentTypeOriginFloatLiteralReplacement, Expression *valueExpr, const DataType &valueType,
	ParseContext &parseContext
);
static int getRefinedClassInstantiationIndex(
	InferenceContext &context, ClassDefinition *classDef, int instIndex, size_t fieldIndex, const DataType &fieldType
);
