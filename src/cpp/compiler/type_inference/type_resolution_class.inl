static DataType toTypeReference(const DataType &valueType) {
	if (!valueType.isDeduced())
		return {};
	if (valueType.kind == DataType::Kind::Type)
		return valueType;
	DataType concreteValueType = valueType;
	DataType typeReference;
	typeReference.kind = DataType::Kind::Type;
	typeReference.referencedKind = concreteValueType.kind;
	typeReference.numericSize = concreteValueType.numericSize;
	typeReference.pointerDepth = concreteValueType.pointerDepth;
	typeReference.classDefinition = concreteValueType.classDefinition;
	typeReference.classInstIndex = concreteValueType.classInstIndex;
	typeReference.arraySize = concreteValueType.arraySize;
	typeReference.matrixRowCount = concreteValueType.matrixRowCount;
	if (concreteValueType.arrayElementType)
		typeReference.arrayElementType = std::make_shared<DataType>(*concreteValueType.arrayElementType);
	return typeReference;
}

static bool resolveCompileTimeTypeReference(
	ParseContext &parseContext, Expression *expr, const BindingFrameStack &bindingFrameStack, DataType &outTypeRef,
	InferenceContext *inferenceContext, const std::vector<DataType> *constructionArgumentTypes
) {
	if (!expr)
		return false;
	CompileTimeValue directValue = lookupCompileTimeExpressionValue(expr, inferenceContext);
	if (auto *compileTimeTypeRef = std::get_if<TypeReferenceValue>(&directValue)) {
		bool requiresClassCompletion = constructionArgumentTypes &&
									   compileTimeTypeRef->type.referencedKind == DataType::Kind::Class &&
									   compileTimeTypeRef->type.classInstIndex < 0;
		if (isConcreteTypeReferenceValue(compileTimeTypeRef->type) && !requiresClassCompletion) {
			outTypeRef = compileTimeTypeRef->type;
			return true;
		}
	}
	bool expressionTypeRequiresClassCompletion =
		constructionArgumentTypes && expr->type.referencedKind == DataType::Kind::Class && expr->type.classInstIndex < 0;
	if (isConcreteTypeReferenceValue(expr->type) && !expressionTypeRequiresClassCompletion) {
		outTypeRef = expr->type;
		return true;
	}
	BindingFrameStack effectiveBindingFrameStack;
	Expression *resolved =
		prepareCompileTimeTypeReferenceExpression(expr, bindingFrameStack, effectiveBindingFrameStack, inferenceContext);
	if (!resolved)
		return false;
	CompileTimeValue resolvedValue = lookupCompileTimeExpressionValue(resolved, inferenceContext);
	if (auto *compileTimeTypeRef = std::get_if<TypeReferenceValue>(&resolvedValue)) {
		bool requiresClassCompletion = constructionArgumentTypes &&
									   compileTimeTypeRef->type.referencedKind == DataType::Kind::Class &&
									   compileTimeTypeRef->type.classInstIndex < 0;
		if (isConcreteTypeReferenceValue(compileTimeTypeRef->type) && !requiresClassCompletion) {
			outTypeRef = compileTimeTypeRef->type;
			return true;
		}
	}
	bool resolvedTypeRequiresClassCompletion = constructionArgumentTypes &&
											   resolved->type.referencedKind == DataType::Kind::Class &&
											   resolved->type.classInstIndex < 0;
	if (isConcreteTypeReferenceValue(resolved->type) && !resolvedTypeRequiresClassCompletion) {
		outTypeRef = resolved->type;
		return true;
	}

	if (resolved->kind == Expression::Kind::Variable && resolved->variable)
		return false;

	if (resolved->kind == Expression::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(resolved->intrinsicName);
		if (kind == IntrinsicKind::Type) {
			DataType resolvedType = resolveKnownExpressionType(resolved, effectiveBindingFrameStack, inferenceContext);
			if (resolvedType.kind == DataType::Kind::Type) {
				outTypeRef = resolvedType;
				return true;
			}
		}
		if (kind == IntrinsicKind::AddPointerDepth) {
			DataType innerTypeRef;
			if (!resolveCompileTimeTypeReference(
					parseContext, resolved->arguments[1], effectiveBindingFrameStack, innerTypeRef, inferenceContext
				) ||
				innerTypeRef.kind != DataType::Kind::Type)
				return false;
			innerTypeRef.pointerDepth++;
			outTypeRef = innerTypeRef;
			return true;
		}
		if (kind == IntrinsicKind::Array) {
			int arraySize = 0;
			if (!resolveStoredCompileTimeInteger(
					resolved->arguments[1], effectiveBindingFrameStack, arraySize, inferenceContext
				))
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Array;
			outTypeRef.arraySize = arraySize;
			if (resolved->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, resolved->arguments[2], effectiveBindingFrameStack, elementTypeRef, inferenceContext
					) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Multiply && resolved->arguments.size() > 2) {
			DataType arrayTypeRef;
			int factor = 0;
			if (resolveCompileTimeTypeReference(
					parseContext, resolved->arguments[1], effectiveBindingFrameStack, arrayTypeRef, inferenceContext
				) &&
				arrayTypeRef.kind == DataType::Kind::Type && arrayTypeRef.referencedKind == DataType::Kind::Array &&
				resolveStoredCompileTimeInteger(resolved->arguments[2], effectiveBindingFrameStack, factor, inferenceContext) &&
				factor >= 0) {
				arrayTypeRef.arraySize *= factor;
				outTypeRef = arrayTypeRef;
				return true;
			}
			if (resolveCompileTimeTypeReference(
					parseContext, resolved->arguments[2], effectiveBindingFrameStack, arrayTypeRef, inferenceContext
				) &&
				arrayTypeRef.kind == DataType::Kind::Type && arrayTypeRef.referencedKind == DataType::Kind::Array &&
				resolveStoredCompileTimeInteger(resolved->arguments[1], effectiveBindingFrameStack, factor, inferenceContext) &&
				factor >= 0) {
				arrayTypeRef.arraySize *= factor;
				outTypeRef = arrayTypeRef;
				return true;
			}
		}
		if (kind == IntrinsicKind::Vector) {
			int vectorSize = 0;
			if (!resolveStoredCompileTimeInteger(
					resolved->arguments[1], effectiveBindingFrameStack, vectorSize, inferenceContext
				) ||
				vectorSize < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Vector;
			outTypeRef.arraySize = vectorSize;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, resolved->arguments[2], effectiveBindingFrameStack, elementTypeRef, inferenceContext
					) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Matrix) {
			int rows = 0;
			int columns = 0;
			if (!resolveStoredCompileTimeInteger(resolved->arguments[1], effectiveBindingFrameStack, rows, inferenceContext) ||
				!resolveStoredCompileTimeInteger(
					resolved->arguments[2], effectiveBindingFrameStack, columns, inferenceContext
				) ||
				rows < 1 || columns < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Matrix;
			outTypeRef.matrixRowCount = rows;
			outTypeRef.arraySize = columns;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 3) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, resolved->arguments[3], effectiveBindingFrameStack, elementTypeRef, inferenceContext
					) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Select) {
			Expression *selectedBranch =
				resolveCompileTimeSelectBranch(resolved, parseContext, effectiveBindingFrameStack, inferenceContext);
			if (selectedBranch)
				return resolveCompileTimeTypeReference(
					parseContext, selectedBranch, effectiveBindingFrameStack, outTypeRef, inferenceContext,
					constructionArgumentTypes
				);
		}
	}

	if (resolved->kind == Expression::Kind::PatternCall) {
		PatternDefinition *def = resolved->selectedPatternDefinition;
		if (!def || !def->section)
			return false;

		if (!def->section->isFlex && def->section->type == SectionType::Class) {
			BindingFrameStack callBindingFrameStack = effectiveBindingFrameStack;
			pushPatternCallBindingScope(callBindingFrameStack, resolved, def);
			outTypeRef = instantiateBoundClassType(
				parseContext, static_cast<ClassSection *>(def->section)->classDefinition, callBindingFrameStack,
				inferenceContext, constructionArgumentTypes
			);
			return outTypeRef.kind == DataType::Kind::Type;
		}
		if (!def->section->isFlex) {
			Instantiation *selectedInstantiation = resolved->selectedInstantiation;
			if (!selectedInstantiation || !selectedInstantiation->valid || !selectedInstantiation->returnType.isDeduced())
				return false;
			DataType resolvedReturnTypeRef = toTypeReference(selectedInstantiation->returnType);
			if (resolvedReturnTypeRef.kind != DataType::Kind::Type)
				return false;
			outTypeRef = resolvedReturnTypeRef;
			return true;
		}
	}
	return false;
}

static bool
completeBoundClassFieldType(const DataType &fieldConstraintInput, const DataType &argumentTypeInput, DataType &outFieldType) {
	DataType fieldConstraint = fieldConstraintInput;
	DataType argumentType = argumentTypeInput;
	if (!argumentType.isConcrete())
		return false;
	if (fieldConstraint.kind == DataType::Kind::Any) {
		outFieldType = argumentType;
		return true;
	}
	if (ClassDefinition::typeStructurallyRefines(argumentType, fieldConstraint)) {
		outFieldType = argumentType;
		return true;
	}
	if (fieldConstraint.kind == DataType::Kind::Array && !fieldConstraint.isConcrete()) {
		if (argumentType.kind != DataType::Kind::Array || argumentType.arraySize != fieldConstraint.arraySize ||
			!argumentType.arrayElementType)
			return false;
		if (fieldConstraint.arrayElementType) {
			DataType completedElementType;
			if (!completeBoundClassFieldType(
					*fieldConstraint.arrayElementType, *argumentType.arrayElementType, completedElementType
				))
				return false;
			outFieldType = fieldConstraint;
			outFieldType.arrayElementType = std::make_shared<DataType>(std::move(completedElementType));
		} else {
			outFieldType = argumentType;
		}
		return outFieldType.isConcrete();
	}
	if (fieldConstraint.kind == DataType::Kind::Class && fieldConstraint.classDefinition &&
		fieldConstraint.classInstIndex < 0) {
		if (argumentType.kind != DataType::Kind::Class || argumentType.classDefinition != fieldConstraint.classDefinition)
			return false;
		outFieldType = argumentType;
		return true;
	}
	if (!fieldConstraint.isConcrete() || !DataType::supportsRuntimeConversion(argumentType, fieldConstraint))
		return false;
	outFieldType = fieldConstraint;
	return true;
}

static DataType instantiateBoundClassType(
	ParseContext &parseContext, ClassDefinition *classDef, const BindingFrameStack &bindingFrameStack,
	InferenceContext *inferenceContext, const std::vector<DataType> *constructionArgumentTypes
) {
	if (!classDef)
		return {};
	if (constructionArgumentTypes && constructionArgumentTypes->size() != classDef->fields.size())
		return {};

	std::string requestKey = buildClassInstantiationRequestKey(bindingFrameStack, inferenceContext, constructionArgumentTypes);
	auto completedRequest = classDef->instantiationIndicesByRequest.find(requestKey);
	if (completedRequest != classDef->instantiationIndicesByRequest.end()) {
		requireCompilerInvariant(
			completedRequest->second >= 0 && completedRequest->second < static_cast<int>(classDef->instantiations.size()),
			"cached class instantiation request refers to a missing instantiation"
		);
		return {DataType::Kind::Type, 0, 0, classDef, completedRequest->second, nullptr, DataType::Kind::Class};
	}
	for (auto active = activeClassInstantiations.rbegin(); active != activeClassInstantiations.rend(); ++active) {
		if (active->parseContext != &parseContext || active->classDefinition != classDef || active->requestKey != requestKey)
			continue;
		return {DataType::Kind::Type, 0, 0, classDef, active->symbolicIndex, nullptr, DataType::Kind::Class};
	}

	int symbolicIndex = nextSymbolicClassInstantiationIndex--;
	ScopedActiveClassInstantiation activeInstantiation(parseContext, classDef, requestKey, symbolicIndex);

	struct ScopedInferenceVariableStateOverride {
		InferenceContext *context{};
		KnownConstantState savedKnownConstants;
		AddressInferenceState savedAddressState;

		explicit ScopedInferenceVariableStateOverride(InferenceContext *inferenceContext)
			: context(inferenceContext), savedKnownConstants(snapshotKnownConstantsForClassInstantiation(inferenceContext)),
			  savedAddressState(snapshotAddressStateForClassInstantiation(inferenceContext)) {}

		~ScopedInferenceVariableStateOverride() {
			restoreKnownConstantsForClassInstantiation(context, std::move(savedKnownConstants));
			restoreAddressStateForClassInstantiation(context, std::move(savedAddressState));
		}
	} scopedInferenceVariableStateOverride(inferenceContext);

	seedKnownConstantsForClassInstantiation(bindingFrameStack, inferenceContext);

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(classDef->fields.size());
	for (size_t fieldIndex = 0; fieldIndex < classDef->fields.size(); fieldIndex++) {
		FieldDefinition &field = classDef->fields[fieldIndex];
		DataType fieldType = field.declaredType;
		if (fieldType.kind == DataType::Kind::Any && !constructionArgumentTypes) {
			activeInstantiation.completed = true;
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		}
		if (fieldType.kind == DataType::Kind::Unresolved && fieldType.typeExpression) {
			expandPendingTypeReferenceExpression(
				fieldType.typeExpression, field.range.line ? field.range.line->section : nullptr
			);
			if (inferenceContext) {
				Expression *typeExpression = fieldType.typeExpression;
				resetExpressionTypes(typeExpression);
				if (!inferExpression(typeExpression, *inferenceContext, false, bindingFrameStack, false))
					return {};
				fieldType.typeExpression = typeExpression;
				DataType inferredTypeRef;
				if (!readInferredTypeReferenceValue(fieldType.typeExpression, inferenceContext, inferredTypeRef))
					return {};
				fieldType = inferredTypeRef.toReferencedType();
			} else {
				DataType resolvedTypeRef;
				if (!resolveCompileTimeTypeReference(
						parseContext, fieldType.typeExpression, bindingFrameStack, resolvedTypeRef, inferenceContext
					) ||
					resolvedTypeRef.kind != DataType::Kind::Type)
					return {};
				fieldType = resolvedTypeRef.toReferencedType();
			}
		}

		if (constructionArgumentTypes) {
			DataType completedFieldType;
			if (!completeBoundClassFieldType(fieldType, (*constructionArgumentTypes)[fieldIndex], completedFieldType))
				return {};
			fieldType = std::move(completedFieldType);
		} else if (!fieldType.isConcrete()) {
			activeInstantiation.completed = true;
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		}
		fieldTypes.push_back(fieldType);
	}

	int instIndex = -1;
	for (int candidateIndex = 0; candidateIndex < static_cast<int>(classDef->instantiations.size()); candidateIndex++) {
		std::vector<DataType> candidateFieldTypes = fieldTypes;
		for (DataType &candidateFieldType : candidateFieldTypes)
			replaceSymbolicClassInstantiation(candidateFieldType, classDef, symbolicIndex, candidateIndex);
		if (candidateFieldTypes != classDef->instantiations[candidateIndex].fieldTypes)
			continue;
		fieldTypes = std::move(candidateFieldTypes);
		instIndex = candidateIndex;
		break;
	}
	if (instIndex < 0) {
		int newIndex = static_cast<int>(classDef->instantiations.size());
		for (DataType &fieldType : fieldTypes)
			replaceSymbolicClassInstantiation(fieldType, classDef, symbolicIndex, newIndex);
		recordClassInstantiationAppend(classDef);
		instIndex = classDef->getOrCreateInstantiation(fieldTypes);
		requireCompilerInvariant(instIndex == newIndex, "new recursive class instantiation did not use its planned index");
	}
	resolveStoredSymbolicClassInstantiation(classDef, symbolicIndex, instIndex);
	cacheClassInstantiationRequest(classDef, requestKey, instIndex);
	activeInstantiation.completed = true;
	return {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
}

static bool instantiateClassFromArgumentTypes(
	ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef, int baseClassInstIndex
) {
	if (!classDef || classDef->fields.size() != argumentTypes.size())
		return false;

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(argumentTypes.size());
	for (size_t i = 0; i < argumentTypes.size(); i++) {
		DataType argumentType = argumentTypes[i];
		if (!argumentType.isConcrete())
			return false;
		DataType fieldConstraint = classDef->fields[i].declaredType;
		if (baseClassInstIndex >= 0 && static_cast<size_t>(baseClassInstIndex) < classDef->instantiations.size() &&
			i < classDef->instantiations[baseClassInstIndex].fieldTypes.size()) {
			fieldConstraint = classDef->instantiations[baseClassInstIndex].fieldTypes[i];
		}
		DataType fieldType;
		if (!completeBoundClassFieldType(fieldConstraint, argumentType, fieldType))
			return false;
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	outTypeRef = {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
	return true;
}
