#include "runtimeConstantCall.h"

// Follow the finalized execution graph, rather than every type instantiated
// while resolving declarations, overloads or compile-time type queries.
class RuntimeLifecycleUses {
  public:
	explicit RuntimeLifecycleUses(InferenceContext &context) : context(context) {}
	std::vector<DataType> customTypes;
	bool valid = true;

	void collectType(const DataType &type) {
		if (type.isPointer() || !type.isConcrete() || !types.insert(type).second)
			return;
		if (type.kind == DataType::Kind::Array) {
			collectType(*type.arrayElementType);
			return;
		}
		if (type.kind != DataType::Kind::Class)
			return;
		ClassDefinition &definition = *type.classDefinition;
		if (definition.retainSection) {
			customTypes.push_back(type);
			return;
		}
		for (const DataType &field : definition.instantiations[type.classInstIndex].fieldTypes)
			collectType(field);
	}

	void collectInstantiation(Instantiation &instantiation, bool callable = false) {
		if (!instantiations.insert(&instantiation).second)
			return;
		collectType(instantiation.returnType);
		if (callable)
			for (const DataType &parameter : instantiation.argumentTypes)
				collectType(parameter);
		if (!instantiation.bodyInferred) {
			requireCompilerInvariant(
				instantiation.inferenceReusedFromCache && instantiation.valid && !instantiation.needsReinfer,
				"runtime function has neither an inferred body nor reusable facts"
			);
			return;
		}
		requireCompilerInvariant(instantiation.body != nullptr, "runtime function has no inferred body");
		collectBody(*instantiation.body);
	}

	void collectExposedFunctions(Section *section) {
		if (section->isExposed) {
			for (PatternDefinition *definition : section->patternDefinitions) {
				requireCompilerInvariant(definition->callableInstantiation, "exposed function has no inferred instantiation");
				collectInstantiation(*definition->callableInstantiation, true);
			}
		}
		for (Section *child : section->children)
			collectExposedFunctions(child);
	}

	void collectSection(Section *section) {
		collectStorageTypes(section);
		for (CodeLine *line : section->codeLines)
			if (line->expression && line->expression->executionFallsThrough.has_value())
				collectExpression(line->expression);
		for (Section *child : section->children)
			if (child->runsInEnclosingBody())
				collectSection(child);
	}

  private:
	std::set<DataType> types;
	std::unordered_set<Instantiation *> instantiations;
	std::unordered_set<Expression *> expressions;
	std::unordered_set<InstantiatedSectionBody *> bodies;

	void collectStorageTypes(Section *section) {
		for (const auto &[name, variable] : section->variables)
			collectType(variable->type);
	}

	void collectBody(InstantiatedSectionBody &body, const BindingFrameStack &bindings = {}) {
		if (!bodies.insert(&body).second)
			return;
		collectStorageTypes(body.sourceSection);
		for (Expression *expression : body.lineExpressions)
			if (expression && expression->executionFallsThrough.has_value())
				collectExpression(expression, bindings);
		for (const auto &child : body.childBodies)
			collectBody(*child, bindings);
	}

	bool prepareCallable(const CompileTimeFunction &function) {
		Instantiation &instantiation = *function.instantiation;
		if (!instantiation.inferenceReusedFromCache)
			return true;
		std::vector<CallableFunctionParameter> parameters;
		collectCallableFunctionParameters({function.definition, function.pathIndex}, parameters);
		requireCompilerInvariant(
			parameters.size() == instantiation.argumentTypes.size(), "symbolic callable lost its inferred parameter types"
		);
		std::vector<Expression> arguments(parameters.size());
		std::vector<std::pair<std::string, Expression *>> bindings;
		for (size_t index = 0; index < parameters.size(); index++) {
			arguments[index].kind = Expression::Kind::TypedPlaceholder;
			arguments[index].type = instantiation.argumentTypes[index];
			if (auto constant = instantiation.constantParameterValues.find(parameters[index].name);
				constant != instantiation.constantParameterValues.end())
				arguments[index].compileTimeValue = constant->second;
			bindings.emplace_back(parameters[index].name, &arguments[index]);
		}
		// Deferred callbacks infer their receiver from the section schema; the
		// authored parameter need not declare that concrete type itself.
		auto required = instantiation.requiredCompileTimeParameters;
		return ensureSectionInstantiationInferred(
			context.parseContext, function.definition->section, function.definition, bindings, instantiation.argumentTypes,
			required, context.currentInstantiation, &context
		);
	}
	bool prepareSelectedFunction(Expression *expression) {
		Instantiation &instance = *expression->selectedInstantiation;
		if (instantiations.contains(&instance) || !instance.inferenceReusedFromCache || instance.bodyInferred)
			return true;
		PatternDefinition *definition = expression->selectedCallableDefinition ? expression->selectedCallableDefinition
																			   : expression->selectedPatternDefinition;
		auto path = expression->selectedCallableDefinition ? expression->selectedCallablePathIndex
														   : expression->selectedPatternPathIndex;
		requireCompilerInvariant(definition && path, "runtime call lost its selected definition path");
		Section *section = definition->section;
		auto slot = std::ranges::find_if(section->instantiations, [&](const auto &entry) {
			return &entry.second == &instance;
		});
		requireCompilerInvariant(slot != section->instantiations.end(), "runtime call lost its selected instantiation");
		requireCompilerInvariant(
			context.parseContext.reuseInferredInstantiation &&
				context.parseContext.reuseInferredInstantiation(
					section, slot->first, instance, ParseContext::InferenceReuseUse::RuntimeCall, &context
				),
			"restored runtime dependency lost its inference facts"
		);
		return !instance.needsReinfer || prepareCallable({definition, *path, &instance});
	}

	void collectExpression(Expression *expression, const BindingFrameStack &bindings = {}) {
		if (!valid)
			return;
		ResolvedBindingLayers resolved = resolveVariableBindingWithCallerScope(expression, bindings);
		if (resolved.expression != expression) {
			collectExpression(resolved.expression, resolved.bindingFrameStack);
			return;
		}
		if (expression->type.isMetaType() || !expressions.insert(expression).second)
			return;
		collectType(expression->type);
		if (const auto *function = std::get_if<CompileTimeFunction>(&expression->compileTimeValue)) {
			size_t diagnosticCount = context.parseContext.diagnostics.size();
			if (!prepareCallable(*function)) {
				valid = false;
				if (context.parseContext.diagnostics.size() == diagnosticCount) {
					requireCompilerInvariant(
						!context.typeFailureDetail.empty(), "callable restoration failed without a diagnostic"
					);
					context.addDiagnosticWithCurrentTrace(buildFailureDetailDiagnostic(
						expression->range, context.typeFailureDetail, {}, context.configuredTypeFailureDetail
					));
				}
				return;
			}
			collectInstantiation(*function->instantiation, true);
		}
		bool folded = expression->selectedInstantiation && isCompileTimeKnown(runtimeConstantCallValue(expression, bindings));
		if (expression->selectedInstantiation) {
			if (!folded && !prepareSelectedFunction(expression)) {
				valid = false;
				return;
			}
			collectInstantiation(*expression->selectedInstantiation, expression->selectedCallableDefinition != nullptr);
		}
		if (expression->inferredDataValue)
			collectExpression(expression->inferredDataValue, bindings);
		BindingFrameStack replacementBindings = bindings;
		if (expression->inferredFlexBody) {
			pushPatternCallBindingScope(replacementBindings, expression, expression->selectedPatternDefinition);
			collectBody(*expression->inferredFlexBody, replacementBindings);
		}
		if (expression->inferredCallerBody)
			collectBody(*expression->inferredCallerBody, replacementBindings);
		if (expression->staticLoopBodies)
			for (const auto &body : *expression->staticLoopBodies)
				collectBody(*body, replacementBindings);
		if (expression->inferredFlexExpansion)
			collectExpression(expression->inferredFlexExpansion, replacementBindings);
		if (expression->inferredFlexBody)
			return;
		bool intrinsic = expression->kind == Expression::Kind::IntrinsicCall;
		if (intrinsic && intrinsicKind(expression->intrinsicName) == IntrinsicKind::DestroyAt)
			collectType(expression->arguments[1]->type.dereferenced());
		for (size_t index = intrinsic ? 1 : 0; index < expression->arguments.size(); index++) {
			if (intrinsic && intrinsicArgumentIsTypeOnly(expression->intrinsicName, static_cast<int>(index)))
				continue;
			collectExpression(expression->arguments[index], bindings);
		}
	}

  private:
	InferenceContext &context;
};
