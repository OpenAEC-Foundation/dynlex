#pragma once
#include "dataSections.h"

static bool validateDataSectionResult(
	Expression *call, Section *definition, const DataType &result, InferenceContext &context, const BindingFrameStack &bindings
) {
	Expression *schema = definition->dataSection->resultType;
	if (!schema)
		return true;
	auto storage = context.createExpressionStorage();
	Expression *type = context.parseContext.cloneExpressionTree(schema, *storage);
	ensureExpressionType(type, context, bindings);
	if (!context.typesValid)
		return false;
	auto constraint = getCompileTimeConstraintValue(context.lookupExpressionValue(type));
	if (!constraint || !constraint->accepts(result, isCompileTimeKnown(context.lookupExpressionValue(call)))) {
		context.fail(buildFailureDetailDiagnostic(call->range, "Section value does not satisfy its declared result type"));
		return false;
	}
	return true;
}

static bool
inferDataSectionFields(Expression *call, Section *definition, InferenceContext &context, const BindingFrameStack &bindings) {
	if (definition->dataSection->receiverType) {
		call->dataSectionValues = std::make_shared<DataSectionValues>();
		call->dataSectionValues->storage = context.createExpressionStorage();
		call->sectionBodyExecution = Expression::SectionBodyExecution::Explicit;
		return true;
	}
	Section *body = call->range.line->sectionOpening;
	requireCompilerInvariant(body, "data section call has no structural body");
	auto values = std::make_shared<DataSectionValues>();
	values->storage = context.createExpressionStorage();
	struct Field {
		DataBodyField *schema;
		TypeConstraint constraint;
		Expression *value;
		bool sequence;
	};
	std::vector<Field> fields;
	for (const auto &schema : definition->dataSection->fields) {
		Expression *constraintExpression = context.parseContext.cloneExpressionTree(schema->constraint, *values->storage);
		ensureExpressionType(constraintExpression, context, bindings);
		if (!context.typesValid)
			return false;
		auto constraint = getCompileTimeConstraintValue(context.lookupExpressionValue(constraintExpression));
		if (!constraint) {
			context.fail(
				buildFailureDetailDiagnostic(schema->range, "A section body field requires a compile-time type or constraint")
			);
			return false;
		}
		bool sequence = constraint->kind == DataType::Kind::Array && constraint->elementConstraint;
		Expression *value = nullptr;
		if (sequence) {
			value = values->storage->create();
			value->kind = Expression::Kind::ArrayLiteral;
			value->range = call->range;
			value->type = {DataType::Kind::Array};
			value->type.arraySize = 0;
		}
		fields.push_back({schema.get(), *constraint, value, sequence});
	}
	auto append = [&](Expression *item, Field *explicitField) {
		if (!inferExpression(item, context, false, bindings, false))
			return false;
		std::vector<Field *> accepting;
		for (Field &field : fields) {
			if (explicitField && &field != explicitField)
				continue;
			const auto &constraint = field.sequence ? *field.constraint.elementConstraint : field.constraint;
			if (constraint.accepts(item->type, isCompileTimeKnown(context.lookupExpressionValue(item))))
				accepting.push_back(&field);
		}
		std::optional<DataType> conversionType;
		if (!explicitField && std::ranges::any_of(accepting, [](Field *field) {
			return field->sequence;
		}))
			std::erase_if(accepting, [](Field *field) {
				return !field->sequence;
			});
		if (accepting.empty()) {
			for (Field &field : fields) {
				if (explicitField && &field != explicitField)
					continue;
				const auto &constraint = field.sequence ? *field.constraint.elementConstraint : field.constraint;
				auto conversion = findUserConversion(item, constraint, true, context, bindings);
				if (conversion.deferred) {
					requireCompilerInvariant(
						context.unresolvedPatternConstraintSignal != nullptr,
						"deferred section conversion outside signature inference"
					);
					*context.unresolvedPatternConstraintSignal = true;
					context.typesValid = false;
					return false;
				}
				if (!conversion.ambiguous.empty()) {
					reportAmbiguousUserConversion(item, typeToUserName(constraint), conversion.ambiguous, context);
					return false;
				}
				if (conversion.selected) {
					accepting.push_back(&field);
					conversionType = conversion.selected->outcomeType;
				}
			}
		}
		if (accepting.size() != 1) {
			context.fail(buildFailureDetailDiagnostic(
				item->range, accepting.empty() ? "This value does not match any section body field"
											   : "This value matches multiple section body fields"
			));
			return false;
		}
		Field &field = *accepting.front();
		if (conversionType && !tryApplyUserConversion(item, *conversionType, true, context, bindings))
			return false;
		DataType itemType = effectiveInferredExpressionType(item);
		if (field.sequence) {
			field.value->arguments.push_back(item);
			DataType merged = itemType;
			if (field.value->type.arrayElementType &&
				!mergeArrayElementType(*field.value->type.arrayElementType, itemType, merged)) {
				context.fail(buildFailureDetailDiagnostic(item->range, "Section body items have incompatible representations"));
				return false;
			}
			field.value->type.arrayElementType = std::make_shared<DataType>(merged);
			field.value->type.arraySize++;
		} else {
			if (field.value) {
				context.fail(buildFailureDetailDiagnostic(item->range, "A scalar section body field can have only one value"));
				return false;
			}
			field.value = item;
		}
		return true;
	};
	std::function<bool(Section *, InstantiatedSectionBody *, Field *)> read =
		[&](Section *section, InstantiatedSectionBody *instance, Field *explicitField) {
		for (size_t index = 0; index < section->codeLines.size(); index++) {
			CodeLine *line = section->codeLines[index];
			Expression *item = instance ? instance->lineExpressions[index] : line->expression;
			if (!item || line->resolved)
				continue;
			Section *fieldDefinition = item->patternMatch && !item->patternMatch->matchingDefinitions.empty()
										   ? item->patternMatch->matchingDefinitions.front()->section
										   : nullptr;
			if (line->sectionOpening && fieldDefinition && fieldDefinition->dataFieldOwner == definition) {
				auto field = std::ranges::find_if(fields, [&](const Field &candidate) {
					return candidate.schema == fieldDefinition->dataField;
				});
				requireCompilerInvariant(field != fields.end(), "named body section has no field schema");
				if (!read(line->sectionOpening, instance ? instance->bodyForChild(line->sectionOpening) : nullptr, &*field))
					return false;
			} else if (!append(item, explicitField))
				return false;
		}
		return true;
	};
	InstantiatedSectionBody *instantiated =
		context.currentInstantiatedSectionBody ? context.currentInstantiatedSectionBody->bodyForChild(body) : nullptr;
	if (!read(body, instantiated, nullptr))
		return false;
	for (Field &field : fields) {
		if (!field.value) {
			context.fail(buildFailureDetailDiagnostic(call->range, "Missing section body field: " + field.schema->name));
			return false;
		}
		if (field.sequence && !field.value->type.arrayElementType) {
			auto element =
				TypeReferenceValue::forConstraint(*field.constraint.elementConstraint, context.parseContext.options.emitSPIRV);
			if (!element) {
				context.fail(buildFailureDetailDiagnostic(call->range, "An empty section body needs a concrete element type"));
				return false;
			}
			field.value->type.arrayElementType = std::make_shared<DataType>(element->type.toReferencedType());
		}
		values->fields[field.schema->name] = field.value;
		if (field.sequence && !field.constraint.accepts(field.value->type, true)) {
			context.fail(buildFailureDetailDiagnostic(
				call->range, "Section body item count does not satisfy field: " + field.schema->name
			));
			return false;
		}
	}
	if (definition->dataSection->bodyClass) {
		Expression *record = values->storage->create();
		record->kind = Expression::Kind::IntrinsicCall;
		record->intrinsicName = "construct";
		record->range = call->range;
		Expression *keyword = values->storage->create();
		keyword->kind = Expression::Kind::Literal;
		keyword->literalValue = std::string("construct");
		keyword->range = call->range;
		std::vector<DataType> fieldTypes;
		for (const Field &field : fields)
			fieldTypes.push_back(effectiveInferredExpressionType(field.value));
		DataType recordType{DataType::Kind::Class};
		recordType.classDefinition = definition->dataSection->bodyClass;
		if (context.trial && context.trialJournal)
			context.trialJournal->recordClassInstantiationAppend(recordType.classDefinition);
		recordType.classInstIndex = recordType.classDefinition->getOrCreateInstantiation(fieldTypes);
		Expression *type = values->storage->create();
		type->kind = Expression::Kind::TypedPlaceholder;
		type->type = recordType.asTypeReference();
		type->range = call->range;
		record->arguments = {keyword, type};
		for (const Field &field : fields)
			record->arguments.push_back(field.value);
		record->type = recordType;
		values->record = record;
	}
	call->dataSectionValues = std::move(values);
	call->sectionBodyExecution = Expression::SectionBodyExecution::Explicit;
	return true;
}

static void inferDeferredSectionBody(Expression *expression, InferenceContext &context, const BindingFrameStack &bindings) {
	for (size_t index = context.activeFlexCallStack.size(); index > 0; --index) {
		Expression *call = context.activeFlexCallStack[index - 1];
		if (!call->deferredFunction)
			continue;
		Section *function = call->deferredFunction;
		PatternDefinition *definition = function->patternDefinitions.front();
		const auto &schema = *context.activeFlexDefinitionStack[index - 1]->dataSection;
		Expression *receiverType =
			context.parseContext.cloneExpressionTree(schema.receiverType, *call->dataSectionValues->storage);
		ensureExpressionType(receiverType, context, bindings);
		if (!context.typesValid)
			return;
		auto reference = getCompileTimeTypeReferenceValue(context.lookupExpressionValue(receiverType));
		if (!reference || !reference->type.toReferencedType().isConcrete() ||
			!reference->type.toReferencedType().isRuntimeValueType()) {
			context.fail(buildFailureDetailDiagnostic(call->range, "A callback receiver requires a concrete runtime type"));
			return;
		}
		Expression *receiver = call->dataSectionValues->storage->create();
		receiver->kind = Expression::Kind::TypedPlaceholder;
		receiver->type = reference->type.toReferencedType();
		receiver->range = call->range;
		std::vector<std::pair<std::string, Expression *>> arguments{{"__section_receiver", receiver}};
		std::vector<DataType> types{receiver->type};
		if (!ensureSectionInstantiationInferred(
				context.parseContext, function, definition, arguments, types, {}, context.currentInstantiation, &context
			)) {
			context.typesValid = false;
			return;
		}
		auto key = buildInstantiationKey({}, arguments, types, [](Expression *) {
			return CompileTimeValue{};
		});
		expression->selectedCallableDefinition = definition;
		expression->selectedCallablePathIndex = 0;
		expression->selectedInstantiation = &function->instantiations.at(key);
		DataType result = expression->selectedInstantiation->returnType;
		if (schema.callbackResultType) {
			Expression *contract =
				context.parseContext.cloneExpressionTree(schema.callbackResultType, *call->dataSectionValues->storage);
			ensureExpressionType(contract, context, bindings);
			if (!context.typesValid)
				return;
			auto constraint = getCompileTimeConstraintValue(context.lookupExpressionValue(contract));
			if (!constraint || !constraint->accepts(result, false)) {
				context.fail(
					buildFailureDetailDiagnostic(call->range, "Callback return type does not satisfy its declared returns type")
				);
				return;
			}
		}
		expression->type = makeFunctionType(result, types);
		return;
	}
	context.fail(buildFailureDetailDiagnostic(expression->range, "deferred body requires a section with a receiver"));
}

static void inferSectionReceiver(Expression *expression, InferenceContext &context) {
	Section *function = context.currentInstantiation && context.currentInstantiation->body
							? context.currentInstantiation->body->sourceSection
							: nullptr;
	if (!function || !function->receiverParameter) {
		context.fail(buildFailureDetailDiagnostic(expression->range, "receiver is only available in a deferred section body"));
		return;
	}
	auto storage = context.createExpressionStorage();
	Expression *value = storage->create();
	value->kind = Expression::Kind::Variable;
	value->variable = function->receiverParameter;
	value->range = function->receiverParameter->range;
	value->type = context.currentInstantiation->argumentTypes.front();
	expression->inferredValueStorage = std::move(storage);
	expression->inferredDataValue = value;
	expression->type = value->type;
}

static void inferDataSectionField(Expression *expression, InferenceContext &context) {
	bool wholeBody = expression->intrinsicName == "section body";
	CompileTimeValue keyValue = wholeBody ? CompileTimeValue{} : context.lookupExpressionValue(expression->arguments[1]);
	auto key = std::get_if<std::string>(&keyValue);
	if (!wholeBody && !key) {
		context.fail(buildFailureDetailDiagnostic(expression->range, "A section field name must be a compile-time string"));
		return;
	}
	for (auto call = context.activeFlexCallStack.rbegin(); call != context.activeFlexCallStack.rend(); ++call) {
		if (!(*call)->dataSectionValues)
			continue;
		Expression *value = (*call)->dataSectionValues->record;
		if (!wholeBody) {
			auto field = (*call)->dataSectionValues->fields.find(*key);
			value = field == (*call)->dataSectionValues->fields.end() ? nullptr : field->second;
		}
		if (!value) {
			context.fail(buildFailureDetailDiagnostic(
				expression->range, wholeBody ? "This section has no body schema" : "Unknown section body field: " + *key
			));
			return;
		}
		expression->inferredDataValue = value;
		expression->dataSectionValues = (*call)->dataSectionValues;
		expression->type = effectiveInferredExpressionType(value);
		context.setExpressionValue(expression, context.lookupExpressionValue(value));
		return;
	}
	context.fail(buildFailureDetailDiagnostic(expression->range, "section field is only valid inside a data section value"));
}
