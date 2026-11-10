using ReferenceLiteralsBySource = std::map<std::string, std::unordered_set<std::string_view>>;

static ReferenceLiteralsBySource
referenceStartLiteralsBySource(const std::map<std::string, Section *> &sections, const std::set<std::string> &sources) {
	ReferenceLiteralsBySource result;
	std::unordered_set<const CodeLine *> lines;
	auto addLine = [&](const CodeLine *line) {
		if (sources.contains(line->sourceFile->uri) && lines.insert(line).second)
			addMatcherStartLiterals(line->fullText, result[line->sourceFile->uri]);
	};
	for (const auto &[_, section] : sections)
		for (const PatternReference *reference : section->patternReferences) {
			const CodeLine *line = reference->range().line;
			addLine(line);
			if (sources.contains(line->sourceFile->uri))
				addMatcherStartLiterals(reference->pattern.text, result[line->sourceFile->uri]);
			if (reference->expression)
				for (const Expression *argument : reference->expression->arguments)
					if (argument && argument->range.line)
						addLine(argument->range.line);
		}
	return result;
}

static std::set<std::string>
changedReferenceLiteralSources(const ParseContext &context, const Json &observations, const Json &prior) {
	std::set<std::string> changed;
	const Json &priorSources = prior.at("reference_literals_by_source");
	for (const auto &[_, source] : context.importedFiles) {
		const std::string &uri = source->uri;
		if (!priorSources.contains(uri) || priorSources.at(uri).at("content") != observations.at(uri).at("content"))
			changed.insert(uri);
	}
	return changed;
}

static void refreshReferenceLiteralIndex(
	Json &projection, const ParseContext &context, const Json &observations, const std::map<std::string, Section *> &sections,
	const Json *prior
) {
	Json bySource = Json::object();
	std::set<std::string> changed;
	for (const auto &[_, source] : context.importedFiles) {
		const std::string &uri = source->uri;
		if (prior && prior->at("reference_literals_by_source").contains(uri) &&
			prior->at("reference_literals_by_source").at(uri).at("content") == observations.at(uri).at("content"))
			bySource[uri] = prior->at("reference_literals_by_source").at(uri);
		else
			changed.insert(uri);
	}
	ReferenceLiteralsBySource current = referenceStartLiteralsBySource(sections, changed);
	for (const std::string &uri : changed) {
		Json literals = Json::object();
		for (std::string_view literal : current[uri])
			literals[llvm::toHex(llvm::StringRef(literal), true)] = true;
		bySource[uri] = Json{{"content", observations.at(uri).at("content")}, {"literals", std::move(literals)}};
	}
	Json global = Json::object();
	for (const auto &[_, source] : bySource.items())
		for (const auto &[literal, __] : source.at("literals").items())
			global[literal] = true;
	projection["reference_literals_by_source"] = std::move(bySource);
	projection["reference_literals_global"] = std::move(global);
}
