#pragma once

static std::optional<std::string> returnTypeReferenceSource(const DataType &type) {
	if (!type.isDeduced() || type.kind == DataType::Kind::Void || type.isMetaType())
		return std::nullopt;

	DataType baseType = type;
	int pointerDepth = baseType.pointerDepth;
	baseType.pointerDepth = 0;
	std::optional<std::string> source;
	switch (baseType.kind) {
	case DataType::Kind::Int:
		if (baseType.numericSize > 0)
			source = "@intrinsic(\"type\", \"int\", " + std::to_string(baseType.numericSize * 8) + ")";
		break;
	case DataType::Kind::Float:
		if (baseType.numericSize > 0)
			source = "@intrinsic(\"type\", \"float\", " + std::to_string(baseType.numericSize * 8) + ")";
		break;
	case DataType::Kind::Bool:
		source = "@intrinsic(\"type\", \"bool\")";
		break;
	case DataType::Kind::Array:
		if (baseType.arraySize >= 0 && baseType.arrayElementType) {
			std::optional<std::string> elementSource = returnTypeReferenceSource(*baseType.arrayElementType);
			if (elementSource) {
				source = "@intrinsic(\"array\", " + std::to_string(baseType.arraySize) + ", " + *elementSource + ")";
			}
		}
		break;
	case DataType::Kind::Vector:
		if (baseType.vectorSize() > 0 && baseType.arrayElementType) {
			std::optional<std::string> elementSource = returnTypeReferenceSource(*baseType.arrayElementType);
			if (elementSource) {
				source = "@intrinsic(\"vector\", " + std::to_string(baseType.vectorSize()) + ", " + *elementSource + ")";
			}
		}
		break;
	case DataType::Kind::Matrix:
		if (baseType.matrixRows() > 0 && baseType.matrixColumns() > 0 && baseType.arrayElementType) {
			std::optional<std::string> elementSource = returnTypeReferenceSource(*baseType.arrayElementType);
			if (elementSource) {
				source = "@intrinsic(\"matrix\", " + std::to_string(baseType.matrixRows()) + ", " +
						 std::to_string(baseType.matrixColumns()) + ", " + *elementSource + ")";
			}
		}
		break;
	case DataType::Kind::Any:
	case DataType::Kind::Unresolved:
	case DataType::Kind::Void:
	case DataType::Kind::Class:
	case DataType::Kind::Type:
	case DataType::Kind::Constraint:
		break;
	}
	if (!source)
		return std::nullopt;
	for (int depth = 0; depth < pointerDepth; depth++)
		*source = "@intrinsic(\"add pointer depth\", " + *source + ")";
	return source;
}

static bool returnValueRangeSupportsQuickFix(const Range &range) {
	if (!range.line || range.subString.empty())
		return false;
	SourceLocation start = range.sourceStart();
	SourceLocation end = range.sourceEnd();
	return start.sourceFile && start.sourceFile == end.sourceFile && start.sourceFileLineIndex == end.sourceFileLineIndex;
}

static Diagnostic buildIncompatibleReturnTypeDiagnostic(
	InferenceContext &context, Expression *returnExpression, Expression *returnValueExpression, const DataType &expectedType,
	const DataType &actualType
) {
	Range diagnosticRange = returnValueExpression ? returnValueExpression->range : returnExpression->range;
	const SyntaxConfig &syntax = syntaxConfigForRange(context.parseContext, diagnosticRange);
	std::string expectedTypeName = typeToUserName(expectedType, context.parseContext);
	std::string actualTypeName = typeToUserName(actualType, context.parseContext);
	Diagnostic diagnostic(
		context.parseContext, Diagnostic::Level::Error, "incompatible return type", diagnosticRange, "expected_type",
		expectedTypeName, "actual_type", actualTypeName
	);
	Range originRange = context.currentInstantiation->returnTypeOriginRange;
	if (originRange.line) {
		diagnostic.relatedInfo.push_back(
			{renderConfiguredMessage(syntax, "incompatible return type", "related origin", {{"type", expectedTypeName}}),
			 originRange}
		);
	}

	if (returnValueExpression && isSupportedCastConversion(actualType, expectedType) &&
		returnValueRangeSupportsQuickFix(returnValueExpression->range)) {
		std::optional<std::string> targetTypeSource = returnTypeReferenceSource(expectedType);
		if (targetTypeSource) {
			std::string replacement =
				"@intrinsic(\"cast\", " + std::string(returnValueExpression->range.subString) + ", " + *targetTypeSource + ")";
			diagnostic.quickFixes.push_back(
				{renderConfiguredMessage(syntax, "incompatible return type", "quick fix convert", {{"type", expectedTypeName}}),
				 returnValueExpression->range, std::move(replacement)}
			);
		}
	}
	return diagnostic;
}

static bool reconcileFunctionReturnType(
	InferenceContext &context, Expression *returnExpression, Expression *returnValueExpression, const DataType &returnType,
	const BindingFrameStack &bindingFrameStack
) {
	if (!context.currentInstantiation)
		return true;
	Instantiation &instantiation = *context.currentInstantiation;
	Range returnRange = returnValueExpression ? returnValueExpression->range : returnExpression->range;
	if (!instantiation.returnType.isDeduced()) {
		if (context.trial) {
			requireCompilerInvariant(context.trialJournal, "trial return-type mutation requires a rollback journal");
			context.trialJournal->recordInstantiationWrite(&instantiation);
		}
		instantiation.returnType = returnType;
		instantiation.returnTypeOriginRange = returnRange;
		return true;
	}
	if (instantiation.returnType == returnType)
		return true;
	DataType refinedReturnType;
	if (refineUnspecifiedClassInstantiation(instantiation.returnType, returnType, refinedReturnType)) {
		if (context.trial) {
			requireCompilerInvariant(context.trialJournal, "trial return-type refinement requires a rollback journal");
			context.trialJournal->recordInstantiationWrite(&instantiation);
		}
		instantiation.returnType = refinedReturnType;
		return true;
	}
	if (returnValueExpression &&
		tryApplyUserConversion(returnValueExpression, instantiation.returnType, true, context, bindingFrameStack))
		return true;
	if (!context.typesValid)
		return false;
	context.fail(buildIncompatibleReturnTypeDiagnostic(
		context, returnExpression, returnValueExpression, instantiation.returnType, returnType
	));
	return false;
}
