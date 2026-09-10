// Reconstruct pattern name from definition elements
static std::string getPatternName(const PatternDefinition *def) {
	std::string name;
	for (const auto &elem : def->patternElements) {
		if (elem.type == PatternElement::Choice && !elem.alternatives.empty()) {
			name += elem.alternatives[0][0].text;
		} else {
			name += elem.text;
		}
	}
	return name;
}

static SymbolKind symbolKindForSection(SectionType type) {
	switch (type) {
	case SectionType::Function:
		return SymbolKind::Function;
	case SectionType::Class:
		return SymbolKind::Class;
	case SectionType::Pattern:
		return SymbolKind::Module;
	default:
		return SymbolKind::Namespace;
	}
}

std::vector<DocumentSymbol> DynLexServer::onDocumentSymbol(const DocumentSymbolParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return {};
	ParseContext *context = findContextFor(params.textDocument.uri);
	if (!hasCompilationStage(context, ParseContext::CompilationStage::AnalyzedSections))
		return {};

	const auto before = [](Position a, Position b) {
		return a.line < b.line || (a.line == b.line && a.character < b.character);
	};
	// Traverse each section once. Anonymous execution/branch sections contribute
	// their source extent to the enclosing symbol, including shorthand bodies.
	std::function<std::optional<Range>(Section *, std::vector<DocumentSymbol> &)> collect =
		[&](Section *section, std::vector<DocumentSymbol> &out) -> std::optional<Range> {
		std::optional<Range> extent;
		auto include = [&](Range range) {
			if (!extent)
				extent = range;
			else {
				if (before(range.start, extent->start))
					extent->start = range.start;
				if (before(extent->end, range.end))
					extent->end = range.end;
			}
		};
		auto includeLine = [&](CodeLine *line) {
			if (pathutil::toAbsoluteUri(line->sourceFile->uri) == params.textDocument.uri)
				include(convertRange(::Range(line, line->rightTrimmedText)));
		};
		if (section->openingLine)
			includeLine(section->openingLine);
		for (CodeLine *line : section->codeLines)
			includeLine(line);
		std::vector<DocumentSymbol> children;
		for (Section *child : section->children) {
			if (auto childRange = collect(child, children))
				include(*childRange);
		}
		for (PatternDefinition *def : section->patternDefinitions) {
			if (!def->range.line || pathutil::toAbsoluteUri(def->range.line->sourceFile->uri) != params.textDocument.uri)
				continue;
			DocumentSymbol symbol;
			symbol.name = getPatternName(def);
			symbol.detail = (section->isFlex ? "flex " : "") + sectionTypeToString(section->type);
			symbol.kind = symbolKindForSection(section->type);
			symbol.selectionRange = convertRange(def->range);
			include(symbol.selectionRange);
			symbol.range = *extent;
			symbol.children = children;
			out.push_back(std::move(symbol));
		}
		if (section->patternDefinitions.empty()) {
			for (auto &child : children)
				out.push_back(std::move(child));
		}
		return extent;
	};
	std::vector<DocumentSymbol> result;
	collect(context->mainSection, result);
	return result;
}
