struct UnavailableInferenceBody {};

class InferenceBodyReader {
  public:
	InferenceBodyReader(
		ParseContext &context, const InferenceValueIdentities &identities, InferenceBodySources &sources,
		InferenceStorageIdentities &storage, std::function<Instantiation *(Section *, const InstantiationKey &)> restoreInstance
	)
		: context(context), identities(identities), sources(sources), storage(storage),
		  restoreInstance(std::move(restoreInstance)) {}
	~InferenceBodyReader() {
		if (!committed) {
			auto created = [&](const auto &entry) {
				return createdScopes.contains(entry.second.get());
			};
			std::erase_if(context.invocationStorageScopes, created);
			std::erase_if(context.instantiationStorageScopes, created);
		}
	}
	std::shared_ptr<InstantiatedSectionBody> restore(const Json &record) {
		this->record = &record;
		sources.bind(record.at("sources"));
		arena = context.createExpressionStorage();
		for (size_t index = 0; index < record.at("expressions").size(); index++)
			expressions.push_back(arena->create());
		for (size_t index = 0; index < record.at("bodies").size(); index++)
			bodies.push_back(std::make_shared<InstantiatedSectionBody>());
		for (size_t index = 0; index < record.at("variables").size(); index++)
			variables.push_back(context.createVariableReference({}, ""));
		for (size_t index = 0; index < record.at("minimum_identities").get<size_t>(); index++)
			minima.push_back(std::make_shared<const MinimumSignedIntegerMagnitudeIdentity>());
		types.resize(record.at("types").size());
		scopes.resize(record.at("scopes").size());
		for (size_t index = 0; index < bodies.size(); index++) {
			const Json &item = record.at("bodies").at(index);
			auto &result = *bodies[index];
			result.sourceSection = sources.section(item.at(0));
			result.parentBody = item.at(1).is_null() ? nullptr : bodies.at(item.at(1).get<size_t>()).get();
			result.expressions = context.createExpressionStorage();
			for (const Json &line : item.at(3))
				result.lineExpressions.push_back(expression(line));
			for (const Json &child : item.at(4))
				result.childBodies.push_back(body(child));
		}
		for (size_t index = 0; index < expressions.size(); index++) {
			using Field = InferenceBodyExpressionField;
			const Json &item = record.at("expressions").at(index);
			expressions[index]->owningSectionBody = body(item.at(static_cast<size_t>(Field::Owner))).get();
			expressions[index]->reusableTemplateExpression = expression(item.at(static_cast<size_t>(Field::Template)));
			for (const Json &argument : item.at(static_cast<size_t>(Field::Arguments)))
				expressions[index]->arguments.push_back(expression(argument));
		}
		for (size_t index = 0; index < variables.size(); index++)
			restoreVariable(*variables[index], record.at("variables").at(index));
		for (size_t index = 0; index < bodies.size(); index++)
			bodies[index]->storageScope = scope(record.at("bodies").at(index).at(2));
		for (size_t index = 0; index < expressions.size(); index++)
			restoreExpression(*expressions[index], record.at("expressions").at(index));
		return body(record.at("root"));
	}
	void commit() {
		// Bodies contain non-owning expression edges. One context-owned arena
		// keeps cross-body captures alive without shared-ownership cycles.
		context.restoredBodyExpressionArenas.push_back(arena);
		for (auto &reference : patternReferences)
			context.restoredBodyPatternReferences.push_back(std::move(reference));
		for (Expression *pack : capturedPacks)
			context.retainCapturedExpressionBody(pack);
		committed = true;
	}

  private:
	Expression *expression(const Json &value) const {
		if (value.is_null())
			return nullptr;
		return value.at(0) == "source" ? sources.expressionSource(value.at(1), storage)
									   : expressions.at(value.at(1).get<size_t>());
	}
	std::shared_ptr<InstantiatedSectionBody> body(const Json &value) const {
		return value.is_null() ? nullptr : bodies.at(value.get<size_t>());
	}
	std::shared_ptr<const VariableStorageScope> scope(const Json &value) {
		if (value.is_null())
			return {};
		size_t index = value.get<size_t>();
		if (scopes.at(index))
			return scopes[index];
		const Json &item = record->at("scopes").at(index);
		if (item.at(0) == "invocation") {
			auto parent = scope(item.at(1));
			size_t before = context.invocationStorageScopes.size();
			scopes[index] = context.storageScopeForInvocation(expression(item.at(2)), item.at(3).get<size_t>(), parent);
			if (context.invocationStorageScopes.size() != before)
				createdScopes.insert(scopes[index].get());
		} else {
			InstantiationKey key;
			for (const Json &argument : item.at(2))
				key.argumentTypes.push_back(type(argument));
			for (const Json &constant : item.at(3))
				key.compileTimeParameters.emplace_back(constant.at(0).get<std::string>(), this->value(constant.at(1)));
			size_t before = context.instantiationStorageScopes.size();
			scopes[index] = context.storageScopeForInstantiation(sources.section(item.at(1)), key);
			if (context.instantiationStorageScopes.size() != before)
				createdScopes.insert(scopes[index].get());
		}
		return scopes[index];
	}
	VariableReference *variable(const Json &value) const {
		if (value.is_null())
			return nullptr;
		return value.at(0) == "source" ? sources.declaration(value.at(1)) : variables.at(value.at(1).get<size_t>());
	}
	DataType type(const Json &value) {
		size_t index = value.get<size_t>();
		if (types.at(index))
			return *types[index];
		const Json &item = record->at("types").at(index);
		DataType result{static_cast<DataType::Kind>(item.at(0).get<int>()), item.at(1).get<int>(), item.at(2).get<int>()};
		result.referencedKind = static_cast<DataType::Kind>(item.at(3).get<int>());
		result.arraySize = item.at(4).get<int>();
		result.matrixRowCount = item.at(5).get<int>();
		if (!item.at(6).is_null())
			result.arrayElementType = std::make_shared<DataType>(type(item.at(6)));
		if (!item.at(7).is_null()) {
			auto *section = sources.section(item.at(7));
			result.classDefinition = static_cast<ClassSection *>(section)->classDefinition;
		}
		result.classInstIndex = item.at(8).get<int>();
		if (!item.at(9).is_null()) {
			std::vector<DataType> parameters;
			for (const Json &parameter : item.at(9).at(1))
				parameters.push_back(type(parameter));
			result.functionSignature = std::make_shared<FunctionSignature>(type(item.at(9).at(0)), std::move(parameters));
		}
		result.typeExpression = expression(item.at(10));
		types[index] = result;
		return result;
	}
	TypeConstraint constraint(const Json &value) {
		if (!portableConstraintReferencesPresent(value, identities.classes))
			throw UnavailableInferenceBody{};
		return decodePortableConstraint(value, identities.classes);
	}
	ConstraintIntegerTerm term(const Json &value) {
		ConstraintIntegerTerm result;
		result.kind = static_cast<ConstraintIntegerTerm::Kind>(value.at(0).get<int>());
		result.constant = value.at(1).get<int64_t>();
		result.sourceArgumentIndex = value.at(2).get<size_t>();
		result.dimension = value.at(3).get<int>();
		if (!value.at(4).is_null())
			result.left = std::make_shared<ConstraintIntegerTerm>(term(value.at(4)));
		if (!value.at(5).is_null())
			result.right = std::make_shared<ConstraintIntegerTerm>(term(value.at(5)));
		return result;
	}
	TypeConstraintTemplate constraintTemplate(const Json &value) {
		TypeConstraintTemplate result;
		result.constantPart = constraint(value.at(0));
		if (!value.at(1).is_null())
			result.rootProjection = ConstraintTypeProjection{value.at(1).at(0).get<size_t>(), value.at(1).at(1).get<size_t>()};
		size_t index = 2;
		for (auto *part :
			 {&result.numericSize, &result.pointerDepth, &result.arraySize, &result.matrixRows, &result.matrixColumns}) {
			if (!value.at(index).is_null())
				*part = term(value.at(index));
			index++;
		}
		if (!value.at(7).is_null())
			result.elementConstraint = std::make_shared<TypeConstraintTemplate>(constraintTemplate(value.at(7)));
		result.collectDependencies();
		return result;
	}
	void restoreVariable(VariableReference &result, const Json &value) {
		result.range = sources.range(value.at(0));
		result.name = value.at(1).get<std::string>();
		result.definition = variable(value.at(2));
		result.declaredTypeConstraintName = value.at(3).get<std::string>();
		result.declaredTypeConstraintRange = sources.range(value.at(4));
		result.declaredTypeConstraint = constraint(value.at(5));
		result.declaredType = type(value.at(6));
		result.hasDependentTypeConstraint = value.at(7).get<bool>();
		for (const Json &dependency : value.at(8))
			result.dependentTypeConstraints.push_back(
				{sources.definition(dependency.at(0)), dependency.at(1).get<size_t>(), constraintTemplate(dependency.at(2))}
			);
	}
	BindingFrameStack bindings(const Json &value) {
		BindingFrameStack result;
		for (const Json &item : value) {
			BindingFrame frame;
			for (const auto &[name, argument] : item.at(0).items())
				frame.bindings.emplace(name, expression(argument));
			for (const auto &[name, declaration] : item.at(1).items())
				frame.declarations.emplace(name, variable(declaration));
			for (const Json &parameter : item.at(2))
				frame.parameterBindings.emplace(variable(parameter.at(0)), expression(parameter.at(1)));
			for (const Json &parameter : item.at(3))
				frame.parameterConstraints.emplace(
					variable(parameter.at(0)), BindingFrame::ParameterConstraint{
												   constraint(parameter.at(1)), parameter.at(2).get<std::string>(),
												   parameter.at(3).get<std::string>(), sources.range(parameter.at(4))
											   }
				);
			result.pushFrame(std::move(frame));
		}
		return result;
	}
	ExpressionCapture captureValue(const Json &value) {
		auto *pack = expression(value.at(0));
		auto index = value.at(1).get<size_t>();
		auto frames = bindings(value.at(2));
		capturedPacks.insert(pack);
		return {
			expressionCaptureIdentity(pack, index, frames),
			std::make_shared<ExpressionCaptureData>(ExpressionCaptureData{pack, index, std::move(frames)})
		};
	}
	CompileTimeValue value(const Json &index) {
		if (index.is_null())
			return {};
		const Json &item = record->at("values").at(index.get<size_t>());
		if (item.at(0) == "minimum")
			return MinimumSignedIntegerMagnitude{minima.at(item.at(1).get<size_t>())};
		if (item.at(0) == "capture")
			return captureValue(item.at(1));
		if (item.at(0) == "pack") {
			ArgumentPackValue result;
			for (const Json &capture : item.at(1))
				result.elements.push_back(std::get<ExpressionCapture>(value(capture)));
			return result;
		}
		if (item.at(0) == "body_function")
			return CompileTimeFunction{sources.definition(item.at(1)), item.at(2).get<size_t>(), instance(item.at(3))};
		auto decoded = decodeInferenceValue(item, identities);
		if (!decoded)
			throw UnavailableInferenceBody{};
		return std::move(*decoded);
	}
	Instantiation *instance(const Json &index) {
		const Json &item = record->at("instances").at(index.get<size_t>());
		InstantiationKey key;
		for (const Json &argument : item.at(1))
			key.argumentTypes.push_back(type(argument));
		for (const Json &constant : item.at(2))
			key.compileTimeParameters.emplace_back(constant.at(0).get<std::string>(), value(constant.at(1)));
		auto *result = restoreInstance(sources.section(item.at(0)), key);
		if (!result)
			throw UnavailableInferenceBody{};
		return result;
	}
	std::shared_ptr<const ConfiguredDiagnosticMessage> configured(const Json &item) {
		return item.at("configured").is_null()
				   ? nullptr
				   : std::make_shared<ConfiguredDiagnosticMessage>(restoreConfiguredMessage(item.at("configured")));
	}
	std::shared_ptr<const Diagnostic> diagnostic(const Json &item) {
		if (item.is_null())
			return nullptr;
		auto result = std::make_shared<Diagnostic>();
		result->level = static_cast<Diagnostic::Level>(item.at(0).get<int>());
		result->message = renderCachedDiagnosticText(context, item.at(1));
		result->configuredMessage = configured(item.at(1));
		result->range = sources.range(item.at(2));
		for (const Json &note : item.at(3))
			result->relatedInfo.push_back(
				{renderCachedDiagnosticText(context, note.at(0)), sources.range(note.at(1)), configured(note.at(0))}
			);
		for (const Json &fix : item.at(4))
			result->quickFixes.push_back(
				{fix.at(0).get<std::string>(), sources.range(fix.at(1)), fix.at(2).get<std::string>()}
			);
		return result;
	}
	PatternMatch match(const Json &item) {
		PatternMatch result{};
		result.matchedEndNode = sources.node(item.at(0));
		for (const Json &definition : item.at(1))
			result.matchingDefinitions.push_back(sources.definition(definition));
		result.lineStartPos = item.at(2).get<size_t>();
		result.lineEndPos = item.at(3).get<size_t>();
		for (const Json &node : item.at(4))
			result.nodesPassed.push_back(sources.node(node));
		for (const Json &reference : item.at(5))
			result.discoveredVariables.push_back(
				{reference.at(0).get<std::string>(), reference.at(1).get<size_t>(), reference.at(2).get<size_t>(),
				 variable(reference.at(3))}
			);
		for (const Json &node : item.at(6))
			result.acceptedLiterals.push_back({sources.node(node)});
		for (const Json &child : item.at(7))
			result.subMatches.push_back(match(child));
		for (const Json &argument : item.at(8))
			result.orderedArguments.push_back(
				{argument.at(0).get<size_t>(), static_cast<MatchedArgument::Kind>(argument.at(1).get<int>()),
				 expression(argument.at(2)), argument.at(3).get<size_t>(), argument.at(4).get<bool>()}
			);
		return result;
	}
	void restoreExpression(Expression &result, const Json &item) {
		using Field = InferenceBodyExpressionField;
		auto field = [&](Field name) -> const Json & {
			return item.at(static_cast<size_t>(name));
		};
		result.kind = static_cast<Expression::Kind>(field(Field::Kind).get<int>());
		result.type = type(field(Field::Type));
		result.compileTimeValue = value(field(Field::Value));
		result.memoryFacts = {
			diagnostic(field(Field::Memory).at(0)), diagnostic(field(Field::Memory).at(1)),
			diagnostic(field(Field::Memory).at(2))
		};
		result.range = sources.range(field(Field::Range));
		std::visit([&](const auto &literal) {
			using T = std::decay_t<decltype(literal)>;
			if constexpr (std::is_assignable_v<decltype(result.literalValue) &, T>)
				result.literalValue = literal;
			else
				crashCompilerBug("inferred body literal has a nonliteral value");
		}, value(field(Field::Literal)));
		result.variable = variable(field(Field::Variable));
		result.dataDeclaration = expression(field(Field::Declaration));
		if (!field(Field::Data).is_null()) {
			result.dataSectionValues = std::make_shared<DataSectionValues>();
			result.dataSectionValues->storage = context.createExpressionStorage();
			for (const auto &[name, field] : field(Field::Data).at(0).items())
				result.dataSectionValues->fields.emplace(name, expression(field));
			result.dataSectionValues->record = expression(field(Field::Data).at(1));
		}
		result.inferredDataValue = expression(field(Field::DataValue));
		result.deferredFunction = sources.section(field(Field::Deferred));
		result.owningSectionBody = body(field(Field::Owner)).get();
		if (!field(Field::Match).is_null()) {
			result.syntheticPatternMatch = std::make_shared<PatternMatch>(match(field(Field::Match)));
			result.patternMatch = result.syntheticPatternMatch.get();
		}
		result.selectedPatternDefinition = sources.definition(field(Field::Definition));
		result.selectedPatternPathIndex = optional<size_t>(field(Field::Path));
		result.selectedCallableDefinition = sources.definition(field(Field::Callable));
		result.selectedCallablePathIndex = optional<size_t>(field(Field::CallablePath));
		result.subjectSetter = expression(field(Field::Subject));
		if (!field(Field::Capture).is_null())
			result.inferredArgumentReference = captureValue(field(Field::Capture));
		if (!field(Field::Instance).is_null()) {
			result.selectedInstantiation = instance(field(Field::Instance));
		}
		result.inferredFlexExpansion = expression(field(Field::Expansion));
		result.inferredPointerStorage = expression(field(Field::Pointer));
		result.inferredFlexBody = body(field(Field::Flex));
		result.inferredCallerBody = body(field(Field::Caller));
		if (!field(Field::Loops).is_null()) {
			result.staticLoopBodies.emplace();
			for (const Json &loop : field(Field::Loops))
				result.staticLoopBodies->push_back(body(loop));
		}
		result.sectionOutcome = CopyOnWrite<Expression::SectionOutcome>(Expression::SectionOutcome{
			static_cast<Expression::SectionOutcome::Kind>(field(Field::Outcome).at(0).get<int>()),
			value(field(Field::Outcome).at(1)), type(field(Field::Outcome).at(2))
		});
		result.executionFallsThrough = optional<bool>(field(Field::FallsThrough));
		result.sectionBodyReachable = field(Field::Reachable).get<bool>();
		result.sectionBodyExecution = static_cast<Expression::SectionBodyExecution>(field(Field::Execution).get<int>());
		result.sectionBodyFallsThrough = field(Field::BodyFallsThrough).get<bool>();
		if (!field(Field::Branch).is_null())
			result.branchSelection =
				Expression::BranchSelection{field(Field::Branch).at(0).get<bool>(), field(Field::Branch).at(1).get<int>()};
		result.reusableTemplateExpression = expression(field(Field::Template));
		result.intrinsicName = field(Field::Intrinsic).get<std::string>();
		if (!field(Field::Reference).is_null()) {
			const Json &reference = field(Field::Reference);
			auto restored = std::make_unique<PatternReference>(&result, static_cast<SectionType>(reference.at(0).get<int>()));
			restored->purpose = static_cast<PatternReference::Purpose>(reference.at(1).get<int>());
			restored->sourceRange = sources.range(reference.at(2));
			restored->pattern.text = reference.at(3).get<std::string>();
			restored->pattern.keyframes.clear();
			for (const Json &frame : reference.at(4))
				restored->pattern.keyframes.push_back({frame.at(0).get<size_t>(), frame.at(1).get<size_t>()});
			for (const Json &element : reference.at(5)) {
				PatternElement value{
					static_cast<PatternElement::Type>(element.at(0).get<int>()), element.at(1).get<std::string>(),
					element.at(2).get<size_t>()
				};
				value.isArgumentPack = element.at(3).get<bool>();
				restored->patternElements.push_back(std::move(value));
			}
			restored->expression = expression(reference.at(6));
			restored->matchingScope = sources.section(reference.at(7));
			restored->resolved = reference.at(8).get<bool>();
			if (!reference.at(9).is_null())
				restored->match = new PatternMatch(match(reference.at(9)));
			if (!reference.at(10).is_null())
				restored->variableResolutionOffsets =
					std::pair{reference.at(10).at(0).get<int>(), reference.at(10).at(1).get<int>()};
			result.patternReference = restored.get();
			patternReferences.push_back(std::move(restored));
		}
		result.isSubMatch = field(Field::Submatch).get<bool>();
		result.isExplicitGroup = field(Field::Grouped).get<bool>();
		result.groupingArgumentIndices = field(Field::GroupIndices).get<std::vector<int>>();
		result.groupingArgumentHasAdjacentSiblingSlot = field(Field::Adjacent).get<std::vector<bool>>();
		result.groupingStartsWithArgument = field(Field::StartsArgument).get<bool>();
		result.groupingEndsWithArgument = field(Field::EndsArgument).get<bool>();
	}
	template <class T> static std::optional<T> optional(const Json &value) {
		return value.is_null() ? std::nullopt : std::optional<T>{value.get<T>()};
	}
	ParseContext &context;
	const InferenceValueIdentities &identities;
	InferenceBodyReferenceReader sources;
	InferenceStorageIdentities &storage;
	std::function<Instantiation *(Section *, const InstantiationKey &)> restoreInstance;
	const Json *record = nullptr;
	std::shared_ptr<ExpressionStorage> arena;
	std::vector<Expression *> expressions;
	std::vector<VariableReference *> variables;
	std::vector<std::shared_ptr<InstantiatedSectionBody>> bodies;
	std::vector<std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity>> minima;
	std::vector<std::optional<DataType>> types;
	std::vector<std::shared_ptr<const VariableStorageScope>> scopes;
	std::set<Expression *> capturedPacks;
	std::set<const VariableStorageScope *> createdScopes;
	std::vector<std::unique_ptr<PatternReference>> patternReferences;
	bool committed = false;
};
