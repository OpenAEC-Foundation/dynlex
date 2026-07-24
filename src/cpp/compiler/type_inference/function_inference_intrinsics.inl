case Expression::Kind::IntrinsicCall: {
	const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
	IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
	if (info) {
		for (size_t argumentIndex = 1; argumentIndex < expr->arguments.size(); argumentIndex++) {
			if (!intrinsicArgumentIsCompileTimeOnly(expr->intrinsicName, static_cast<int>(argumentIndex)))
				continue;
			Expression *argumentExpression = expr->arguments[argumentIndex];
			if (!argumentExpression)
				crashCompilerBug("intrinsic compile-time argument validation encountered null argument expression");
			CompileTimeValue argumentValue = resolveStoredCompileTimeValue(argumentExpression, flexBindingFrameStack, &context);
			if (!isCompileTimeKnown(argumentValue)) {
				failCompileTimeOnlyIntrinsicArgument(argumentIndex, "compile-time known");
				break;
			}
		}
		if (!context.typesValid)
			break;
	}
	if (info) {
		switch (info->returnKind) {
		case IntrinsicReturnKind::SameAsArgs:
			if (expr->arguments.size() == 2) {
				expr->type = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
			} else {
				DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
				DataType result;
				if (!DataType::promoteArithmetic(leftType, rightType, result)) {
					setConfiguredTypeFailure(
						expr->range, "incompatible operand types", "message",
						{{"left_type", typeToUserName(leftType, context.parseContext)},
						 {"right_type", typeToUserName(rightType, context.parseContext)}}
					);
					break;
				}
				expr->type = result;
			}
			break;
		case IntrinsicReturnKind::SameAsInts:
			if (expr->arguments.size() == 2) {
				DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!isBitwiseOperandType(valueType)) {
					setConfiguredTypeFailure(
						expr->range, "bitwise operator operand invalid", "message",
						{{"operator", expr->intrinsicName}, {"value_type", typeToUserName(valueType, context.parseContext)}}
					);
					break;
				}
				expr->type = valueType;
			} else {
				DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
				DataType result;
				if (!DataType::promoteBitwise(leftType, rightType, result)) {
					setConfiguredTypeFailure(
						expr->range, "bitwise operator operands invalid", "message",
						{{"operator", expr->intrinsicName},
						 {"left_type", typeToUserName(leftType, context.parseContext)},
						 {"right_type", typeToUserName(rightType, context.parseContext)}}
					);
					break;
				}
				expr->type = result;
			}
			break;
		case IntrinsicReturnKind::Bool: {
			if (kind == IntrinsicKind::And || kind == IntrinsicKind::Or) {
				DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
				if (!isLogicalOperandType(leftType) || !isLogicalOperandType(rightType)) {
					setConfiguredTypeFailure(
						expr->range, "logical operator operands invalid", "message",
						{{"operator", expr->intrinsicName},
						 {"left_type", typeToUserName(leftType, context.parseContext)},
						 {"right_type", typeToUserName(rightType, context.parseContext)}}
					);
					break;
				}
			} else if (kind == IntrinsicKind::Not) {
				DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!isLogicalOperandType(valueType)) {
					setConfiguredTypeFailure(
						expr->range, "logical operator operand invalid", "message",
						{{"operator", "not"}, {"value_type", typeToUserName(valueType, context.parseContext)}}
					);
					break;
				}
			} else {
				DataType leftType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				DataType rightType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
				DataType promoted;
				DataType refinedPointerType;
				bool pointerEquality =
					(kind == IntrinsicKind::Equal || kind == IntrinsicKind::NotEqual) && leftType.isPointer() &&
					rightType.isPointer() &&
					(leftType == rightType || refineUnspecifiedClassInstantiation(leftType, rightType, refinedPointerType));
				if (!pointerEquality && !DataType::promoteArithmetic(leftType, rightType, promoted)) {
					setConfiguredTypeFailure(
						expr->range, "incompatible operand types", "message",
						{{"left_type", typeToUserName(leftType, context.parseContext)},
						 {"right_type", typeToUserName(rightType, context.parseContext)}}
					);
					break;
				}
			}
			expr->type = {DataType::Kind::Bool};
			break;
		}
		case IntrinsicReturnKind::Void: {
			CompileTimeValue conditionValue;
			DataType sectionConditionType;
			if (kind == IntrinsicKind::Return) {
				Expression *returnValueExpression = expr->arguments.size() > 1 ? expr->arguments[1] : nullptr;
				Expression *sourceReturnValueExpression =
					returnValueExpression ? resolveThroughBindings(returnValueExpression, flexBindingFrameStack) : nullptr;
				ScopedRecursiveInferenceObservation returnValueObservation(context, context.currentInstantiation);
				DataType retType = returnValueExpression
									   ? ensureExpressionType(returnValueExpression, context, flexBindingFrameStack)
									   : DataType{DataType::Kind::Void};
				if (!retType.isDeduced()) {
					if (returnValueObservation.ownerObserved() && context.currentInstantiation) {
						expr->type = {DataType::Kind::Void};
						break;
					}
					context.setTypeFailure("return value type is unresolved");
					break;
				}
				if (context.currentInstantiation) {
					if (!reconcileFunctionReturnType(context, expr, sourceReturnValueExpression, retType))
						break;
				} else if (retType.kind != DataType::Kind::Void) {
					Range returnRange =
						context.activeFlexCallStack.empty() ? expr->range : context.activeFlexCallStack.back()->range;
					DataType programReturnType{DataType::Kind::Int, 4};
					if (!DataType::supportsRuntimeConversion(retType, programReturnType)) {
						std::string detail = renderConfiguredMessage(
							syntaxConfigForRange(context.parseContext, returnRange), "program return value invalid", "message",
							{{"value_type", typeToUserName(retType, context.parseContext)}}
						);
						failWithDetail(returnRange, detail);
						break;
					}
				}
				if (expr->arguments.size() <= 1)
					context.setExpressionValue(expr, {});
				else
					context.setExpressionValue(expr, context.lookupExpressionValue(expr->arguments[1]));
				if (context.currentInstantiation) {
					if (context.trial) {
						requireCompilerInvariant(context.trialJournal, "trial return provenance requires a rollback journal");
						context.trialJournal->recordInstantiationWrite(context.currentInstantiation);
					}
					AddressProvenance returnProvenance =
						returnValueExpression ? inferAddressProvenance(returnValueExpression, context, flexBindingFrameStack)
											  : AddressProvenance{};
					joinAddressProvenance(context.currentInstantiation->returnAddressProvenance, returnProvenance);
					context.currentInstantiation->hasReturnAddressProvenance = true;
				}
				if (!context.typesValid)
					break;
			}
			if ((kind == IntrinsicKind::If || kind == IntrinsicKind::ElseIf || kind == IntrinsicKind::LoopWhile) &&
				expr->arguments.size() > 1) {
				DataType operandType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!isLogicalOperandType(operandType)) {
					std::string operatorLabel = kind == IntrinsicKind::If		? "if condition"
												: kind == IntrinsicKind::ElseIf ? "else if condition"
																				: "loop while condition";
					setConfiguredTypeFailure(
						expr->range, "logical operator operand invalid", "message",
						{{"operator", operatorLabel}, {"value_type", typeToUserName(operandType, context.parseContext)}}
					);
					break;
				}
				conditionValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
				sectionConditionType = operandType;
			}
			if ((kind == IntrinsicKind::Switch || kind == IntrinsicKind::Case) && expr->arguments.size() > 1) {
				DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!valueType.isInteger()) {
					failConfigured(
						expr->arguments[1]->range,
						kind == IntrinsicKind::Switch ? "switch requires integer" : "case value must be constant integer"
					);
					break;
				}
				conditionValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
				sectionConditionType = valueType;
				if (kind == IntrinsicKind::Case &&
					(!getCompileTimeIntegerValue(conditionValue) ||
					 !isStructurallyCompileTimeConstant(expr->arguments[1], flexBindingFrameStack, context))) {
					failConfigured(expr->arguments[1]->range, "case value must be constant integer");
					break;
				}
			}
			if (context.typesValid)
				expr->sectionOutcome = sectionOutcomeForIntrinsic(kind, conditionValue, sectionConditionType);
			if (kind == IntrinsicKind::Store)
				inferStoreEffects(expr, context, flexBindingFrameStack);
			if (kind == IntrinsicKind::StoreAt || kind == IntrinsicKind::InitializeAt) {
				DataType pointerType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!pointerType.isDeduced() || !pointerType.isPointer()) {
					setConfiguredTypeFailure(expr->range, "store at requires pointer");
					break;
				}
				DataType elementType = pointerType.dereferenced();
				DataType valueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
				if (!context.typesValid)
					break;
				if (!isVariableAssignmentCompatible(elementType, valueType)) {
					setConfiguredTypeFailure(
						expr->range, "store at value incompatible", "message",
						{{"value_type", typeToUserName(valueType, context.parseContext)},
						 {"element_type", typeToUserName(elementType, context.parseContext)}}
					);
					break;
				}
				applyStoreThroughAddress(
					context, inferAddressProvenance(expr->arguments[1], context, flexBindingFrameStack), expr->arguments[2],
					flexBindingFrameStack
				);
			}
			if (kind == IntrinsicKind::DestroyAt) {
				DataType pointerType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!pointerType.isDeduced() || !pointerType.isPointer()) {
					setConfiguredTypeFailure(expr->range, "store at requires pointer");
					break;
				}
			}
			if (kind == IntrinsicKind::ExecuteBody && !inferSectionFlexCallerBody(expr, context))
				break;
			if (kind == IntrinsicKind::SetSubject) {
				DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!valueType.isDeduced() || valueType.kind == DataType::Kind::Void) {
					failWithDetail(expr->arguments[1]->range, "the subject must have a runtime value");
					break;
				}
				context.currentSubject = {.setter = expr, .ambiguous = false};
			}
			expr->type = {DataType::Kind::Void};
			break;
		}
		case IntrinsicReturnKind::Float:
			expr->type = {DataType::Kind::Float, 4};
			break;
		case IntrinsicReturnKind::Custom:
			if (kind == IntrinsicKind::LifecycleValue) {
				Section *sourceSection = expr->range.line ? expr->range.line->section : nullptr;
				while (sourceSection && sourceSection->type != SectionType::Retain &&
					   sourceSection->type != SectionType::Release) {
					sourceSection = sourceSection->parent;
				}
				if (!sourceSection) {
					failWithDetail(expr->range, "lifecycle value is only available in retain and release sections");
					break;
				}
				requireCompilerInvariant(
					context.currentInstantiation && context.currentInstantiation->argumentTypes.size() == 1,
					"lifecycle section inference is missing its managed value type"
				);
				expr->type = context.currentInstantiation->argumentTypes.front();
			} else if (kind == IntrinsicKind::Subject) {
				if (context.currentSubject.ambiguous) {
					failWithDetail(expr->range, "the subject depends on which control-flow path executes");
					break;
				}
				if (!context.currentSubject.setter) {
					failWithDetail(expr->range, "no subject has been set");
					break;
				}
				expr->subjectSetter = context.currentSubject.setter;
				if (expr->subjectSetter->arguments.size() <= 1)
					crashCompilerBug("subject assignment is missing its value expression");
				expr->type = ensureExpressionType(expr->subjectSetter->arguments[1], context, flexBindingFrameStack);
			} else if (kind == IntrinsicKind::CommandLineArgumentCount || kind == IntrinsicKind::CommandLineArgumentValues) {
				if (context.parseContext.options.emitWASM || context.parseContext.options.emitSPIRV) {
					failWithDetail(expr->range, "Command-line arguments are unavailable for this target", 0);
					break;
				}
				if (kind == IntrinsicKind::CommandLineArgumentCount) {
					expr->type = {DataType::Kind::Int, 4};
				} else {
					expr->type = {DataType::Kind::Int, 1};
					expr->type.pointerDepth = 2;
				}
			} else if (kind == IntrinsicKind::AddressOf) {
				DataType varType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (varType.isDeduced())
					expr->type = varType.pointed();
			} else if (kind == IntrinsicKind::Dereference) {
				DataType ptrType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (ptrType.isDeduced() && ptrType.isPointer())
					expr->type = ptrType.dereferenced();
			} else if (isExternalCallIntrinsicKind(kind)) {
				for (size_t metadataIndex = 1; metadataIndex <= 2; metadataIndex++) {
					Expression *metadataExpression = resolveThroughFlexBindings(expr->arguments[metadataIndex]);
					if (!metadataExpression || !std::holds_alternative<std::string>(metadataExpression->literalValue)) {
						failIntrinsicArgumentRequirement(metadataIndex, "a string literal");
						break;
					}
				}
				if (!context.typesValid)
					break;
				DataType retTypeRef = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
				if (retTypeRef.kind != DataType::Kind::Type) {
					setConfiguredTypeFailure(expr->range, "call return type must be type reference");
					break;
				}
				if (retTypeRef.kind == DataType::Kind::Type && (retTypeRef.referencedKind == DataType::Kind::Type ||
																retTypeRef.referencedKind == DataType::Kind::Unresolved)) {
					setConfiguredTypeFailure(expr->range, "call return type must be concrete runtime type");
					break;
				}
				size_t runtimeArgumentStart = externalCallRuntimeArgumentStart(kind);
				if (kind == IntrinsicKind::VariadicCall) {
					int fixedArgumentCount = 0;
					if (!resolveStoredCompileTimeInteger(
							expr->arguments[4], flexBindingFrameStack, fixedArgumentCount, &context
						)) {
						failIntrinsicArgumentRequirement(4, "a compile-time integer");
						break;
					}
					if (fixedArgumentCount < 0) {
						failWithDetail(
							expr->arguments[4]->range, "Intrinsic 'variadic call' fixed argument count cannot be negative", 0
						);
						break;
					}
					size_t callArgumentCount = expr->arguments.size() - runtimeArgumentStart;
					if (static_cast<size_t>(fixedArgumentCount) > callArgumentCount) {
						std::string argumentLabel = callArgumentCount == 1 ? " call argument" : " call arguments";
						failWithDetail(
							expr->arguments[4]->range,
							"Intrinsic 'variadic call' fixed argument count cannot exceed its " +
								std::to_string(callArgumentCount) + argumentLabel,
							0
						);
						break;
					}
				}
				for (size_t i = runtimeArgumentStart; i < expr->arguments.size(); i++) {
					DataType argType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
					if (argType.kind == DataType::Kind::Type) {
						setConfiguredTypeFailure(expr->range, "call arguments cannot be type values");
						break;
					}
					if (argType.kind == DataType::Kind::Void) {
						failWithDetail(expr->arguments[i]->range, "call arguments must have runtime values");
						break;
					}
				}
				if (!context.typesValid)
					break;
				for (size_t i = runtimeArgumentStart; i < expr->arguments.size(); i++) {
					retainExternalAddress(context, inferAddressProvenance(expr->arguments[i], context, flexBindingFrameStack));
				}
				invalidateExternalCallWrites(context);
				if (retTypeRef.kind == DataType::Kind::Type)
					expr->type = retTypeRef.toReferencedType();
			} else if (kind == IntrinsicKind::Function) {
				Expression *functionExpr = resolveThroughFlexBindings(expr->arguments[1]);
				requireCompilerInvariant(
					functionExpr != nullptr, "function intrinsic lost its argument expression during type inference"
				);
				if (!std::holds_alternative<std::string>(functionExpr->literalValue)) {
					setConfiguredTypeFailure(functionExpr->range, "function intrinsic requires string literal");
					if (!context.trial)
						context.addDiagnosticWithCurrentTrace(Diagnostic(
							context.parseContext, Diagnostic::Level::Error, "function intrinsic requires string literal",
							functionExpr->range
						));
					break;
				}
				std::string signature = std::get<std::string>(functionExpr->literalValue);
				std::vector<PatternDefinition *> callableMatches = findCallableFunctionDefinitionsBySignature(
					context.parseContext, signature, functionExpr->range.line ? functionExpr->range.line->sourceFile : nullptr
				);
				if (callableMatches.empty()) {
					setConfiguredTypeFailure(
						functionExpr->range, "unknown function reference", "message", {{"signature", signature}}
					);
					if (!context.trial)
						context.addDiagnosticWithCurrentTrace(Diagnostic(
							context.parseContext, Diagnostic::Level::Error, "unknown function reference", functionExpr->range,
							"signature", signature
						));
					break;
				}
				if (callableMatches.size() > 1) {
					setConfiguredTypeFailure(
						functionExpr->range, "ambiguous function reference", "message", {{"signature", signature}}
					);
					if (!context.trial)
						context.addDiagnosticWithCurrentTrace(Diagnostic(
							context.parseContext, Diagnostic::Level::Error, "ambiguous function reference", functionExpr->range,
							"signature", signature
						));
					break;
				}
				Instantiation *callableInstantiation =
					ensureCallableFunctionInstantiationInferred(callableMatches.front(), context, functionExpr->range);
				if (!callableInstantiation)
					break;
				expr->selectedCallableDefinition = callableMatches.front();
				expr->selectedInstantiation = callableInstantiation;
				expr->type = {DataType::Kind::Int, 1};
				expr->type.pointerDepth = 1;
			} else if (kind == IntrinsicKind::Cast) {
				DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				DataType typeArgType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
				if (typeArgType.kind != DataType::Kind::Type) {
					failCompileTimeOnlyIntrinsicArgument(2, "a compile-time type reference");
					break;
				}
				if (!isValidCastRuntimeType(valueType)) {
					setConfiguredTypeFailure(
						expr->range, "invalid cast source type", "message",
						{{"source_type", typeToUserName(valueType, context.parseContext)}}
					);
					break;
				}
				DataType castResultType;
				if (typeArgType.kind == DataType::Kind::Type &&
					!tryResolveCastResultType(valueType, typeArgType, castResultType)) {
					DataType requestedType = typeArgType.toReferencedType();
					setConfiguredTypeFailure(
						expr->range, "unsupported cast", "message",
						{{"from_type", typeToUserName(valueType, context.parseContext)},
						 {"to_type", typeToUserName(requestedType, context.parseContext)}}
					);
					break;
				}
				expr->type = castResultType;
			} else if (kind == IntrinsicKind::Type) {
				// @intrinsic("type", kindString[, bits])
				std::string kindStr;
				CompileTimeValue kindValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
				if (auto *str = std::get_if<std::string>(&kindValue))
					kindStr = *str;
				if (kindStr.empty()) {
					failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type kind string");
					break;
				}
				std::optional<DataType> typeRefType = makeBuiltinTypeReference(kindStr, context.parseContext.options.emitSPIRV);
				if (!typeRefType) {
					failWithDetail(
						expr->arguments[1]->range,
						"Unknown compile-time type kind '" + kindStr + "' in intrinsic '" + expr->intrinsicName + "'", 0
					);
					break;
				}
				std::optional<int> numericByteSize;
				if (expr->arguments.size() > 2) {
					int bitCount = 0;
					if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, bitCount, &context) ||
						bitCount <= 0 || bitCount % 8 != 0) {
						failIntrinsicArgumentRequirement(2, "a positive integer divisible by 8");
						break;
					}
					numericByteSize = bitCount / 8;
					typeRefType = makeBuiltinTypeReference(kindStr, context.parseContext.options.emitSPIRV, numericByteSize);
					if (!typeRefType) {
						failIntrinsicArgumentRequirement(2, "a numeric type kind that accepts a bit width");
						break;
					}
				}
				TypeReferenceValue typeRefValue =
					TypeReferenceValue::builtin(kindStr, context.parseContext.options.emitSPIRV, numericByteSize);
				expr->type = typeRefValue.type;
				context.setExpressionValue(expr, typeRefValue);
			} else if (kind == IntrinsicKind::Fix) {
				CompileTimeValue sourceValue =
					resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
				std::optional<TypeConstraint> constraint = getCompileTimeConstraintValue(sourceValue);
				if (!constraint) {
					failCompileTimeOnlyIntrinsicArgument(1, "a type or constraint value");
					break;
				}
				constraint->requiresCompileTimeValue = true;
				expr->type = {DataType::Kind::Constraint};
				context.setExpressionValue(expr, *constraint);
			} else if (kind == IntrinsicKind::TypeOf) {
				DataType valueType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (valueType.isDeduced()) {
					expr->type.kind = DataType::Kind::Type;
					expr->type.referencedKind = valueType.kind;
					expr->type.numericSize = valueType.numericSize;
					expr->type.pointerDepth = valueType.pointerDepth;
					expr->type.classDefinition = valueType.classDefinition;
					expr->type.classInstIndex = valueType.classInstIndex;
					expr->type.arraySize = valueType.arraySize;
					expr->type.arrayElementType =
						valueType.arrayElementType ? std::make_shared<DataType>(*valueType.arrayElementType) : nullptr;
					context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
				}
			} else if (kind == IntrinsicKind::Select) {
				DataType conditionType = expr->arguments[1]->type;
				if (!conditionType.isDeduced())
					conditionType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!isLogicalOperandType(conditionType)) {
					setConfiguredTypeFailure(
						expr->range, "logical operator operand invalid", "message",
						{{"operator", "select condition"}, {"value_type", typeToUserName(conditionType, context.parseContext)}}
					);
					break;
				}
				CompileTimeValue conditionValue =
					resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
				if (auto *condition = std::get_if<bool>(&conditionValue)) {
					size_t selectedArgumentIndex = *condition ? 2 : 3;
					DataType selectedType = expr->arguments[selectedArgumentIndex]->type;
					if (!selectedType.isDeduced())
						selectedType =
							ensureExpressionType(expr->arguments[selectedArgumentIndex], context, flexBindingFrameStack);
					if (selectedType.isDeduced())
						expr->type = selectedType;
					context.setExpressionValue(expr, context.lookupExpressionValue(expr->arguments[selectedArgumentIndex]));
					break;
				}
				DataType trueType = expr->arguments[2]->type;
				if (!trueType.isDeduced())
					trueType = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
				DataType falseType = expr->arguments[3]->type;
				if (!falseType.isDeduced())
					falseType = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
				DataType mergedType;
				if (!mergeSelectBranchTypes(trueType, falseType, mergedType)) {
					setConfiguredTypeFailure(
						expr->range, "incompatible operand types", "message",
						{{"left_type", typeToUserName(trueType, context.parseContext)},
						 {"right_type", typeToUserName(falseType, context.parseContext)}}
					);
					break;
				}
				expr->type = mergedType;
			} else if (kind == IntrinsicKind::SizeOf) {
				Expression *typeExpression = expr->arguments[1];
				if (!inferExpression(typeExpression, context, false, flexBindingFrameStack))
					break;
				expr->arguments[1] = typeExpression;
				DataType typeArgType;
				if (!resolveCompileTimeTypeReference(
						context.parseContext, expr->arguments[1], flexBindingFrameStack, typeArgType, &context
					) ||
					typeArgType.kind != DataType::Kind::Type) {
					failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type reference");
					break;
				}
				if (typeArgType.referencedKind == DataType::Kind::Type ||
					typeArgType.referencedKind == DataType::Kind::Unresolved) {
					setConfiguredTypeFailure(expr->range, "size of type invalid");
					break;
				}
				expr->type = {DataType::Kind::Int, 8};
			} else if (kind == IntrinsicKind::BuildInfo) {
				Expression *keyExpr = expr->arguments[1];
				if (!inferExpression(keyExpr, context, false, flexBindingFrameStack))
					break;
				expr->arguments[1] = keyExpr;
				CompileTimeValue keyValue = context.lookupExpressionValue(keyExpr);
				auto *key = std::get_if<std::string>(&keyValue);
				if (!key) {
					setConfiguredTypeFailure(expr->range, "build info key must be string literal");
					break;
				}
				std::optional<DataType> infoType = buildInfoValueType(*key);
				if (!infoType) {
					setConfiguredTypeFailure(expr->range, "unknown build info key", "message", {{"key", std::string(*key)}});
					break;
				}
				expr->type = *infoType;
			} else if (kind == IntrinsicKind::TargetIs) {
				Expression *targetExpr = expr->arguments[1];
				if (!inferExpression(targetExpr, context, false, flexBindingFrameStack))
					break;
				expr->arguments[1] = targetExpr;
				CompileTimeValue targetValue = context.lookupExpressionValue(targetExpr);
				auto *targetName = std::get_if<std::string>(&targetValue);
				if (!targetName) {
					setConfiguredTypeFailure(expr->range, "build target must be string literal");
					break;
				}
				if (!evaluateTargetIs(context.parseContext, *targetName).has_value()) {
					setConfiguredTypeFailure(
						expr->range, "unknown build target", "message", {{"target", std::string(*targetName)}}
					);
					break;
				}
				expr->type = {DataType::Kind::Bool};
			} else if (kind == IntrinsicKind::ShaderStageIs) {
				Expression *shaderStageExpr = expr->arguments[1];
				if (!inferExpression(shaderStageExpr, context, false, flexBindingFrameStack))
					break;
				expr->arguments[1] = shaderStageExpr;
				CompileTimeValue shaderStageValue = context.lookupExpressionValue(shaderStageExpr);
				auto *shaderStageName = std::get_if<std::string>(&shaderStageValue);
				if (!shaderStageName) {
					setConfiguredTypeFailure(expr->range, "shader stage must be string literal");
					break;
				}
				if (!evaluateShaderStageIs(context.parseContext, *shaderStageName).has_value()) {
					setConfiguredTypeFailure(
						expr->range, "unknown shader stage", "message", {{"stage", std::string(*shaderStageName)}}
					);
					break;
				}
				expr->type = {DataType::Kind::Bool};
			} else if (kind == IntrinsicKind::Array) {
				int arraySize = 0;
				if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, arraySize, &context) ||
					arraySize < 0) {
					failIntrinsicArgumentRequirement(1, "a non-negative integer");
					break;
				}
				expr->type.kind = DataType::Kind::Type;
				expr->type.referencedKind = DataType::Kind::Array;
				expr->type.arraySize = arraySize;
				if (expr->arguments.size() > 2) {
					DataType elemTypeRef = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
					if (elemTypeRef.kind == DataType::Kind::Constraint) {
						std::optional<TypeConstraint> elementConstraint = getCompileTimeConstraintValue(
							resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
						);
						if (!elementConstraint) {
							failCompileTimeOnlyIntrinsicArgument(2, "a compile-time element constraint");
							break;
						}
						TypeConstraint arrayConstraint = TypeConstraint::any();
						arrayConstraint.kind = DataType::Kind::Array;
						arrayConstraint.pointerDepth = 0;
						arrayConstraint.arraySize = arraySize;
						arrayConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
						expr->type = {DataType::Kind::Constraint};
						context.setExpressionValue(expr, arrayConstraint);
						break;
					}
					if (elemTypeRef.kind != DataType::Kind::Type) {
						failCompileTimeOnlyIntrinsicArgument(2, "a compile-time element type reference");
						break;
					}
					expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
				}
				TypeConstraint arrayConstraint = TypeConstraint::any();
				arrayConstraint.kind = DataType::Kind::Array;
				arrayConstraint.pointerDepth = 0;
				arrayConstraint.arraySize = arraySize;
				if (expr->arguments.size() > 2) {
					std::optional<TypeReferenceValue> elementValue = getCompileTimeTypeReferenceValue(
						resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
					);
					if (!elementValue)
						crashCompilerBug("inferred array element type is missing its type-reference value");
					arrayConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
				}
				context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(arrayConstraint)});
			} else if (kind == IntrinsicKind::Vector) {
				int vectorSize = 0;
				if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, vectorSize, &context) ||
					vectorSize < 1) {
					failIntrinsicArgumentRequirement(1, "an integer greater than 0");
					break;
				}
				expr->type.kind = DataType::Kind::Type;
				expr->type.referencedKind = DataType::Kind::Vector;
				expr->type.arraySize = vectorSize;
				expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
				if (expr->arguments.size() > 2) {
					DataType elemTypeRef = ensureExpressionType(expr->arguments[2], context, flexBindingFrameStack);
					if (elemTypeRef.kind == DataType::Kind::Constraint) {
						std::optional<TypeConstraint> elementConstraint = getCompileTimeConstraintValue(
							resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
						);
						if (!elementConstraint) {
							failCompileTimeOnlyIntrinsicArgument(2, "a compile-time vector element constraint");
							break;
						}
						TypeConstraint vectorConstraint = TypeConstraint::any();
						vectorConstraint.kind = DataType::Kind::Vector;
						vectorConstraint.pointerDepth = 0;
						vectorConstraint.arraySize = vectorSize;
						vectorConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
						expr->type = {DataType::Kind::Constraint};
						context.setExpressionValue(expr, vectorConstraint);
						break;
					}
					if (elemTypeRef.kind != DataType::Kind::Type) {
						failCompileTimeOnlyIntrinsicArgument(2, "a compile-time vector element type reference");
						break;
					}
					expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
				}
				if (!context.typesValid)
					break;
				TypeConstraint vectorConstraint = TypeConstraint::fromTypeReference(expr->type);
				if (expr->arguments.size() > 2) {
					std::optional<TypeReferenceValue> elementValue = getCompileTimeTypeReferenceValue(
						resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context)
					);
					if (!elementValue)
						crashCompilerBug("inferred vector element type is missing its type-reference value");
					vectorConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
				}
				context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(vectorConstraint)});
			} else if (kind == IntrinsicKind::Matrix) {
				int rows = 0;
				int columns = 0;
				if (!resolveStoredCompileTimeInteger(expr->arguments[1], flexBindingFrameStack, rows, &context) || rows < 1) {
					failIntrinsicArgumentRequirement(1, "an integer greater than 0");
					break;
				}
				if (!resolveStoredCompileTimeInteger(expr->arguments[2], flexBindingFrameStack, columns, &context) ||
					columns < 1) {
					failIntrinsicArgumentRequirement(2, "an integer greater than 0");
					break;
				}
				expr->type.kind = DataType::Kind::Type;
				expr->type.referencedKind = DataType::Kind::Matrix;
				expr->type.matrixRowCount = rows;
				expr->type.arraySize = columns;
				expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
				if (expr->arguments.size() > 3) {
					DataType elemTypeRef = ensureExpressionType(expr->arguments[3], context, flexBindingFrameStack);
					if (elemTypeRef.kind == DataType::Kind::Constraint) {
						std::optional<TypeConstraint> elementConstraint = getCompileTimeConstraintValue(
							resolveStoredCompileTimeValue(expr->arguments[3], flexBindingFrameStack, &context)
						);
						if (!elementConstraint) {
							failCompileTimeOnlyIntrinsicArgument(3, "a compile-time matrix element constraint");
							break;
						}
						TypeConstraint matrixConstraint = TypeConstraint::any();
						matrixConstraint.kind = DataType::Kind::Matrix;
						matrixConstraint.pointerDepth = 0;
						matrixConstraint.matrixRows = rows;
						matrixConstraint.matrixColumns = columns;
						matrixConstraint.elementConstraint = std::make_shared<TypeConstraint>(*elementConstraint);
						expr->type = {DataType::Kind::Constraint};
						context.setExpressionValue(expr, matrixConstraint);
						break;
					}
					if (elemTypeRef.kind != DataType::Kind::Type) {
						failCompileTimeOnlyIntrinsicArgument(3, "a compile-time matrix element type reference");
						break;
					}
					expr->type.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
				}
				if (!context.typesValid)
					break;
				TypeConstraint matrixConstraint = TypeConstraint::fromTypeReference(expr->type);
				if (expr->arguments.size() > 3) {
					std::optional<TypeReferenceValue> elementValue = getCompileTimeTypeReferenceValue(
						resolveStoredCompileTimeValue(expr->arguments[3], flexBindingFrameStack, &context)
					);
					if (!elementValue)
						crashCompilerBug("inferred matrix element type is missing its type-reference value");
					matrixConstraint.elementConstraint = std::make_shared<TypeConstraint>(elementValue->constraint);
				}
				context.setExpressionValue(expr, TypeReferenceValue{expr->type, std::move(matrixConstraint)});
			} else if (kind == IntrinsicKind::AddPointerDepth) {
				DataType typeArgType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (typeArgType.kind == DataType::Kind::Constraint) {
					CompileTimeValue sourceValue =
						resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
					std::optional<TypeConstraint> constraint = getCompileTimeConstraintValue(sourceValue);
					if (!constraint || (constraint->kind && (*constraint->kind == DataType::Kind::Type ||
															 *constraint->kind == DataType::Kind::Constraint))) {
						setConfiguredTypeFailure(expr->range, "pointer to type invalid");
						break;
					}
					constraint->pointerDepth = constraint->pointerDepth.value_or(0) + 1;
					expr->type = {DataType::Kind::Constraint};
					context.setExpressionValue(expr, *constraint);
					break;
				}
				if (typeArgType.kind != DataType::Kind::Type) {
					failCompileTimeOnlyIntrinsicArgument(1, "a compile-time type or constraint reference");
					break;
				}
				if (typeArgType.referencedKind == DataType::Kind::Type ||
					typeArgType.referencedKind == DataType::Kind::Unresolved) {
					setConfiguredTypeFailure(expr->range, "pointer to type invalid");
					break;
				}
				expr->type = typeArgType;
				expr->type.pointerDepth++;
				std::optional<TypeReferenceValue> sourceTypeValue = getCompileTimeTypeReferenceValue(
					resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context)
				);
				if (!sourceTypeValue)
					crashCompilerBug("pointer type shaping is missing its source type-reference value");
				sourceTypeValue->type = expr->type;
				sourceTypeValue->constraint.pointerDepth = sourceTypeValue->constraint.pointerDepth.value_or(0) + 1;
				context.setExpressionValue(expr, *sourceTypeValue);
			} else if (kind == IntrinsicKind::Construct) {
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
				if (!resolveCompileTimeTypeReference(
						context.parseContext, expr->arguments[1], flexBindingFrameStack, typeRefType, &context,
						allConstructionArgumentsDeduced ? &constructionArgumentTypes : nullptr
					) ||
					typeRefType.kind != DataType::Kind::Type) {
					setConfiguredTypeFailure(expr->range, "construct requires compile-time type reference");
					break;
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
							if (DataType::promoteArithmetic(
									*valueType.arrayElementType, matrixType.matrixElementType(), promoted
								) &&
								promoted == matrixType.matrixElementType()) {
								expr->type = matrixType;
							}
						}
					}

				} else if (typeRefType.classDefinition) {
					DataType instantiatedTypeRef;
					if (allConstructionArgumentsDeduced && instantiateClassFromArgumentTypes(
															   typeRefType.classDefinition, constructionArgumentTypes,
															   instantiatedTypeRef, typeRefType.classInstIndex
														   )) {
						expr->type = instantiatedTypeRef.toReferencedType();
					} else {
						DataType targetType = typeRefType.toReferencedType();
						if (expr->arguments.size() == targetType.classDefinition->fields.size() + 2 &&
							targetType.classInstIndex >= 0) {
							const auto &fieldTypes =
								targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
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
			} else if (kind == IntrinsicKind::Property) {
				DataType instType = ensureExpressionType(expr->arguments[1], context, flexBindingFrameStack);
				if (!instType.isDeduced()) {
					context.typesValid = false;
					break;
				}
				if (instType.isPointer() && instType.kind == DataType::Kind::Class)
					instType = instType.dereferenced();
				std::string fieldName;
				CompileTimeValue propertyValue =
					resolveStoredCompileTimeValue(expr->arguments[2], flexBindingFrameStack, &context);
				if (const auto *propertyName = std::get_if<std::string>(&propertyValue)) {
					fieldName = *propertyName;
				} else {
					Expression *propExpr = resolveThroughFlexBindings(expr->arguments[2]);
					fieldName = extractFieldName(propExpr);
				}
				if (fieldName.empty()) {
					failCompileTimeOnlyIntrinsicArgument(2, "a compile-time property name");
					break;
				}
				DataType builtInPropertyType = resolveBuiltInPropertyType(instType, fieldName);
				if (builtInPropertyType.isDeduced()) {
					expr->type = builtInPropertyType;
					break;
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
						expr->range, "class missing property", "message",
						{{"type", typeToUserName(instType, context.parseContext)}, {"property", fieldName}}
					);
				}
			} else {
				std::string uri =
					(expr && expr->range.line && expr->range.line->sourceFile) ? expr->range.line->sourceFile->uri : "";
				int line = (expr && expr->range.line) ? expr->range.line->sourceFileLineIndex + 1 : -1;
				crashUnimplementedIntrinsic("type inference", expr->intrinsicName, uri, line);
			}
			break;
		}
	}
	if (context.typesValid)
		markIntrinsicImpurityIfNeeded(expr, context, flexBindingFrameStack);
	context.setExpressionValue(expr, inferIntrinsicCompileTimeValue(expr, context, flexBindingFrameStack));
	break;
}
