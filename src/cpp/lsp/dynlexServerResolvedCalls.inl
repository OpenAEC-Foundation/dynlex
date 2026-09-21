// Reports calls from completed inference, including call-site-specific reusable bodies.
Json DynLexServer::onCallExpressions(const TextDocumentIdentifier &params) {
	if (isConfigDocumentUri(params.uri))
		return Json::array();

	std::vector<Json> entries;
	std::unordered_set<Expression *> visitedExpressions;
	std::unordered_set<const InstantiatedSectionBody *> visitedBodies;
	std::stack<const InstantiatedSectionBody *> bodies;
	std::function<void(Expression *)> collectExpression = [&](Expression *current) {
		if (!visitedExpressions.insert(current).second)
			return;
		if (current->inferredFlexBody)
			bodies.push(current->inferredFlexBody.get());
		if (current->kind == Expression::Kind::PatternCall) {
			SourceLocation start = current->range.sourceStart();
			requireCompilerInvariant(start.sourceFile, "resolved source call has no source location");
			if (pathutil::toAbsoluteUri(start.sourceFile->uri) == params.uri) {
				PatternDefinition *definition = current->selectedPatternDefinition;
				requireCompilerInvariant(
					definition && definition->range.line && definition->range.line->sourceFile,
					"resolved source call has no definition location"
				);
				requireCompilerInvariant(current->type.isDeduced(), "resolved source call has no inferred return type");
				Location definitionLocation;
				definitionLocation.uri = pathutil::toAbsoluteUri(definition->range.line->sourceFile->uri);
				definitionLocation.range = convertRange(definition->range);
				entries.push_back({
					{"range", convertRange(current->range)},
					{"definition", definitionLocation},
					{"returnType", typeToUserName(current->type)},
				});
			}
		}
		for (Expression *argument : current->arguments)
			collectExpression(argument);
	};
	auto collectLine = [&](Expression *expression) {
		// Definition templates and unreachable lines intentionally have no inference metadata.
		if (expression && expression->executionFallsThrough.has_value())
			collectExpression(expression);
	};

	for (ParseContext *context : findContextsFor(params.uri)) {
		if (!hasCompilationStage(context, ParseContext::CompilationStage::InferredTypes))
			continue;
		std::stack<Section *> sections;
		sections.push(context->mainSection);
		while (!sections.empty()) {
			Section *section = sections.top();
			sections.pop();
			for (Section *child : section->children)
				sections.push(child);
			for (CodeLine *line : section->codeLines)
				collectLine(line->expression);
			for (const auto &[key, instantiation] : section->instantiations) {
				if (!instantiation.valid)
					continue;
				requireCompilerInvariant(instantiation.body != nullptr, "inferred instantiation has no body");
				bodies.push(instantiation.body.get());
			}
		}
		while (!bodies.empty()) {
			const InstantiatedSectionBody *body = bodies.top();
			bodies.pop();
			if (!visitedBodies.insert(body).second)
				continue;
			for (const auto &child : body->childBodies)
				bodies.push(child.get());
			for (Expression *expression : body->lineExpressions)
				collectLine(expression);
		}
	}

	std::ranges::sort(entries, [](const Json &left, const Json &right) {
		auto position = [](const Json &entry) {
			const Json &range = entry.at("range");
			return std::tie(
				range.at("start").at("line"), range.at("start").at("character"), range.at("end").at("line"),
				range.at("end").at("character")
			);
		};
		if (position(left) != position(right))
			return position(left) < position(right);
		return left < right;
	});
	// Repeated flex expansions and analysis profiles can describe the same source call.
	entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
	return entries;
}
