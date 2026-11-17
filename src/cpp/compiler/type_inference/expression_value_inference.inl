void ExpressionInferenceSupport::inferLiteral() {
	CompileTimeValue literalValue{};
	if (std::optional<NumericLiteralValue> numericValue = numericLiteralValueOf(*expr)) {
		// Shader pipelines default explicit float literals to f32 to avoid
		// introducing Float64 arithmetic from constants like 0.5 or 800.0.
		NumericLiteralTyping typing = typeNumericLiteral(context.parseContext, expr->range.line, *numericValue);
		if (typing.unrepresentableBy) {
			setConfiguredTypeFailure(
				expr->range, "number rule literal unrepresentable", "message",
				{{"rule", std::string(typing.unrepresentableBy->range.subString)}}
			);
			context.typeFailureRelatedInfo.push_back(configuredRelatedInfo(
				context.parseContext, expr->range, "number rule literal unrepresentable", "related rule",
				typing.unrepresentableBy->range
			));
			return;
		}
		// A literal's typing does not depend on how its line is grouped, so trials record it too.
		if (typing.widenedBy) {
			context.parseContext.scopeRuleWarnings.push_back(
				{"number rule literal widened",
				 expr->range,
				 typing.widenedBy->range,
				 {{"rule", std::string(typing.widenedBy->range.subString)}, {"type", typeToUserName(typing.type)}}}
			);
		}
		if (typing.contradicted) {
			context.parseContext.scopeRuleWarnings.push_back(
				{"number rule contradicted",
				 expr->range,
				 typing.contradicted->range,
				 {{"explicit", std::string(expr->range.subString)},
				  {"rule", std::string(typing.contradicted->range.subString)}}}
			);
		}
		expr->type = typing.type;
		literalValue = typing.value;
	} else if (std::holds_alternative<std::string>(expr->literalValue)) {
		expr->type = {DataType::Kind::Int, 1};
		expr->type.pointerDepth = 1;
		literalValue = std::get<std::string>(expr->literalValue);
	}
	context.setExpressionValue(expr, literalValue);
}

void ExpressionInferenceSupport::inferArgumentPack() {
	ArgumentPackValue pack;
	for (size_t index = 0; index < expr->arguments.size(); index++) {
		Expression *argument = expr->arguments[index];
		CompileTimeValue forwarded = resolveStoredCompileTimeValue(argument, flexBindingFrameStack, &context);
		if (expr->arguments.size() == 1 && std::holds_alternative<ArgumentPackValue>(forwarded))
			pack = std::get<ArgumentPackValue>(std::move(forwarded));
		else
			pack.elements.push_back(captureExpression(context.parseContext, expr, index, flexBindingFrameStack));
	}
	expr->type = {DataType::Kind::ArgumentPack};
	expr->type.arraySize = static_cast<int>(pack.elements.size());
	context.setExpressionValue(expr, std::move(pack));
}

bool ExpressionInferenceSupport::inferArrayLiteral() {
	context.setExpressionValue(expr, {});
	if (expr->arguments.empty())
		return true;
	DataType elementType = ensureExpressionType(expr->arguments[0], context, flexBindingFrameStack);
	if (!elementType.isDeduced())
		return true;
	for (size_t i = 1; i < expr->arguments.size(); i++) {
		DataType nextType = ensureExpressionType(expr->arguments[i], context, flexBindingFrameStack);
		DataType merged;
		if (!mergeArrayElementType(elementType, nextType, merged)) {
			context.typesValid = false;
			setConfiguredTypeFailure(expr->range, "array literal element type mismatch");
			return false;
		}
		elementType = merged;
	}
	expr->type = DataType(DataType::Kind::Array);
	expr->type.arraySize = static_cast<int>(expr->arguments.size());
	expr->type.arrayElementType = std::make_shared<DataType>(elementType);
	if (typeHasManagedLifecycle(expr->type))
		markCurrentInstantiationImpure(context);
	return true;
}

bool ExpressionInferenceSupport::inferVariable() {
	if (expr->variable) {
		std::string varName = expr->variable->name;
		// Check flex bindings first
		ResolvedBindingLayers resolvedBinding = resolveVariableBindingWithCallerScope(expr, flexBindingFrameStack);
		Expression *flexBinding = resolvedBinding.expression;
		if (flexBinding && flexBinding != expr) {
			bool reusable = flexBinding->type.isDeduced() && context.inferredArguments->reusable(flexBinding);
			if (!reusable) {
				// A binding belongs to the caller, outside this expression's
				// grouping snapshot. Rejected trials must restore its selected
				// expansion together with the expansion's compile-time values.
				if (context.trial)
					context.trialJournal->recordBorrowedExpressionWrite(flexBinding);
				bool preserveBindingGrouping =
					flexBinding->type.isDeduced() ||
					(context.fixedGroupingRoots && context.fixedGroupingRoots->contains(flexBinding));
				bool inferred =
					inferExpression(flexBinding, context, preserveBindingGrouping, resolvedBinding.bindingFrameStack);
				if (!inferred)
					return false;
			}
			DataType boundType = ensureExpressionType(flexBinding, context, resolvedBinding.bindingFrameStack);
			if (boundType.isDeduced())
				expr->type = boundType;
			expr->inferredArgumentReference = flexBinding->inferredArgumentReference;
			CompileTimeValue variableValue = inferVariableCompileTimeValue(expr, context, flexBindingFrameStack);
			if (!isCompileTimeKnown(variableValue) && expr->type.kind == DataType::Kind::Type)
				variableValue = TypeReferenceValue::exact(expr->type);
			context.setExpressionValue(expr, variableValue);
			return true;
		}
		// Look up variable in scope
		Section *sec = expr->range.line ? expr->range.line->section : nullptr;
		Variable *var = sec ? sec->findVariable(expr->variable) : nullptr;
		if (!var) {
			context.typesValid = false;
			context.setConfiguredTypeFailure(expr->range, "unknown variable", "message", {{"name", varName}});
			if (!context.trial)
				context.addDiagnosticWithCurrentTrace(
					Diagnostic(context.parseContext, Diagnostic::Level::Error, "unknown variable", expr->range, "name", varName)
				);
			return false;
		}
		if (var->dataInitializer && !var->type.isDeduced()) {
			Expression *initializer = var->dataInitializer;
			Section *definition = initializer->patternMatch->matchingDefinitions.front()->section;
			if (definition->dataSection && definition->dataSection->resultType) {
				Expression *resultType = definition->dataSection->resultType;
				ensureExpressionType(resultType, context, flexBindingFrameStack);
				auto type = getCompileTimeTypeReferenceValue(context.lookupExpressionValue(resultType));
				if (!type) {
					context.fail(
						buildFailureDetailDiagnostic(resultType->range, "A data section result must be a compile-time type")
					);
					return false;
				}
				if (context.trial && context.trialJournal)
					context.trialJournal->recordVariableWrite(var);
				var->type = type->type.toReferencedType();
			}
		}
		if (var->isGlobal)
			context.noteAccessedGlobalReference(var->definition);
		if (var->isGlobal && var->dataInitializer && !expressionIsLValueOnlyUse(context, expr)) {
			// Pure execution may discover the value after inference visits this read.
			// An engaged null scope retains entry execution, rather than only source.
			context.recordBodyRead(*context.parseContext.mainSection, std::shared_ptr<const VariableStorageScope>{});
		}
		if (context.currentInstantiation && var->isGlobal && !var->dataInitializer && !expressionIsLValueOnlyUse(context, expr))
			markCurrentInstantiationImpure(context);
		restoreRefinedVariableType(expr, var, context);
		if (var->type.isDeduced())
			expr->type = var->type;
		else if (var->declaredType.isDeduced() && expressionIsLValueOnlyUse(context, expr))
			expr->type = var->declaredType;
		if (context.parseContext.incrementalPreinferenceActive && var->isGlobal && !expr->type.isDeduced() &&
			!expressionIsLValueOnlyUse(context, expr)) {
			context.fail(context.setConfiguredTypeFailure(expr->range, "variable has no type", "message", {{"name", varName}}));
			return false;
		}
	}
	context.setExpressionValue(expr, inferVariableCompileTimeValue(expr, context, flexBindingFrameStack));
	if (expr->type.kind == DataType::Kind::ArgumentReference && !context.inferringReferenceAssignmentTarget) {
		CompileTimeValue stored = context.lookupExpressionValue(expr);
		const auto *capture = std::get_if<ExpressionCapture>(&stored);
		requireCompilerInvariant(capture && capture->data, "argument reference variable lost its capture");
		expr->inferredArgumentReference = *capture;
		Expression *&referenced = capture->data->expression();
		if (!inferExpression(referenced, context, referenced->type.isDeduced(), capture->data->bindings))
			return false;
		expr->type = referenced->type;
		context.setExpressionValue(expr, context.lookupExpressionValue(referenced));
	}
	return true;
}
