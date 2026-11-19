static void storeInstantiationSelectionForSection(
	std::unordered_map<std::string, std::string> &selectedInstantiationBySelectionKey, Section *section,
	const std::string &instantiationKey
) {
	if (!section || instantiationKey.empty())
		return;
	for (PatternDefinition *definition : section->patternDefinitions) {
		if (!definition)
			continue;
		std::string key = makeSelectionKey(definition->range);
		if (!key.empty())
			selectedInstantiationBySelectionKey[key] = instantiationKey;
	}
	for (const auto &[_, refs] : section->variableReferences) {
		for (VariableReference *reference : refs) {
			if (!reference)
				continue;
			std::string key = makeSelectionKey(reference->range);
			if (!key.empty())
				selectedInstantiationBySelectionKey[key] = instantiationKey;
		}
	}
}

static bool rangeContainsSource(const ::Range &range, const std::string &uri, int line, int character) {
	if (!range.line)
		return false;
	SourceLocation start = range.sourceStart();
	SourceLocation end = range.sourceEnd();
	if (!start.sourceFile || !end.sourceFile)
		return false;
	if (pathutil::toAbsoluteUri(start.sourceFile->uri) != uri || start.sourceFileLineIndex != line ||
		end.sourceFileLineIndex != line) {
		return false;
	}
	return character >= start.column && character < end.column;
}

static bool rangeContainsSourceForHover(const ::Range &range, const std::string &uri, int line, int character) {
	if (rangeContainsSource(range, uri, line, character))
		return true;
	if (character > 0 && rangeContainsSource(range, uri, line, character - 1))
		return true;
	return false;
}

static VariableReference *findVariableReferenceAt(Section *fromSection, const std::string &uri, int line, int character) {
	for (Section *section = fromSection; section; section = section->parent) {
		for (const auto &[_, refs] : section->variableReferences) {
			for (VariableReference *reference : refs) {
				if (!reference)
					continue;
				if (rangeContainsSource(reference->range, uri, line, character))
					return reference;
			}
		}
	}
	return nullptr;
}

static VariableReference *findVariableReferenceInDocument(
	Section *rootSection, const std::string &uri, int line, int character, Section **outReferenceSection = nullptr
) {
	if (outReferenceSection)
		*outReferenceSection = nullptr;
	if (!rootSection)
		return nullptr;

	std::vector<Section *> stack{rootSection};
	while (!stack.empty()) {
		Section *section = stack.back();
		stack.pop_back();
		if (!section)
			continue;

		for (Section *child : section->children) {
			if (child)
				stack.push_back(child);
		}

		for (const auto &[_, refs] : section->variableReferences) {
			for (VariableReference *reference : refs) {
				if (!reference)
					continue;
				if (!rangeContainsSource(reference->range, uri, line, character))
					continue;
				if (outReferenceSection)
					*outReferenceSection = section;
				return reference;
			}
		}
	}
	return nullptr;
}

static std::string formatCompileTimeValue(const CompileTimeValue &value) {
	if (const auto *resource = std::get_if<ShaderResourceValue>(&value))
		return "shader binding(" + resource->identity() + ")";
	if (const auto *integer = std::get_if<WideInteger>(&value))
		return integer->toString();
	if (const auto *integer = std::get_if<std::uint64_t>(&value))
		return std::to_string(*integer);
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return std::to_string(*integer);
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return "9223372036854775808";
	if (const auto *number = std::get_if<double>(&value)) {
		if (std::isfinite(*number)) {
			double rounded = std::round(*number);
			if (std::abs(*number - rounded) < 1e-9) {
				std::ostringstream asInteger;
				asInteger << static_cast<long long>(rounded);
				return asInteger.str();
			}
		}
		std::ostringstream asFloat;
		asFloat << *number;
		return asFloat.str();
	}
	if (const auto *text = std::get_if<std::string>(&value))
		return quotedSourceString(*text);
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "true" : "false";
	if (const auto *typeRef = std::get_if<TypeReferenceValue>(&value)) {
		return typeRef->type.kind == DataType::Kind::Type ? typeToUserName(typeRef->type.toReferencedType())
														  : typeToUserName(typeRef->type);
	}
	if (const auto *constraint = std::get_if<TypeConstraint>(&value))
		return typeToUserName(*constraint);
	if (const auto *pointer = std::get_if<CompileTimePointer>(&value))
		return "pointer(" + std::to_string(pointer->address) + ")";
	return "?";
}

static std::string markdownCodeSpan(std::string_view text) {
	size_t longest = 0;
	size_t run = 0;
	for (char character : text) {
		run = character == '`' ? run + 1 : 0;
		longest = std::max(longest, run);
	}
	std::string delimiter(longest + 1, '`');
	bool pad = !text.empty() && (text.front() == '`' || text.back() == '`');
	return delimiter + (pad ? " " : "") + std::string(text) + (pad ? " " : "") + delimiter;
}

static Json makeVariableHoverContents(const std::string &typeText, const std::optional<CompileTimeValue> &value) {
	std::ostringstream markdown;
	if (!typeText.empty()) {
		markdown << "type:\n\n```dynlex\n" << typeText << "\n```";
	}
	bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
	if (hasKnownValue) {
		if (!typeText.empty())
			markdown << "\n\n";
		markdown << "value: " << markdownCodeSpan(formatCompileTimeValue(*value));
	}
	return Json{{"kind", "markdown"}, {"value", markdown.str()}};
}

static const Instantiation *findSelectedInstantiationForSection(
	Section *ownerSection, const std::string &selectionKey,
	const std::unordered_map<std::string, std::string> &selectedInstantiationBySelectionKey
) {
	if (!ownerSection || ownerSection->instantiations.empty())
		return nullptr;
	auto findBySignature = [&](const std::string &signature) -> const Instantiation * {
		if (signature.empty())
			return nullptr;
		for (const auto &[instantiationKey, instantiation] : ownerSection->instantiations) {
			if (makeInstantiationSignature(instantiationKey) == signature)
				return &instantiation;
		}
		return nullptr;
	};
	auto selectFromSelectionKey = [&](const std::string &key) -> const Instantiation * {
		if (key.empty())
			return nullptr;
		auto it = selectedInstantiationBySelectionKey.find(key);
		if (it == selectedInstantiationBySelectionKey.end())
			return nullptr;
		return findBySignature(it->second);
	};
	if (const Instantiation *selected = selectFromSelectionKey(selectionKey))
		return selected;
	for (PatternDefinition *definition : ownerSection->patternDefinitions) {
		if (!definition)
			continue;
		if (const Instantiation *selected = selectFromSelectionKey(makeSelectionKey(definition->range)))
			return selected;
	}
	for (const auto &[_, refs] : ownerSection->variableReferences) {
		for (VariableReference *reference : refs) {
			if (!reference)
				continue;
			if (const Instantiation *selected = selectFromSelectionKey(makeSelectionKey(reference->range)))
				return selected;
		}
	}
	return &ownerSection->instantiations.begin()->second;
}

static const Expression *lookupExpressionForHover(
	const Expression *expr, Section *ownerSection, const std::string &selectionKey,
	const std::unordered_map<std::string, std::string> &selectedInstantiationBySelectionKey
) {
	if (!expr)
		return nullptr;
	Section *instantiatedOwnerSection = sourceInstantiationOwner(ownerSection);
	const Instantiation *selectedInstantiation =
		findSelectedInstantiationForSection(instantiatedOwnerSection, selectionKey, selectedInstantiationBySelectionKey);
	return inferredSourceExpression(expr, selectedInstantiation);
}

static std::optional<DataType> lookupReferenceTypeInBody(const InstantiatedSectionBody &body, VariableReference *reference) {
	if (!reference)
		return std::nullopt;
	for (Expression *root : body.lineExpressions) {
		std::optional<DataType> type;
		visitExpressionTree(root, [&](Expression *expression) {
			if (expression->kind != Expression::Kind::Variable || expression->variable != reference)
				return false;
			type = expression->type;
			return true;
		});
		if (type)
			return type;
	}
	for (const auto &child : body.childBodies) {
		if (std::optional<DataType> type = lookupReferenceTypeInBody(*child, reference))
			return type;
	}
	return std::nullopt;
}

static DataType lookupVariableHoverType(
	const Expression *sourceExpression, Section *sourceScope, Section *ownerSection, VariableReference *reference,
	VariableReference *definition, const std::string &name, const std::string &selectionKey,
	const std::unordered_map<std::string, std::string> &selections, const DataType &templateType
) {
	if (sourceExpression) {
		const Expression *expression =
			lookupExpressionForHover(sourceExpression, sourceScope, makeSelectionKey(sourceExpression->range), selections);
		return expression ? expression->type : DataType{};
	}
	const Instantiation *instance =
		findSelectedInstantiationForSection(sourceInstantiationOwner(ownerSection), selectionKey, selections);
	if (!instance)
		return templateType;
	if (instance->body) {
		if (std::optional<DataType> type = lookupReferenceTypeInBody(*instance->body, reference))
			return *type;
		if (std::optional<DataType> type = lookupReferenceTypeInBody(*instance->body, definition))
			return *type;
	}
	// Parameters are declared in the signature, outside the executable body.
	auto parameter = instance->parameterTypesByName.find(name);
	return parameter != instance->parameterTypesByName.end() ? parameter->second : DataType{};
}

static std::optional<CompileTimeValue> lookupHoverConstantValue(
	const Expression *sourceExpression, Section *sourceScope, Section *ownerSection, VariableReference *definition,
	const std::string &variableName, const std::string &selectionKey,
	const std::unordered_map<std::string, std::string> &selections
) {
	if (sourceExpression) {
		const Expression *expression =
			lookupExpressionForHover(sourceExpression, sourceScope, makeSelectionKey(sourceExpression->range), selections);
		return expression ? std::optional<CompileTimeValue>(getExpressionCompileTimeValue(expression)) : std::nullopt;
	}
	// Signature parameters have no executable expression at their declaration.
	const Instantiation *instance =
		findSelectedInstantiationForSection(sourceInstantiationOwner(ownerSection), selectionKey, selections);
	if (!instance)
		return std::nullopt;
	auto parameter = instance->constantParameterValues.find(variableName);
	if (parameter == instance->constantParameterValues.end() && definition && definition->name != variableName)
		parameter = instance->constantParameterValues.find(definition->name);
	return parameter != instance->constantParameterValues.end() ? std::optional<CompileTimeValue>(parameter->second)
																: std::nullopt;
}

static PatternDefinition *matchedPatternDefinitionForHover(const Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;
	if (expr->selectedPatternDefinition)
		return expr->selectedPatternDefinition;
	Section *ownerSection = expr->range.line ? expr->range.line->section : nullptr;
	PatternDefinition *selectedDefinition = nullptr;
	if (ownerSection) {
		for (const auto &[key, instantiation] : ownerSection->instantiations) {
			(void)key;
			Expression *activeExpression = instantiation.body ? instantiation.body->findCloneOf(expr) : nullptr;
			if (!activeExpression || !activeExpression->selectedPatternDefinition)
				continue;
			if (selectedDefinition && selectedDefinition != activeExpression->selectedPatternDefinition)
				return nullptr;
			selectedDefinition = activeExpression->selectedPatternDefinition;
		}
	}
	if (selectedDefinition)
		return selectedDefinition;
	const std::vector<PatternDefinition *> &definitions = expr->patternMatch->matchingDefinitions;
	return definitions.size() == 1 ? definitions.front() : nullptr;
}

struct CursorResolution {
	CodeLine *codeLine{};
	int localOffset = -1;
	Expression *expr{};
	PatternDefinition *matchedPattern{};
	VariableReference *referenceAtCursor{};
	VariableReference *definitionAtCursor{};
	std::string variableName;
};

static std::optional<CursorResolution>
resolveCursorData(ParseContext &context, const std::string &uri, int line, int character) {
	for (CodeLine *codeLine : context.codeLines) {
		if (!codeLine || !codeLine->containsSourceLocation(uri, line, character) || !codeLine->expression)
			continue;
		int localOffset = codeLine->mapSourceToOffset(uri, line, character);
		if (localOffset < 0)
			continue;

		Expression *root = codeLine->expression;
		Expression *expr = findDeepestExpression(root, localOffset);
		if (Expression *sourceVariable = findVariableArgumentAtOffset(root, localOffset))
			expr = sourceVariable;
		else if (Expression *sourceVariable = sourceVariableExpressionAt(root, codeLine->mapOffsetToSource(localOffset, true)))
			expr = sourceVariable;

		CursorResolution resolved;
		resolved.codeLine = codeLine;
		resolved.localOffset = localOffset;
		resolved.expr = expr;
		resolved.matchedPattern = matchedPatternDefinitionForHover(expr);
		if (expr && expr->kind == Expression::Kind::Variable && expr->variable) {
			resolved.referenceAtCursor = expr->variable;
			resolved.definitionAtCursor = expr->variable->definition ? expr->variable->definition : expr->variable;
			resolved.variableName = expr->variable->name;
		} else {
			VariableReference *reference = findVariableReferenceAt(codeLine->section, uri, line, character);
			if (reference) {
				resolved.referenceAtCursor = reference;
				resolved.definitionAtCursor = reference->definition ? reference->definition : reference;
				resolved.variableName = reference->name;
			}
		}
		return resolved;
	}
	return std::nullopt;
}

std::optional<Location> DynLexServer::onDefinition(const TextDocumentPositionParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return std::nullopt;
	const auto byteParams = bytePositionParams(params);
	for (ParseContext *context : findContextsFor(params.textDocument.uri)) {
		if (std::optional<Location> location = definitionInContext(context, byteParams))
			return location;
	}
	return std::nullopt;
}

std::optional<Location> DynLexServer::definitionInContext(ParseContext *context, const TextDocumentPositionParams &params) {
	for (const auto &[path, target] : context->importDefinitions) {
		const int offset = path.line->mapSourceToOffset(params.textDocument.uri, params.position.line, params.position.character);
		if (offset >= path.start() && offset < path.end())
			return Location{pathutil::toAbsoluteUri(target->uri), {}};
	}
	if (!hasCompilationStage(context, ParseContext::CompilationStage::ResolvedPatterns)) {
		return std::nullopt;
	}

	std::optional<CursorResolution> resolved =
		resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character);
	if (!resolved.has_value())
		return std::nullopt;
	if (resolved->expr && resolved->expr->kind == Expression::Kind::PatternCall && resolved->matchedPattern) {
		Section *targetSection = resolved->matchedPattern->section;
		if (targetSection && targetSection->instantiations.size() > 1) {
			std::vector<DataType> callArgTypes = argumentTypesForDefinition(resolved->expr, resolved->matchedPattern);
			std::string key = makeInstantiationSignature(callArgTypes);
			for (const auto &[instantiationKey, ignoredInstantiation] : targetSection->instantiations) {
				(void)ignoredInstantiation;
				if (instantiationKey.argumentTypes != callArgTypes)
					continue;
				storeInstantiationSelectionForSection(
					selectedInstantiationBySelectionKey, targetSection, makeInstantiationSignature(instantiationKey)
				);
				break;
			}
		}
	}
	if (resolved->expr) {
		auto target = getDefinitionTarget(resolved->expr);
		if (target) {
			Location loc;
			loc.uri = pathutil::toAbsoluteUri(target->line->sourceFile->uri);
			loc.range = convertRange(*target);
			return loc;
		}
	}

	return std::nullopt;
}

std::optional<Hover> DynLexServer::onHover(const TextDocumentPositionParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return std::nullopt;
	const auto byteParams = bytePositionParams(params);
	for (ParseContext *context : findContextsFor(params.textDocument.uri)) {
		if (std::optional<Hover> hover = hoverInContext(context, byteParams))
			return hover;
	}
	return std::nullopt;
}

std::optional<Hover> DynLexServer::hoverInContext(ParseContext *context, const TextDocumentPositionParams &params) {
	if (!hasCompilationStage(context, ParseContext::CompilationStage::InferredTypes))
		return std::nullopt;

	std::optional<CursorResolution> resolved =
		resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character);
	if (!resolved.has_value() && params.position.character > 0) {
		resolved = resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character - 1);
	} else if (resolved.has_value() && !resolved->referenceAtCursor && params.position.character > 0) {
		std::optional<CursorResolution> previousCharResolved =
			resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character - 1);
		if (previousCharResolved.has_value() && previousCharResolved->referenceAtCursor)
			resolved = std::move(previousCharResolved);
	}
	const Expression *sourceVariable =
		resolved && resolved->expr && resolved->expr->kind == Expression::Kind::Variable ? resolved->expr : nullptr;
	Section *sourceScope = sourceVariable ? resolved->codeLine->section : nullptr;

	for (const ParseContext::SourceTokenAnnotation &annotation : context->sourceTokenAnnotations) {
		if (annotation.kind != ParseContext::SourceTokenKind::Variable)
			continue;
		if (!rangeContainsSourceForHover(
				annotation.range, params.textDocument.uri, params.position.line, params.position.character
			))
			continue;
		if (!annotation.range.line || !annotation.range.line->section)
			continue;
		const std::string variableName = std::string(annotation.range.subString);
		Variable *resolvedVariable = annotation.range.line->section->findVariable(variableName);
		if (!resolvedVariable)
			continue;
		Section *ownerSection = (resolvedVariable->definition && resolvedVariable->definition->range.line)
									? resolvedVariable->definition->range.line->section
									: annotation.range.line->section;
		if (!ownerSection)
			continue;
		Variable *ownedVariable = resolvedVariable;
		if (ownedVariable->definition) {
			if (Section *instantiatedOwner =
					findInstantiatedOwnerSectionForDefinition(*context, ownedVariable->definition, variableName)) {
				ownerSection = instantiatedOwner;
			}
		}
		ownerSection = findBestSectionForVariableLookup(*context, ownerSection, ownedVariable->definition, variableName);
		std::string selectionKey = makeSelectionKey(annotation.range);
		VariableReference *reference = findVariableReferenceAt(
			annotation.range.line->section, params.textDocument.uri, params.position.line, params.position.character
		);
		std::optional<CompileTimeValue> value = lookupHoverConstantValue(
			sourceVariable, sourceScope, ownerSection, ownedVariable->definition, variableName, selectionKey,
			selectedInstantiationBySelectionKey
		);
		bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
		std::string typeText = typeToUserName(lookupVariableHoverType(
			sourceVariable, sourceScope, ownerSection, reference, ownedVariable->definition, variableName, selectionKey,
			selectedInstantiationBySelectionKey, ownedVariable->type
		));
		if (typeText.empty() && !hasKnownValue)
			continue;

		Hover hover;
		hover.contents = makeVariableHoverContents(typeText, value);
		hover.range = convertRange(annotation.range);
		return hover;
	}

	for (const ParseContext::SourceTokenAnnotation &annotation : context->sourceTokenAnnotations) {
		if (annotation.kind != ParseContext::SourceTokenKind::PatternReference)
			continue;
		if (!rangeContainsSourceForHover(
				annotation.range, params.textDocument.uri, params.position.line, params.position.character
			))
			continue;
		PatternDefinition *definition = findDefinitionBySignature(
			*context, annotation.referencedPatternType, annotation.range.subString,
			annotation.range.line ? annotation.range.line->sourceFile : nullptr
		);
		if (!definition)
			return std::nullopt;
		Hover hover;
		hover.contents = definition->toString();
		hover.range = convertRange(annotation.range);
		return hover;
	}

	std::optional<Hover> definitionHover;
	{
		std::vector<Section *> stack{context->mainSection};
		while (!stack.empty()) {
			Section *section = stack.back();
			stack.pop_back();
			if (!section)
				continue;
			for (Section *child : section->children) {
				if (child)
					stack.push_back(child);
			}
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition)
					continue;
				if (!rangeContainsSource(
						definition->range, params.textDocument.uri, params.position.line, params.position.character
					))
					continue;
				Hover hover;
				hover.contents = definition->toString();
				hover.range = convertRange(definition->range);
				definitionHover = std::move(hover);
				stack.clear();
				break;
			}
		}
	}

	if (resolved.has_value()) {
		Expression *expr = resolved->expr;
		PatternDefinition *matchedPattern = resolved->matchedPattern;
		VariableReference *referenceAtHover = resolved->referenceAtCursor;
		VariableReference *variableDefinition = resolved->definitionAtCursor;
		std::string variableName = resolved->variableName;
		if (expr && expr->kind != Expression::Kind::Variable) {
			Section *expressionOwnerSection = resolved->codeLine ? resolved->codeLine->section : nullptr;
			if (!expressionOwnerSection && expr->range.line)
				expressionOwnerSection = expr->range.line->section;
			std::string selectionKey = makeSelectionKey(expr->range);
			const Expression *instanceExpression =
				lookupExpressionForHover(expr, expressionOwnerSection, selectionKey, selectedInstantiationBySelectionKey);
			std::optional<CompileTimeValue> expressionValue =
				instanceExpression ? std::optional<CompileTimeValue>(getExpressionCompileTimeValue(instanceExpression))
								   : std::nullopt;
			bool hasKnownExpressionValue = expressionValue.has_value() && isCompileTimeKnown(*expressionValue);
			if (hasKnownExpressionValue) {
				Hover hover;
				hover.contents = makeVariableHoverContents(typeToUserName(instanceExpression->type), expressionValue);
				hover.range = convertRange(expr->range);
				return hover;
			}
		}
		if (!referenceAtHover || !variableDefinition) {
			if (matchedPattern) {
				Hover hover;
				hover.contents = matchedPattern->toString();
				hover.range = convertRange(expr->range);
				return hover;
			}
		} else {
			Section *ownerSection = nullptr;
			if (variableDefinition && variableDefinition->range.line && variableDefinition->range.line->section)
				ownerSection = variableDefinition->range.line->section;
			if (!ownerSection && referenceAtHover->range.line && referenceAtHover->range.line->section)
				ownerSection =
					findOwningVariableSection(referenceAtHover->range.line->section, variableDefinition, variableName);
			if (!ownerSection) {
				ownerSection = findOwningVariableSectionAtSource(
					*context, params.textDocument.uri, params.position.line, params.position.character, variableDefinition,
					variableName
				);
			}
			if (Section *instantiatedOwner =
					findInstantiatedOwnerSectionForDefinition(*context, variableDefinition, variableName)) {
				ownerSection = instantiatedOwner;
			}
			ownerSection = findBestSectionForVariableLookup(*context, ownerSection, variableDefinition, variableName);
			if (ownerSection) {
				std::string selectionKey = makeSelectionKey(referenceAtHover->range);

				Variable *ownedVariable = nullptr;
				auto variableIt = ownerSection->variables.find(variableName);
				if (variableIt != ownerSection->variables.end())
					ownedVariable = variableIt->second;
				if (!ownedVariable)
					ownedVariable = ownerSection->findVariable(variableName);

				std::string typeText = typeToUserName(lookupVariableHoverType(
					sourceVariable, sourceScope, ownerSection, referenceAtHover, variableDefinition, variableName, selectionKey,
					selectedInstantiationBySelectionKey, ownedVariable ? ownedVariable->type : (expr ? expr->type : DataType{})
				));

				std::optional<CompileTimeValue> value = lookupHoverConstantValue(
					sourceVariable, sourceScope, ownerSection, variableDefinition, variableName, selectionKey,
					selectedInstantiationBySelectionKey
				);
				bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
				if (typeText.empty() && !hasKnownValue)
					return std::nullopt;

				Hover hover;
				hover.contents = makeVariableHoverContents(typeText, value);
				if (expr)
					hover.range = convertRange(expr->range);
				return hover;
			}
			if (matchedPattern) {
				Hover hover;
				hover.contents = matchedPattern->toString();
				hover.range = convertRange(expr->range);
				return hover;
			}
		}
	}

	Section *referenceSection = nullptr;
	VariableReference *referenceAtHover = findVariableReferenceInDocument(
		context->mainSection, params.textDocument.uri, params.position.line, params.position.character, &referenceSection
	);
	if (!referenceAtHover) {
		if (definitionHover.has_value())
			return definitionHover;
		return std::nullopt;
	}

	std::string variableName = referenceAtHover->name;
	VariableReference *variableDefinition = referenceAtHover->definition ? referenceAtHover->definition : referenceAtHover;
	Section *ownerSection = findOwningVariableSection(referenceSection, variableDefinition, variableName);
	ownerSection = findBestSectionForVariableLookup(*context, ownerSection, variableDefinition, variableName);
	if (!ownerSection) {
		if (definitionHover.has_value())
			return definitionHover;
		return std::nullopt;
	}
	std::string selectionKey = makeSelectionKey(referenceAtHover->range);

	Variable *ownedVariable = nullptr;
	auto variableIt = ownerSection->variables.find(variableName);
	if (variableIt != ownerSection->variables.end())
		ownedVariable = variableIt->second;

	std::string typeText = typeToUserName(lookupVariableHoverType(
		sourceVariable, sourceScope, ownerSection, referenceAtHover, variableDefinition, variableName, selectionKey,
		selectedInstantiationBySelectionKey, ownedVariable ? ownedVariable->type : DataType{}
	));

	std::optional<CompileTimeValue> value = lookupHoverConstantValue(
		sourceVariable, sourceScope, ownerSection, variableDefinition, variableName, selectionKey,
		selectedInstantiationBySelectionKey
	);
	bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
	if (typeText.empty() && !hasKnownValue) {
		if (definitionHover.has_value())
			return definitionHover;
		return std::nullopt;
	}

	Hover hover;
	hover.contents = makeVariableHoverContents(typeText, value);
	hover.range = convertRange(referenceAtHover->range);
	return hover;
}

Json DynLexServer::onInstantiationsInDocument(const TextDocumentIdentifier &params) {
	if (isConfigDocumentUri(params.uri))
		return Json::array();

	Json entries = Json::array();
	std::unordered_set<std::string> seenSelectionKeys;
	for (ParseContext *context : findContextsFor(params.uri))
		appendInstantiationsInContext(context, params, entries, seenSelectionKeys);
	return entries;
}

void DynLexServer::appendInstantiationsInContext(
	ParseContext *context, const TextDocumentIdentifier &params, Json &entries,
	std::unordered_set<std::string> &seenSelectionKeys
) {
	if (!hasCompilationStage(context, ParseContext::CompilationStage::InferredTypes))
		return;

	std::vector<Section *> stack{context->mainSection};
	while (!stack.empty()) {
		Section *section = stack.back();
		stack.pop_back();
		if (!section)
			continue;
		for (Section *child : section->children) {
			if (child)
				stack.push_back(child);
		}
		for (const auto &[_, refs] : section->variableReferences) {
			for (VariableReference *referenceAtHover : refs) {
				if (!referenceAtHover || !referenceAtHover->range.line || !referenceAtHover->range.line->sourceFile)
					continue;
				if (pathutil::toAbsoluteUri(referenceAtHover->range.line->sourceFile->uri) != params.uri)
					continue;

				std::string variableName = referenceAtHover->name;
				VariableReference *variableDefinition =
					referenceAtHover->definition ? referenceAtHover->definition : referenceAtHover;
				Section *ownerSection = findOwningVariableSection(section, variableDefinition, variableName);
				if (!ownerSection || ownerSection->instantiations.empty())
					continue;

				std::string selectionKey = makeSelectionKey(referenceAtHover->range);
				if (selectionKey.empty() || seenSelectionKeys.contains(selectionKey))
					continue;
				seenSelectionKeys.insert(selectionKey);

				Json options = buildInstantiationOptions(ownerSection);
				if (options.empty())
					continue;

				std::string currentKey;
				auto selectedIt = selectedInstantiationBySelectionKey.find(selectionKey);
				if (selectedIt != selectedInstantiationBySelectionKey.end())
					currentKey = selectedIt->second;
				if (currentKey.empty())
					currentKey = options[0].at("key").get<std::string>();

				entries.push_back({
					{"selectionKey", selectionKey},
					{"currentKey", currentKey},
					{"range", convertRange(referenceAtHover->range)},
					{"options", options},
				});
			}
		}

		if (!section->instantiations.empty()) {
			Json options = buildInstantiationOptions(section);
			if (!options.empty()) {
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition || !definition->range.line || !definition->range.line->sourceFile)
						continue;
					if (pathutil::toAbsoluteUri(definition->range.line->sourceFile->uri) != params.uri)
						continue;
					std::string selectionKey = makeSelectionKey(definition->range);
					if (selectionKey.empty() || seenSelectionKeys.contains(selectionKey))
						continue;
					seenSelectionKeys.insert(selectionKey);

					std::string currentKey;
					auto selectedIt = selectedInstantiationBySelectionKey.find(selectionKey);
					if (selectedIt != selectedInstantiationBySelectionKey.end())
						currentKey = selectedIt->second;
					if (currentKey.empty())
						currentKey = options[0].at("key").get<std::string>();

					entries.push_back({
						{"selectionKey", selectionKey},
						{"currentKey", currentKey},
						{"range", convertRange(definition->range)},
						{"options", options},
					});
				}
			}
		}
	}
}

void DynLexServer::onSelectInstantiation(const Json &params) {
	if (!params.is_object())
		return;
	if (!params.contains("selectionKey") || !params.contains("instantiationKey"))
		return;
	if (!params.at("selectionKey").is_string() || !params.at("instantiationKey").is_string())
		return;

	std::string selectionKey = params.at("selectionKey").get<std::string>();
	std::string instantiationKey = params.at("instantiationKey").get<std::string>();
	if (selectionKey.empty())
		return;
	if (instantiationKey.empty()) {
		selectedInstantiationBySelectionKey.erase(selectionKey);
		return;
	}
	selectedInstantiationBySelectionKey[selectionKey] = instantiationKey;
}

#include "dynlexServerSymbols.inl"

std::vector<CodeAction> DynLexServer::onCodeAction(const CodeActionParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return {};
	std::vector<CodeAction> actions;
	for (const Diagnostic &diag : params.context.diagnostics) {
		if (!diag.data || !diag.data->contains("quickFixes"))
			continue;
		for (const Json &fix : (*diag.data)["quickFixes"]) {
			if (!fix.contains("title") || !fix.contains("replacement") || !fix.contains("range"))
				continue;
			CodeAction action;
			action.title = fix.at("title").get<std::string>();
			action.kind = "quickfix";
			action.diagnostics.push_back(diag);
			WorkspaceEdit edit;
			TextEdit textEdit;
			textEdit.range = fix.at("range").get<Range>();
			textEdit.newText = fix.at("replacement").get<std::string>();
			std::string targetUri = params.textDocument.uri;
			if (fix.contains("uri") && !fix.at("uri").is_null())
				targetUri = fix.at("uri").get<std::string>();
			edit.changes[targetUri] = Json::array({textEdit});
			action.edit = std::move(edit);
			actions.push_back(std::move(action));
		}
	}
	return actions;
}

SemanticTokens DynLexServer::onSemanticTokensFull(const SemanticTokensParams &params) {
	SemanticTokens result;
	result.data = generateSemanticTokens(params.textDocument.uri);
	return result;
}

std::string DynLexServer::onRenderSemanticTokens(const TextDocumentIdentifier &params) {
	auto docIt = documents.find(params.uri);
	if (docIt == documents.end())
		return {};
	return renderTaggedSemanticTokensFromData(docIt->second->content, generateSemanticTokens(params.uri));
}

std::optional<std::string> DynLexServer::onReadDocument(const TextDocumentIdentifier &params) {
	auto document = documents.find(params.uri);
	if (document != documents.end())
		return document->second->content;

	ParseContext *context = findContextFor(params.uri);
	if (!hasCompilationStage(context, ParseContext::CompilationStage::ImportedFiles))
		return std::nullopt;
	for (const auto &[_, sourceFile] : context->importedFiles) {
		if (sourceFile && pathutil::toAbsoluteUri(sourceFile->uri) == params.uri)
			return sourceFile->content;
	}
	return std::nullopt;
}

std::vector<int> DynLexServer::generateSemanticTokens(const std::string &uri) {
	if (isConfigDocumentUri(uri)) {
		auto docIt = documents.find(uri);
		if (docIt == documents.end())
			return {};
		return encodeConfigSemanticTokens(*docIt->second);
	}
	auto docIt = documents.find(uri);
	if (docIt == documents.end())
		return {};

	std::vector<ParseContext *> contexts = findContextsFor(uri);
	SemanticTokenBuilder mergedTokens(docIt->second->lineCount());
	for (ParseContext *context : contexts) {
		if (!hasCompilationStage(context, ParseContext::CompilationStage::AnalyzedSections))
			continue;
		const auto contextTokens = collectSemanticTokens(*context, uri, docIt->second->lineCount(), false);
		for (size_t lineIndex = 0; lineIndex < contextTokens.size(); ++lineIndex) {
			for (const SemanticToken &token : contextTokens[lineIndex])
				mergedTokens.add(static_cast<int>(lineIndex), token);
		}
	}
	std::vector<std::vector<SemanticToken>> tokensByLine = mergedTokens.tokenLines();
	ParseContext *liveLexingContext = contexts.empty() ? nullptr : contexts.front();

	auto lockedIt = lockedLinesByUri.find(uri);
	if (lockedIt != lockedLinesByUri.end()) {
		for (const auto &[lineIndex, state] : lockedIt->second) {
			if (lineIndex < 0 || lineIndex >= docIt->second->lineCount())
				continue;
			// Keep compiler semantic tokens when the locked line still matches
			// the last compiled baseline; only fall back to live lexing for
			// actively edited (diverged) lines.
			if (std::string(docIt->second->getLine(lineIndex)) == state.committedText)
				continue;
			tokensByLine[lineIndex] = collectLiveLineSemanticTokens(liveLexingContext, *docIt->second, uri, lineIndex);
		}
	}

	return encodeSemanticTokens(tokensByLine, *docIt->second);
}
