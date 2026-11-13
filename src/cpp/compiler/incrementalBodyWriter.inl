class InferenceBodyWriter {
  public:
	InferenceBodyWriter(
		const InferenceValueIdentities &identities, InferenceBodySources &sources, InferenceStorageIdentities &storage,
		InferenceClassSnapshot &classSnapshot, std::function<CompileTimeValue(const Expression *)> readValue = {}
	)
		: identities(identities), sources(sources), storage(storage), classSnapshot(classSnapshot),
		  readValue(std::move(readValue)) {}
	Json capture(const Instantiation &instance) {
		auto started = Clock::now();
		for (const auto &argument : instance.argumentTypes)
			addTypeClasses(argument);
		addTypeClasses(instance.returnType);
		for (const auto &[name, constant] : instance.constantParameterValues)
			(void)value(constant);
		Json result{
			{"root", body(instance.body.get())},
			{"for_values", instance.purity == InstantiationPurity::Pure && instance.returnType.kind != DataType::Kind::Void}
		};
		size_t expressionIndex = 0, bodyIndex = 0, variableIndex = 0;
		while (expressionIndex < expressions.size() || bodyIndex < bodies.size() || variableIndex < variables.size()) {
			if (bodyIndex < bodies.size()) {
				const auto *value = bodies[bodyIndex++];
				Json lines = Json::array(), children = Json::array();
				for (Expression *line : value->lineExpressions)
					lines.push_back(expression(line));
				for (const auto &child : value->childBodies)
					children.push_back(body(child.get()));
				bodyRecords.push_back(
					Json::array(
						{sources.section(value->sourceSection), body(value->parentBody), scope(value->storageScope),
						 std::move(lines), std::move(children)}
					)
				);
			}
			if (expressionIndex < expressions.size())
				expressionRecords.push_back(expressionRecord(*expressions[expressionIndex++]));
			if (variableIndex < variables.size())
				variableRecords.push_back(variableRecord(*variables[variableIndex++]));
		}
		result["expressions"] = std::move(expressionRecords);
		result["expression_fields"] = inferenceBodyExpressionFields;
		result["values"] = std::move(valueRecords);
		result["instances"] = std::move(instantiationRecords);
		result["bodies"] = std::move(bodyRecords);
		result["variables"] = std::move(variableRecords);
		result["types"] = std::move(types);
		result["scopes"] = std::move(scopes);
		result["sources"] = sources.take();
		result["minimum_identities"] = minima.size();
		result["dependencies"] = dependencies;
		result["classes"] = std::move(classRecords);
		captureElapsed = elapsedMilliseconds(started);
		return result;
	}

	size_t valueReferenceCount() const { return valueReferences; }
	size_t instantiationReferenceCount() const { return instantiationReferences; }
	double captureMilliseconds() const { return captureElapsed; }

  private:
	template <class T>
	Json intern(const T *value, std::unordered_map<const T *, size_t> &indices, std::vector<const T *> &values) {
		if (!value)
			return nullptr;
		auto [found, inserted] = indices.try_emplace(value, values.size());
		if (inserted)
			values.push_back(value);
		return found->second;
	}
	Json expression(const Expression *value) {
		if (!value)
			return nullptr;
		if (auto found = expressionReferences.find(value); found != expressionReferences.end())
			return found->second;
		Json reference;
		if (auto source = storage.encodeExpression(value))
			reference = Json::array({"source", sources.expressionSource(std::move(*source))});
		else {
			reference = Json::array({"local", expressions.size()});
			expressions.push_back(value);
		}
		return expressionReferences.emplace(value, std::move(reference)).first->second;
	}
	Json body(const InstantiatedSectionBody *value) { return intern(value, bodyIndices, bodies); }
	Json scope(const std::shared_ptr<const VariableStorageScope> &value) {
		if (!value)
			return nullptr;
		auto [found, inserted] = scopeIndices.try_emplace(value.get(), scopes.size());
		size_t index = found->second;
		if (!inserted)
			return index;
		scopes.push_back(nullptr);
		Json record;
		if (const auto *origin = storage.instantiationOrigin(value.get())) {
			const auto &[section, key] = *origin;
			Json arguments = Json::array(), constants = Json::array();
			for (const DataType &argument : key.argumentTypes)
				arguments.push_back(type(argument));
			for (const auto &[name, constant] : key.compileTimeParameters)
				constants.push_back(Json::array({name, this->value(constant)}));
			record = Json::array({"instance", sources.section(section), std::move(arguments), std::move(constants)});
		} else {
			const auto *invocation = storage.invocationOrigin(value.get());
			requireCompilerInvariant(invocation, "inferred body scope has no logical origin");
			const auto &[parent, site, iteration] = *invocation;
			record = Json::array({"invocation", scope(parent), expression(site), iteration});
		}
		scopes[index] = std::move(record);
		return index;
	}
	Json variable(const VariableReference *value) {
		if (!value)
			return nullptr;
		Json source = sources.declaration(value);
		return source.is_null() ? Json::array({"local", intern(value, variableIndices, variables)})
								: Json::array({"source", std::move(source)});
	}
	Json type(const DataType &value) {
		if (auto found = typeIndices.find(value); found != typeIndices.end())
			return found->second;
		addTypeClasses(value);
		size_t index = types.size();
		types.push_back(nullptr);
		typeIndices.emplace(value, index);
		Json arguments = Json::array();
		if (value.functionSignature)
			for (const auto &argument : value.functionSignature->parameters)
				arguments.push_back(type(argument));
		Json record = Json::array(
			{static_cast<int>(value.kind), value.numericSize, value.pointerDepth, static_cast<int>(value.referencedKind),
			 value.arraySize, value.matrixRowCount, value.arrayElementType ? type(*value.arrayElementType) : Json(nullptr),
			 value.classDefinition ? sources.section(identities.sections.at(identities.classes.at(value.classDefinition)))
								   : Json(nullptr),
			 value.classInstIndex,
			 value.functionSignature ? Json::array({type(value.functionSignature->result), std::move(arguments)})
									 : Json(nullptr),
			 expression(value.typeExpression)}
		);
		types[index] = std::move(record);
		return index;
	}
	Json constraint(const TypeConstraint &value) {
		addConstraintClasses(value);
		auto encoded = encodePortableConstraint(value, identities.classes);
		requireCompilerInvariant(encoded.has_value(), "inferred body constraint has no portable identity");
		return std::move(*encoded);
	}
	Json term(const ConstraintIntegerTerm &value) {
		return Json::array(
			{static_cast<int>(value.kind), value.constant, value.sourceArgumentIndex, value.dimension,
			 value.left ? term(*value.left) : Json(nullptr), value.right ? term(*value.right) : Json(nullptr)}
		);
	}
	Json constraintTemplate(const TypeConstraintTemplate &value) {
		Json record = Json::array(
			{constraint(value.constantPart),
			 value.rootProjection ? Json::array({value.rootProjection->sourceArgumentIndex, value.rootProjection->elementDepth})
								  : Json(nullptr)}
		);
		for (const auto *part :
			 {&value.numericSize, &value.pointerDepth, &value.arraySize, &value.matrixRows, &value.matrixColumns})
			record.push_back(*part ? term(**part) : Json(nullptr));
		record.push_back(value.elementConstraint ? constraintTemplate(*value.elementConstraint) : Json(nullptr));
		return record;
	}
	Json variableRecord(const VariableReference &value) {
		Json dependencies = Json::array();
		for (const auto &dependency : value.dependentTypeConstraints)
			dependencies.push_back(
				Json::array(
					{sources.definition(dependency.definition), dependency.pathIndex, constraintTemplate(dependency.constraint)}
				)
			);
		return Json::array(
			{sources.range(value.range), value.name, variable(value.definition), value.declaredTypeConstraintName,
			 sources.range(value.declaredTypeConstraintRange), constraint(value.declaredTypeConstraint),
			 type(value.declaredType), value.hasDependentTypeConstraint, std::move(dependencies)}
		);
	}
	Json bindings(const BindingFrameStack &value) {
		Json frames = Json::array();
		value.forEachFrame([&](const BindingFrame &frame) {
			Json names = Json::object(), declarations = Json::object(), parameters = Json::array(), constraints = Json::array();
			for (const auto &[name, item] : frame.bindings)
				names[name] = expression(item);
			for (const auto &[name, item] : frame.declarations)
				declarations[name] = variable(item);
			for (const auto &[name, item] : frame.parameterBindings)
				parameters.push_back(Json::array({variable(name), expression(item)}));
			for (const auto &[name, item] : frame.parameterConstraints)
				constraints.push_back(
					Json::array(
						{variable(name), constraint(item.constraint), item.parameterName, item.sourceConstraintName,
						 sources.range(item.sourceRange)}
					)
				);
			frames.push_back(
				Json::array({std::move(names), std::move(declarations), std::move(parameters), std::move(constraints)})
			);
		});
		return frames;
	}
	Json captureValue(const ExpressionCapture &value) {
		requireCompilerInvariant(value.data != nullptr, "inferred argument capture has no expression data");
		return Json::array({expression(value.data->pack), value.data->index, bindings(value.data->bindings)});
	}
	static std::optional<std::pair<size_t, std::uint64_t>> scalarValueKey(const CompileTimeValue &item) {
		if (const auto *value = std::get_if<std::int64_t>(&item))
			return std::pair{item.index(), std::bit_cast<std::uint64_t>(*value)};
		if (const auto *value = std::get_if<std::uint64_t>(&item))
			return std::pair{item.index(), *value};
		if (const auto *value = std::get_if<double>(&item))
			return std::pair{item.index(), std::bit_cast<std::uint64_t>(*value)};
		if (const auto *value = std::get_if<bool>(&item))
			return std::pair{item.index(), static_cast<std::uint64_t>(*value)};
		if (const auto *value = std::get_if<CompileTimePointer>(&item))
			return std::pair{item.index(), value->address};
		return {};
	}
	Json value(const CompileTimeValue &item) {
		if (std::holds_alternative<std::monostate>(item))
			return nullptr;
		valueReferences++;
		auto lookup = [](const auto &cache, const auto &key) -> std::optional<size_t> {
			auto found = cache.find(key);
			return found == cache.end() ? std::nullopt : std::optional<size_t>{found->second};
		};
		auto scalar = scalarValueKey(item);
		const auto *text = std::get_if<std::string>(&item);
		const auto *reference = std::get_if<TypeReferenceValue>(&item);
		const auto *constraint = std::get_if<TypeConstraint>(&item);
		const auto *capture = std::get_if<ExpressionCapture>(&item);
		std::optional<size_t> previous;
		if (scalar)
			previous = lookup(scalarValues, *scalar);
		else if (text)
			previous = lookup(stringValues, *text);
		else if (reference)
			previous = lookup(typeValues, *reference);
		else if (constraint)
			previous = lookup(constraintValues, *constraint);
		else if (capture)
			previous = lookup(captureValues, capture->identity);
		if (previous) {
			if (capture)
				requireCompilerInvariant(capture->data != nullptr, "inferred argument capture has no expression data");
			return *previous;
		}
		Json encoded = valueRecord(item);
		auto [found, inserted] = valueIndices.try_emplace(encoded, valueRecords.size());
		size_t index = found->second;
		if (inserted)
			valueRecords.push_back(std::move(encoded));
		if (scalar)
			scalarValues.emplace(*scalar, index);
		else if (text)
			stringValues.emplace(*text, index);
		else if (reference)
			typeValues.emplace(*reference, index);
		else if (constraint)
			constraintValues.emplace(*constraint, index);
		else if (capture)
			captureValues.emplace(capture->identity, index);
		return index;
	}
	Json valueRecord(const CompileTimeValue &item) {
		if (const auto *reference = std::get_if<TypeReferenceValue>(&item)) {
			addTypeClasses(reference->type);
			addConstraintClasses(reference->constraint);
		}
		if (const auto *constraint = std::get_if<TypeConstraint>(&item))
			addConstraintClasses(*constraint);
		if (const auto *minimum = std::get_if<MinimumSignedIntegerMagnitude>(&item)) {
			auto [found, _] = minima.try_emplace(minimum->identity.get(), minima.size());
			return Json::array({"minimum", found->second});
		}
		if (const auto *capture = std::get_if<ExpressionCapture>(&item))
			return Json::array({"capture", captureValue(*capture)});
		if (const auto *pack = std::get_if<ArgumentPackValue>(&item)) {
			Json values = Json::array();
			for (const auto &capture : pack->elements)
				values.push_back(value(CompileTimeValue{capture}));
			return Json::array({"pack", std::move(values)});
		}
		if (const auto *function = std::get_if<CompileTimeFunction>(&item))
			return Json::array(
				{"body_function", sources.definition(function->definition), function->pathIndex,
				 instance(function->instantiation)}
			);
		auto encoded = encodeInferenceValue(item, identities);
		requireCompilerInvariant(encoded.has_value(), "inferred body has an incomplete compile-time value");
		return std::move(*encoded);
	}
	void addTypeClasses(const DataType &type) {
		if (type.classDefinition && type.classInstIndex >= 0)
			addClass(type.classDefinition);
		if (type.arrayElementType)
			addTypeClasses(*type.arrayElementType);
		if (type.functionSignature) {
			addTypeClasses(type.functionSignature->result);
			for (const auto &parameter : type.functionSignature->parameters)
				addTypeClasses(parameter);
		}
	}
	void addConstraintClasses(const TypeConstraint &constraint) {
		if (constraint.classDefinition && constraint.classInstantiationIndex && *constraint.classInstantiationIndex >= 0)
			addClass(constraint.classDefinition);
		if (constraint.elementConstraint)
			addConstraintClasses(*constraint.elementConstraint);
		if (constraint.functionSignature) {
			addTypeClasses(constraint.functionSignature->result);
			for (const auto &parameter : constraint.functionSignature->parameters)
				addTypeClasses(parameter);
		}
	}
	void addClass(const ClassDefinition *definition) {
		const auto &path = identities.classes.at(definition);
		if (classRecords.contains(path))
			return;
		const Json &state = classSnapshot.instances(definition);
		classRecords[path] = classSnapshot.reference(definition);
		dependencies.emplace(path, "*");
		for (const auto &instance : definition->instantiations)
			for (const auto &field : instance.fieldTypes)
				addTypeClasses(field);
		if (!state.at("requests").is_null())
			for (const auto &[key, request] : definition->instantiationRequests) {
				for (const auto &[name, type, constant] : request->parameters) {
					addTypeClasses(type);
					(void)value(constant);
				}
				if (request->constructionArgumentTypes)
					for (const DataType &type : *request->constructionArgumentTypes)
						addTypeClasses(type);
			}
	}
	Json instance(const Instantiation *value) {
		if (!value)
			return nullptr;
		instantiationReferences++;
		auto [found, inserted] = instantiationIndices.try_emplace(value, instantiationRecords.size());
		size_t index = found->second;
		if (!inserted)
			return index;
		instantiationRecords.push_back(nullptr);
		const auto *origin = storage.instantiationOrigin(value->storageScope.get());
		requireCompilerInvariant(origin, "inferred body instance has no logical storage origin");
		const auto &[section, key] = *origin;
		Json arguments = Json::array(), constants = Json::array();
		for (const auto &argument : key.argumentTypes)
			arguments.push_back(type(argument));
		for (const auto &[name, constant] : key.compileTimeParameters)
			constants.push_back(Json::array({name, this->value(constant)}));
		auto identity = inferenceSummaryKey(key, identities);
		dependencies.emplace(identities.paths.at(section), identity.value_or("*"));
		instantiationRecords[index] = Json::array({sources.section(section), std::move(arguments), std::move(constants)});
		return index;
	}
	Json diagnostic(const std::shared_ptr<const Diagnostic> &value) {
		if (!value)
			return nullptr;
		Json notes = Json::array(), fixes = Json::array();
		for (const auto &note : value->relatedInfo)
			notes.push_back(
				Json::array({cachedDiagnosticText(note.message, note.configuredMessage), sources.range(note.range)})
			);
		for (const auto &fix : value->quickFixes)
			fixes.push_back(Json::array({fix.title, sources.range(fix.range), fix.replacement}));
		return Json::array(
			{static_cast<int>(value->level), cachedDiagnosticText(value->message, value->configuredMessage),
			 sources.range(value->range), std::move(notes), std::move(fixes)}
		);
	}
	Json match(const PatternMatch *item) {
		if (!item)
			return nullptr;
		Json definitions = Json::array(), nodes = Json::array(), variables = Json::array(), literals = Json::array(),
			 submatches = Json::array(), arguments = Json::array();
		for (auto *value : item->matchingDefinitions)
			definitions.push_back(sources.definition(value));
		for (auto *value : item->nodesPassed)
			nodes.push_back(sources.node(value));
		for (const auto &value : item->discoveredVariables)
			variables.push_back(
				Json::array({value.name, value.lineStartPos, value.lineEndPos, variable(value.variableReference)})
			);
		for (const auto &value : item->acceptedLiterals)
			literals.push_back(sources.node(value.node));
		for (const auto &value : item->subMatches)
			submatches.push_back(match(&value));
		for (const auto &value : item->orderedArguments)
			arguments.push_back(
				Json::array(
					{value.argumentIndex, static_cast<int>(value.kind), expression(value.expression), value.itemIndex,
					 value.isPackElement}
				)
			);
		return Json::array(
			{sources.node(item->matchedEndNode), std::move(definitions), item->lineStartPos, item->lineEndPos, std::move(nodes),
			 std::move(variables), std::move(literals), std::move(submatches), std::move(arguments)}
		);
	}
	Json expressionRecord(const Expression &item) {
		Json arguments = Json::array();
		for (auto *argument : item.arguments)
			arguments.push_back(expression(argument));
		Json literal = std::visit([&](const auto &literal) {
			return value(CompileTimeValue{literal});
		}, item.literalValue);
		Json data = nullptr;
		if (item.dataSectionValues) {
			Json fields = Json::object();
			for (const auto &[name, field] : item.dataSectionValues->fields)
				fields[name] = expression(field);
			data = Json::array({std::move(fields), expression(item.dataSectionValues->record)});
		}
		const auto &outcome = item.sectionOutcome.read();
		Json loops = nullptr;
		if (item.staticLoopBodies) {
			loops = Json::array();
			for (const auto &iteration : *item.staticLoopBodies)
				loops.push_back(body(iteration.get()));
		}
		Json reference = nullptr;
		if (item.patternReference) {
			const auto &source = *item.patternReference;
			Json frames = Json::array(), elements = Json::array();
			for (const auto &frame : source.pattern.keyframes)
				frames.push_back(Json::array({frame.patternPos, frame.linePos}));
			for (const auto &element : source.patternElements)
				elements.push_back(
					Json::array({static_cast<int>(element.type), element.text, element.startPos, element.isArgumentPack})
				);
			reference = Json::array(
				{static_cast<int>(source.patternType), static_cast<int>(source.purpose), sources.range(source.sourceRange),
				 source.pattern.text, std::move(frames), std::move(elements), expression(source.expression),
				 sources.section(source.matchingScope), source.resolved, match(source.match),
				 source.variableResolutionOffsets
					 ? Json::array({source.variableResolutionOffsets->first, source.variableResolutionOffsets->second})
					 : Json(nullptr)}
			);
		}
		using Field = InferenceBodyExpressionField;
		Json record = Json::array();
		record.get_ref<Json::array_t &>().resize(static_cast<size_t>(Field::Count));
		auto field = [&](Field name, Json value) {
			record.at(static_cast<size_t>(name)) = std::move(value);
		};
		field(Field::Reference, std::move(reference));
		field(Field::Kind, static_cast<int>(item.kind));
		field(Field::Type, type(item.type));
		field(Field::Value, value(readValue ? readValue(&item) : item.compileTimeValue));
		field(
			Field::Memory,
			Json::array(
				{diagnostic(item.memoryFacts.value), diagnostic(item.memoryFacts.address), diagnostic(item.memoryFacts.effects)}
			)
		);
		field(Field::Range, sources.range(item.range));
		field(Field::Literal, std::move(literal));
		field(Field::Variable, variable(item.variable));
		field(Field::Declaration, expression(item.dataDeclaration));
		field(Field::Data, std::move(data));
		field(Field::DataValue, expression(item.inferredDataValue));
		field(Field::Deferred, sources.section(item.deferredFunction));
		field(Field::Owner, body(item.owningSectionBody));
		field(Field::Match, match(item.patternMatch));
		field(Field::Definition, sources.definition(item.selectedPatternDefinition));
		field(Field::Path, optional(item.selectedPatternPathIndex));
		field(Field::Callable, sources.definition(item.selectedCallableDefinition));
		field(Field::CallablePath, optional(item.selectedCallablePathIndex));
		field(Field::Subject, expression(item.subjectSetter));
		field(Field::Capture, item.inferredArgumentReference ? captureValue(*item.inferredArgumentReference) : Json(nullptr));
		field(Field::Instance, instance(item.selectedInstantiation));
		field(Field::Expansion, expression(item.inferredFlexExpansion));
		field(Field::Pointer, expression(item.inferredPointerStorage));
		field(Field::Flex, body(item.inferredFlexBody.get()));
		field(Field::Caller, body(item.inferredCallerBody.get()));
		field(Field::Loops, std::move(loops));
		field(
			Field::Outcome,
			Json::array({static_cast<int>(outcome.kind), value(outcome.conditionValue), type(outcome.conditionType)})
		);
		field(Field::FallsThrough, optional(item.executionFallsThrough));
		field(Field::Reachable, item.sectionBodyReachable);
		field(Field::Execution, static_cast<int>(item.sectionBodyExecution));
		field(Field::BodyFallsThrough, item.sectionBodyFallsThrough);
		field(
			Field::Branch, item.branchSelection
							   ? Json::array({item.branchSelection->known, item.branchSelection->selectedBranchIndex})
							   : Json(nullptr)
		);
		field(Field::Template, expression(item.reusableTemplateExpression));
		field(Field::Intrinsic, item.intrinsicName);
		field(Field::Arguments, std::move(arguments));
		field(Field::Submatch, item.isSubMatch);
		field(Field::Grouped, item.isExplicitGroup);
		field(Field::GroupIndices, item.groupingArgumentIndices);
		field(Field::Adjacent, item.groupingArgumentHasAdjacentSiblingSlot);
		field(Field::StartsArgument, item.groupingStartsWithArgument);
		field(Field::EndsArgument, item.groupingEndsWithArgument);
		return record;
	}
	template <class T> static Json optional(const std::optional<T> &value) { return value ? Json(*value) : Json(nullptr); }
	const InferenceValueIdentities &identities;
	InferenceBodyReferenceWriter sources;
	InferenceStorageIdentities &storage;
	InferenceClassSnapshot &classSnapshot;
	std::function<CompileTimeValue(const Expression *)> readValue;
	size_t valueReferences = 0;
	size_t instantiationReferences = 0;
	double captureElapsed = 0;
	Json valueRecords = Json::array();
	Json instantiationRecords = Json::array();
	std::unordered_map<const Instantiation *, size_t> instantiationIndices;
	std::map<Json, size_t> valueIndices;
	std::map<std::pair<size_t, std::uint64_t>, size_t> scalarValues;
	std::unordered_map<std::string, size_t> stringValues, captureValues;
	std::map<TypeReferenceValue, size_t> typeValues;
	std::map<TypeConstraint, size_t> constraintValues;
	std::unordered_map<const Expression *, Json> expressionReferences;
	std::unordered_map<const InstantiatedSectionBody *, size_t> bodyIndices;
	std::unordered_map<const VariableReference *, size_t> variableIndices;
	std::unordered_map<const VariableStorageScope *, size_t> scopeIndices;
	std::vector<const Expression *> expressions;
	std::vector<const InstantiatedSectionBody *> bodies;
	std::vector<const VariableReference *> variables;
	std::map<DataType, size_t> typeIndices;
	std::map<const MinimumSignedIntegerMagnitudeIdentity *, size_t> minima;
	SymbolicInferenceUnits dependencies;
	Json expressionRecords = Json::array(), bodyRecords = Json::array(), variableRecords = Json::array(), types = Json::array();
	Json classRecords = Json::object();
	Json scopes = Json::array();
};
