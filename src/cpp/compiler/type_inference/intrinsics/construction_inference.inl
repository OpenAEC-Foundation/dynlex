void ExpressionInferenceSupport::inferArrayIntrinsic() {
	if (expr->arguments.size() == 1) {
		TypeConstraint arrayConstraint = TypeConstraint::any();
		arrayConstraint.kind = DataType::Kind::Array;
		arrayConstraint.pointerDepth = 0;
		expr->type = {DataType::Kind::Constraint};
		context.setExpressionValue(expr, arrayConstraint);
		return;
	}
	std::optional<int> arraySize;
	size_t elementArgumentIndex = 0;
	DataType firstArgumentType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
	if (firstArgumentType.isInteger()) {
		int resolvedSize = 0;
		if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, resolvedSize, &context) ||
			resolvedSize < 0) {
			failIntrinsicArgumentRequirement(1, "a non-negative compile-time integer");
			return;
		}
		arraySize = resolvedSize;
		if (expr->arguments.size() > 2)
			elementArgumentIndex = 2;
	} else {
		if (expr->arguments.size() != 2) {
			failIntrinsicArgumentRequirement(1, "an array length before its element type");
			return;
		}
		elementArgumentIndex = 1;
	}

	std::optional<TypeConstraint> elementConstraint;
	std::optional<DataType> exactElementType;
	if (elementArgumentIndex != 0) {
		DataType elementReferenceType =
			ensureExpressionType(expr->arguments[elementArgumentIndex], context, flexBindingFrameStack);
		CompileTimeValue elementValue =
			resolveStoredCompileTimeValue(expr->arguments[elementArgumentIndex], flexBindingFrameStack, &context);
		elementConstraint = getCompileTimeConstraintValue(elementValue);
		if (!elementConstraint) {
			failCompileTimeOnlyIntrinsicArgument(
				static_cast<int>(elementArgumentIndex), "a compile-time element type or constraint"
			);
			return;
		}
		if (elementReferenceType.kind == DataType::Kind::Type)
			exactElementType = elementReferenceType.toReferencedType();
	}

	TypeConstraint arrayConstraint = TypeConstraint::any();
	arrayConstraint.kind = DataType::Kind::Array;
	arrayConstraint.pointerDepth = 0;
	arrayConstraint.arraySize = arraySize;
	if (elementConstraint)
		arrayConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
	if (!arraySize || (elementConstraint && !exactElementType)) {
		expr->type = {DataType::Kind::Constraint};
		context.setExpressionValue(expr, arrayConstraint);
		return;
	}
	expr->type.kind = DataType::Kind::Type;
	expr->type.referencedKind = DataType::Kind::Array;
	expr->type.arraySize = *arraySize;
	if (exactElementType)
		expr->type.arrayElementType = std::make_shared<DataType>(*exactElementType);
	context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(arrayConstraint)});
}

void ExpressionInferenceSupport::inferVectorIntrinsic() {
	int vectorSize = 0;
	if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, vectorSize, &context) || vectorSize < 1) {
		failIntrinsicArgumentRequirement(1, "an integer greater than 0");
		return;
	}
	expr->type.kind = DataType::Kind::Type;
	expr->type.referencedKind = DataType::Kind::Vector;
	expr->type.arraySize = vectorSize;
	expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
	if (expr->arguments.size() > 2) {
		DataType elemTypeRef = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
		if (elemTypeRef.kind == DataType::Kind::Constraint) {
			std::optional<TypeConstraint> elementConstraint =
				getCompileTimeConstraintValue(resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
				);
			if (!elementConstraint) {
				failCompileTimeOnlyIntrinsicArgument(2, "a compile-time vector element constraint");
				return;
			}
			TypeConstraint vectorConstraint = TypeConstraint::any();
			vectorConstraint.kind = DataType::Kind::Vector;
			vectorConstraint.pointerDepth = 0;
			vectorConstraint.arraySize = vectorSize;
			vectorConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
			expr->type = {DataType::Kind::Constraint};
			context.setExpressionValue(expr, vectorConstraint);
			return;
		}
		if (elemTypeRef.kind != DataType::Kind::Type) {
			failCompileTimeOnlyIntrinsicArgument(2, "a compile-time vector element type reference");
			return;
		}
		expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
	}
	if (!context.typesValid)
		return;
	TypeConstraint vectorConstraint = TypeConstraint::fromTypeReference(expr->type);
	if (expr->arguments.size() > 2) {
		std::optional<TypeReferenceValue> elementValue =
			getCompileTimeTypeReferenceValue(resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
			);
		if (!elementValue)
			crashCompilerBug("inferred vector element type is missing its type-reference value");
		vectorConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
	}
	context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(vectorConstraint)});
}

void ExpressionInferenceSupport::inferMatrixIntrinsic() {
	int rows = 0;
	int columns = 0;
	if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, rows, &context) || rows < 1) {
		failIntrinsicArgumentRequirement(1, "an integer greater than 0");
		return;
	}
	if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, columns, &context) || columns < 1) {
		failIntrinsicArgumentRequirement(2, "an integer greater than 0");
		return;
	}
	expr->type.kind = DataType::Kind::Type;
	expr->type.referencedKind = DataType::Kind::Matrix;
	expr->type.matrixRowCount = rows;
	expr->type.arraySize = columns;
	expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
	if (expr->arguments.size() > 3) {
		DataType elemTypeRef = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
		if (elemTypeRef.kind == DataType::Kind::Constraint) {
			std::optional<TypeConstraint> elementConstraint =
				getCompileTimeConstraintValue(resolveStoredCompileTimeValue(expr->arguments[3], flexBindingFrameStack, &context)
				);
			if (!elementConstraint) {
				failCompileTimeOnlyIntrinsicArgument(3, "a compile-time matrix element constraint");
				return;
			}
			TypeConstraint matrixConstraint = TypeConstraint::any();
			matrixConstraint.kind = DataType::Kind::Matrix;
			matrixConstraint.pointerDepth = 0;
			matrixConstraint.matrixRows = rows;
			matrixConstraint.matrixColumns = columns;
			matrixConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
			expr->type = {DataType::Kind::Constraint};
			context.setExpressionValue(expr, matrixConstraint);
			return;
		}
		if (elemTypeRef.kind != DataType::Kind::Type) {
			failCompileTimeOnlyIntrinsicArgument(3, "a compile-time matrix element type reference");
			return;
		}
		expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
	}
	if (!context.typesValid)
		return;
	TypeConstraint matrixConstraint = TypeConstraint::fromTypeReference(expr->type);
	if (expr->arguments.size() > 3) {
		std::optional<TypeReferenceValue> elementValue =
			getCompileTimeTypeReferenceValue(resolveStoredCompileTimeValue(expr->arguments[3], flexBindingFrameStack, &context)
			);
		if (!elementValue)
			crashCompilerBug("inferred matrix element type is missing its type-reference value");
		matrixConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
	}
	context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(matrixConstraint)});
}

void ExpressionInferenceSupport::inferConstructIntrinsic() {
	std::vector<DataType> constructionArgumentTypes;
	constructionArgumentTypes.reserve(expr->arguments.size() - 2);
	bool allConstructionArgumentsDeduced = true;
	for (size_t i = 2; i < expr->arguments.size(); i++) {
		DataType argumentType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
		if (!argumentType.isDeduced()) {
			allConstructionArgumentsDeduced = false;
			break;
		}
		constructionArgumentTypes.push_back(argumentType);
	}
	DataType typeRefType;
	const std::vector<DataType> *constructionTypesForResolution =
		allConstructionArgumentsDeduced && !constructionArgumentTypes.empty() ? &constructionArgumentTypes : nullptr;
	if (!resolveCompileTimeTypeReference(
			context.parseContext, expr->arguments[1], flexBindingFrameStack, typeRefType, &context,
			constructionTypesForResolution
		) ||
		typeRefType.kind != DataType::Kind::Type) {
		setConfiguredTypeFailure(expr->range, "construct requires compile-time type reference");
		return;
	}
	if (expr->arguments.size() == 2) {
		DataType targetType = typeRefType.toReferencedType();
		if (!targetType.isConcrete() || !targetType.isRuntimeValueType()) {
			setConfiguredTypeFailure(
				expr->range, "type cannot be constructed", "message", {{"type", typeToUserName(targetType)}}
			);
			return;
		}
		expr->type = targetType;
		return;
	}
	if (typeRefType.referencedKind == DataType::Kind::Array) {
		DataType arrayType = typeRefType.toReferencedType();
		if (arrayType.arraySize == static_cast<int>(expr->arguments.size()) - 2) {
			DataType elementType =
				arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
			bool allDeduced = true;
			for (size_t i = 2; i < expr->arguments.size(); i++) {
				DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
				if (!argType.isDeduced())
					allDeduced = false;
				if (!arrayType.arrayElementType) {
					if (!elementType.isDeduced())
						elementType = argType;
					else if (elementType != argType)
						allDeduced = false;
				}
			}
			if (allDeduced && elementType.isDeduced()) {
				arrayType.arrayElementType = std::make_shared<DataType>(elementType);
				expr->type = arrayType;
			}
		}
	} else if (typeRefType.referencedKind == DataType::Kind::Vector) {
		DataType vectorType = typeRefType.toReferencedType();
		if (vectorType.arraySize == static_cast<int>(expr->arguments.size()) - 2) {
			bool allCompatible = true;
			for (size_t i = 2; i < expr->arguments.size(); i++) {
				DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
				if (!argType.isDeduced()) {
					allCompatible = false;
					break;
				}
				DataType promoted;
				if (!DataType::promoteArithmetic(argType, *vectorType.arrayElementType, promoted) ||
					promoted != *vectorType.arrayElementType) {
					allCompatible = false;
					break;
				}
			}
			if (allCompatible)
				expr->type = vectorType;
		}
	} else if (typeRefType.referencedKind == DataType::Kind::Matrix) {
		DataType matrixType = typeRefType.toReferencedType();
		if (expr->arguments.size() == 3) {
			DataType valueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
			if (valueType.kind == DataType::Kind::Array && valueType.arrayElementType &&
				valueType.arraySize == matrixType.matrixRows() * matrixType.matrixColumns()) {
				DataType promoted;
				if (DataType::promoteArithmetic(*valueType.arrayElementType, matrixType.matrixElementType(), promoted) &&
					promoted == matrixType.matrixElementType()) {
					expr->type = matrixType;
				}
			}
		}

	} else if (typeRefType.classDefinition) {
		DataType instantiatedTypeRef;
		if (allConstructionArgumentsDeduced &&
			instantiateClassFromArgumentTypes(
				typeRefType.classDefinition, constructionArgumentTypes, instantiatedTypeRef, typeRefType.classInstIndex
			)) {
			expr->type = instantiatedTypeRef.toReferencedType();
		} else {
			DataType targetType = typeRefType.toReferencedType();
			if (expr->arguments.size() == targetType.classDefinition->fields.size() + 2 && targetType.classInstIndex >= 0) {
				const auto &fieldTypes = targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
				bool allCompatible = constructionArgumentTypes.size() == fieldTypes.size();
				for (size_t i = 0; allCompatible && i < fieldTypes.size(); i++) {
					if (!DataType::supportsRuntimeConversion(constructionArgumentTypes[i], fieldTypes[i]))
						allCompatible = false;
				}
				if (allCompatible)
					expr->type = targetType;
			}
		}
	} else if (expr->arguments.size() == 3) {
		DataType targetType = typeRefType.toReferencedType();
		DataType valueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
		if (valueType.isDeduced())
			expr->type = targetType;
	}
}

void ExpressionInferenceSupport::inferPropertyIntrinsic() {
	DataType instType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack).propertyOwnerType();
	if (!instType.isDeduced()) {
		context.typesValid = false;
		return;
	}
	std::string fieldName;
	CompileTimeValue propertyValue = resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context);
	if (const auto *propertyName = std::get_if<std::string>(&propertyValue)) {
		fieldName = *propertyName;
	} else {
		Expression *propExpr = resolveThroughFlexBindings(expr->arguments[2]);
		fieldName = extractFieldName(propExpr);
	}
	if (fieldName.empty()) {
		failCompileTimeOnlyIntrinsicArgument(2, "a compile-time property name");
		return;
	}
	DataType builtInPropertyType = resolveBuiltInPropertyType(instType, fieldName);
	if (builtInPropertyType.isDeduced()) {
		expr->type = builtInPropertyType;
		return;
	}
	if (instType.kind == DataType::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
		if (!fieldName.empty()) {
			ClassDefinition *classDef = instType.classDefinition;
			for (size_t i = 0; i < classDef->fields.size(); i++) {
				if (classDef->fields[i].name == fieldName) {
					expr->type = classDef->instantiations[instType.classInstIndex].fieldTypes[i];
					break;
				}
			}
		}
	}
	if (!expr->type.isDeduced() && !fieldName.empty()) {
		setConfiguredTypeFailure(
			expr->range, "class missing property", "message", {{"type", typeToUserName(instType)}, {"property", fieldName}}
		);
	}
}
