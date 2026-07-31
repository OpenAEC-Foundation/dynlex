// Reports inferred calls using the compiler's source and definition ranges.
Json DynLexServer::onCallExpressions(const TextDocumentIdentifier &params) {
	if (isConfigDocumentUri(params.uri))
		return Json::array();

	struct ResolvedCall {
		Expression *expression;
		PatternDefinition *definition;
	};
	std::vector<ResolvedCall> calls;
	std::unordered_set<Expression *> visitedExpressions;
	for (ParseContext *context : findContextsFor(params.uri)) {
		if (!hasCompilationStage(context, ParseContext::CompilationStage::InferredTypes))
			continue;

		std::vector<Section *> sections{context->mainSection};
		while (!sections.empty()) {
			Section *section = sections.back();
			sections.pop_back();
			if (!section)
				continue;
			for (Section *child : section->children)
				sections.push_back(child);

			std::function<void(Expression *)> collectExpression = [&](Expression *expression) {
				if (!expression || !visitedExpressions.insert(expression).second)
					return;
				if (expression->kind == Expression::Kind::PatternCall) {
					SourceLocation start = expression->range.sourceStart();
					requireCompilerInvariant(start.sourceFile, "resolved source call has no source location");
					if (pathutil::toAbsoluteUri(start.sourceFile->uri) == params.uri) {
						PatternDefinition *definition = matchedPatternDefinitionForHover(expression);
						requireCompilerInvariant(
							definition && definition->range.line && definition->range.line->sourceFile,
							"resolved source call has no definition location"
						);
						calls.push_back({expression, definition});
					}
				}
				for (Expression *argument : expression->arguments)
					collectExpression(argument);
			};
			for (CodeLine *line : section->codeLines) {
				if (line)
					collectExpression(line->expression);
			}
		}
	}

	std::ranges::sort(calls, [](const ResolvedCall &left, const ResolvedCall &right) {
		SourceLocation leftStart = left.expression->range.sourceStart();
		SourceLocation rightStart = right.expression->range.sourceStart();
		if (leftStart.sourceFileLineIndex != rightStart.sourceFileLineIndex)
			return leftStart.sourceFileLineIndex < rightStart.sourceFileLineIndex;
		if (leftStart.column != rightStart.column)
			return leftStart.column < rightStart.column;
		SourceLocation leftEnd = left.expression->range.sourceEnd();
		SourceLocation rightEnd = right.expression->range.sourceEnd();
		if (leftEnd.sourceFileLineIndex != rightEnd.sourceFileLineIndex)
			return leftEnd.sourceFileLineIndex < rightEnd.sourceFileLineIndex;
		return leftEnd.column < rightEnd.column;
	});

	Json entries = Json::array();
	for (const ResolvedCall &call : calls) {
		std::string returnType = typeToUserName(call.expression->type);
		requireCompilerInvariant(!returnType.empty(), "resolved source call has no inferred return type");
		Location definitionLocation;
		definitionLocation.uri = pathutil::toAbsoluteUri(call.definition->range.line->sourceFile->uri);
		definitionLocation.range = convertRange(call.definition->range);
		entries.push_back({
			{"range", convertRange(call.expression->range)},
			{"definition", definitionLocation},
			{"returnType", returnType},
		});
	}
	return entries;
}
