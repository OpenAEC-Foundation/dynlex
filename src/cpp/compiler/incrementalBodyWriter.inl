class InferenceBodyWriter {
  public:
	InferenceBodyWriter(
		const InferenceValueIdentities &identities, InferenceBodySources &sources, InferenceStorageIdentities &storage
	)
		: identities(identities), sources(sources), storage(storage) {}
	Json capture(const Instantiation &instance) {
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
		result.update(
			Json{
				{"expressions", std::move(expressionRecords)},
				{"bodies", std::move(bodyRecords)},
				{"variables", std::move(variableRecords)},
				{"types", std::move(types)},
				{"scopes", std::move(scopes)},
				{"sources", sources.take()},
				{"minimum_identities", minima.size()},
				{"dependencies", dependencies},
				{"classes", std::move(classRecords)}
			}
		);
		return result;
	}

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
		if (auto source = storage.encodeExpression(value))
			return Json::array({"source", sources.expressionSource(std::move(*source))});
		return Json::array({"local", intern(value, expressionIndices, expressions)});
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
		addTypeClasses(value);
		if (auto found = typeIndices.find(value); found != typeIndices.end())
			return found->second;
		size_t index = types.size();
		types.push_back(nullptr);
		typeIndices.emplace(value, index);
		Json arguments = Json::array();
		if (value.functionSignature)
			for (const auto &argument : value.functionSignature->parameters)
				arguments.push_back(type(argument));
		Json layout = nullptr;
		if (value.classDefinition && value.classInstIndex >= 0) {
			DataType concrete{DataType::Kind::Class, 0, 1, value.classDefinition, value.classInstIndex};
			std::set<std::pair<const ClassDefinition *, int>> visiting;
			auto identity = inferenceTypeIdentity(concrete, identities.classes, visiting);
			requireCompilerInvariant(identity.has_value(), "inferred body class has no committed layout");
			layout = std::move(*identity);
		}
		Json record = Json::array(
			{static_cast<int>(value.kind), value.numericSize, value.pointerDepth, static_cast<int>(value.referencedKind),
			 value.arraySize, value.matrixRowCount, value.arrayElementType ? type(*value.arrayElementType) : Json(nullptr),
			 value.classDefinition ? sources.section(identities.sections.at(identities.classes.at(value.classDefinition)))
								   : Json(nullptr),
			 value.classInstIndex,
			 value.functionSignature ? Json::array({type(value.functionSignature->result), std::move(arguments)})
									 : Json(nullptr),
			 expression(value.typeExpression), std::move(layout)}
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
	Json value(const CompileTimeValue &item) {
		if (const auto *reference = std::get_if<TypeReferenceValue>(&item)) {
			addTypeClasses(reference->type);
			addConstraintClasses(reference->constraint);
		}
		if (const auto *constraint = std::get_if<TypeConstraint>(&item))
			addConstraintClasses(*constraint);
		if (std::holds_alternative<std::monostate>(item))
			return nullptr;
		if (const auto *minimum = std::get_if<MinimumSignedIntegerMagnitude>(&item)) {
			auto [found, _] = minima.try_emplace(minimum->identity.get(), minima.size());
			return Json::array({"minimum", found->second});
		}
		if (const auto *capture = std::get_if<ExpressionCapture>(&item))
			return Json::array({"capture", captureValue(*capture)});
		if (const auto *pack = std::get_if<ArgumentPackValue>(&item)) {
			Json values = Json::array();
			for (const auto &capture : pack->elements)
				values.push_back(captureValue(capture));
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
		classRecords[path] = sources.classInstances(definition);
		dependencies.emplace(path, "*");
		for (const auto &instance : definition->instantiations)
			for (const auto &field : instance.fieldTypes)
				addTypeClasses(field);
		if (!classRecords.at(path).at("requests").is_null())
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
		const auto &[section, key] = sources.instance(value);
		Json arguments = Json::array(), constants = Json::array();
		for (const auto &argument : key->argumentTypes)
			arguments.push_back(type(argument));
		for (const auto &[name, constant] : key->compileTimeParameters)
			constants.push_back(Json::array({name, this->value(constant)}));
		auto identity = inferenceSummaryKey(*key, identities);
		dependencies.emplace(identities.paths.at(section), identity.value_or("*"));
		return Json::array({sources.section(section), std::move(arguments), std::move(constants)});
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
		return Json{
			{"reference", std::move(reference)},
			{"kind", static_cast<int>(item.kind)},
			{"type", type(item.type)},
			{"value", value(item.compileTimeValue)},
			{"memory", Json::array(
						   {diagnostic(item.memoryFacts.value), diagnostic(item.memoryFacts.address),
							diagnostic(item.memoryFacts.effects)}
					   )},
			{"range", sources.range(item.range)},
			{"literal", std::move(literal)},
			{"variable", variable(item.variable)},
			{"declaration", expression(item.dataDeclaration)},
			{"data", std::move(data)},
			{"data_value", expression(item.inferredDataValue)},
			{"deferred", sources.section(item.deferredFunction)},
			{"owner", body(item.owningSectionBody)},
			{"match", match(item.patternMatch)},
			{"definition", sources.definition(item.selectedPatternDefinition)},
			{"path", optional(item.selectedPatternPathIndex)},
			{"callable", sources.definition(item.selectedCallableDefinition)},
			{"callable_path", optional(item.selectedCallablePathIndex)},
			{"subject", expression(item.subjectSetter)},
			{"capture", item.inferredArgumentReference ? captureValue(*item.inferredArgumentReference) : Json(nullptr)},
			{"instance", instance(item.selectedInstantiation)},
			{"expansion", expression(item.inferredFlexExpansion)},
			{"pointer", expression(item.inferredPointerStorage)},
			{"flex", body(item.inferredFlexBody.get())},
			{"caller", body(item.inferredCallerBody.get())},
			{"loops", std::move(loops)},
			{"outcome",
			 Json::array({static_cast<int>(outcome.kind), value(outcome.conditionValue), type(outcome.conditionType)})},
			{"falls_through", optional(item.executionFallsThrough)},
			{"reachable", item.sectionBodyReachable},
			{"execution", static_cast<int>(item.sectionBodyExecution)},
			{"body_falls_through", item.sectionBodyFallsThrough},
			{"branch", item.branchSelection
						   ? Json::array({item.branchSelection->known, item.branchSelection->selectedBranchIndex})
						   : Json(nullptr)},
			{"template", expression(item.reusableTemplateExpression)},
			{"intrinsic", item.intrinsicName},
			{"arguments", std::move(arguments)},
			{"submatch", item.isSubMatch},
			{"grouped", item.isExplicitGroup},
			{"group_indices", item.groupingArgumentIndices},
			{"adjacent", item.groupingArgumentHasAdjacentSiblingSlot},
			{"starts_argument", item.groupingStartsWithArgument},
			{"ends_argument", item.groupingEndsWithArgument}
		};
	}
	template <class T> static Json optional(const std::optional<T> &value) { return value ? Json(*value) : Json(nullptr); }
	const InferenceValueIdentities &identities;
	InferenceBodyReferenceWriter sources;
	InferenceStorageIdentities &storage;
	std::unordered_map<const Expression *, size_t> expressionIndices;
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
