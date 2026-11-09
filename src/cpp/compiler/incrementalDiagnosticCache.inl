template <typename Visit> void forEachSemanticLine(std::string_view content, std::string_view commentPrefix, Visit visit) {
	int lineIndex = 0;
	for (size_t start = 0; start < content.size(); lineIndex++) {
		size_t end = content.find('\n', start);
		if (end == std::string_view::npos)
			end = content.size();
		std::string_view line = content.substr(start, end - start);
		if (size_t comment = findCommentStart(line, commentPrefix); comment != std::string_view::npos)
			line = line.substr(0, comment);
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
			line.remove_suffix(1);
		if (!line.empty())
			visit(line, lineIndex);
		start = end + (end < content.size());
	}
}

std::string semanticSourceDigest(std::string_view content, std::string_view commentPrefix) {
	std::string semantic;
	semantic.reserve(content.size());
	forEachSemanticLine(content, commentPrefix, [&](std::string_view line, int) {
		semantic.append(line);
		semantic.push_back('\n');
	});
	return digest(semantic);
}

std::vector<int> semanticLineNumbers(std::string_view content, std::string_view commentPrefix) {
	std::vector<int> lines;
	forEachSemanticLine(content, commentPrefix, [&](std::string_view, int index) {
		lines.push_back(index);
	});
	return lines;
}

Json configuredMessageRecord(const ConfiguredDiagnosticMessage &message) {
	Json nested = Json::array();
	for (const ConfiguredDiagnosticMessage &replacement : message.nestedPlaceholders)
		nested.push_back(configuredMessageRecord(replacement));
	return Json{
		{"key", message.key},
		{"variant", message.variant},
		{"internal", message.internal},
		{"placeholders", message.placeholders},
		{"name", message.placeholderName},
		{"nested", std::move(nested)}
	};
}

ConfiguredDiagnosticMessage restoreConfiguredMessage(const Json &record) {
	ConfiguredDiagnosticMessage message{
		record.at("key").get<std::string>(),
		record.at("variant").get<std::string>(),
		record.at("placeholders").get<std::vector<std::pair<std::string, std::string>>>(),
		record.at("internal").get<bool>(),
		record.at("name").get<std::string>(),
		{}
	};
	for (const Json &nested : record.at("nested"))
		message.nestedPlaceholders.push_back(restoreConfiguredMessage(nested));
	return message;
}

Json cachedDiagnosticText(std::string_view text, const std::shared_ptr<const ConfiguredDiagnosticMessage> &configured) {
	return Json{{"text", text}, {"configured", configured ? configuredMessageRecord(*configured) : Json(nullptr)}};
}

std::string renderCachedDiagnosticText(const ParseContext &context, const Json &record) {
	if (record.at("configured").is_null())
		return record.at("text").get<std::string>();
	return renderConfiguredDiagnosticMessage(context, restoreConfiguredMessage(record.at("configured")));
}

Json captureDiagnosticRange(
	const ParseContext &context, const Range &range, std::set<std::string> &sources,
	std::unordered_map<const lsp::SourceFile *, std::vector<int>> &lineNumbers
) {
	Json record{{"text", range.toString()}, {"source", nullptr}};
	SourceLocation start = range.sourceStart();
	SourceLocation end = range.sourceEnd();
	if (end.sourceFile)
		sources.insert(end.sourceFile->uri);
	if (!start.sourceFile)
		return record;
	const lsp::SourceFile *source = start.sourceFile;
	sources.insert(source->uri);
	auto [found, inserted] = lineNumbers.try_emplace(source);
	if (inserted) {
		const std::string &prefix =
			source->isInternal ? context.builtinSyntax.commentPrefix : context.projectSyntax.commentPrefix;
		found->second = semanticLineNumbers(source->content, prefix);
	}
	auto line = std::lower_bound(found->second.begin(), found->second.end(), start.sourceFileLineIndex);
	int ordinal =
		line != found->second.end() && *line == start.sourceFileLineIndex ? static_cast<int>(line - found->second.begin()) : -1;
	record["source"] = source->uri;
	record["ordinal"] = ordinal;
	record["start_column"] = start.column;
	record["end_column"] = end.column;
	return record;
}

Json captureCachedDiagnostics(const ParseContext &context, std::string &rendered, std::set<std::string> &sources) {
	Json records = Json::array();
	std::unordered_map<const lsp::SourceFile *, std::vector<int>> lineNumbers;
	for (const Diagnostic &diagnostic : context.diagnostics) {
		Json related = Json::array();
		for (const RelatedInfo &note : diagnostic.relatedInfo)
			related.push_back(
				Json{
					{"message", cachedDiagnosticText(note.message, note.configuredMessage)},
					{"range", captureDiagnosticRange(context, note.range, sources, lineNumbers)}
				}
			);
		records.push_back(
			Json{
				{"level", enumToString(diagnostic.level)},
				{"message", cachedDiagnosticText(diagnostic.message, diagnostic.configuredMessage)},
				{"range", captureDiagnosticRange(context, diagnostic.range, sources, lineNumbers)},
				{"related", std::move(related)}
			}
		);
		rendered += diagnostic.toString();
		rendered.push_back('\n');
	}
	return records;
}

std::optional<std::string> renderCachedRange(
	const Json &record, RecordingFileSystem &files, const Json &prefixes, const std::set<std::string> &changedSources,
	std::unordered_map<std::string, std::vector<int>> &lineNumbers
) {
	if (record.at("source").is_null())
		return record.at("text").get<std::string>();
	const std::string &uri = record.at("source").get_ref<const std::string &>();
	if (!changedSources.contains(uri))
		return record.at("text").get<std::string>();
	int ordinal = record.at("ordinal").get<int>();
	if (ordinal < 0)
		return std::nullopt;
	auto found = lineNumbers.find(uri);
	if (found == lineNumbers.end()) {
		lsp::SourceFile *source = files.cachedFile(uri);
		requireCompilerInvariant(source != nullptr, "cached diagnostic source disappeared");
		const std::string &prefix = source->isInternal ? prefixes.at("builtin").get_ref<const std::string &>()
													   : prefixes.at("project").get_ref<const std::string &>();
		found = lineNumbers.emplace(uri, semanticLineNumbers(source->content, prefix)).first;
	}
	requireCompilerInvariant(
		static_cast<size_t>(ordinal) < found->second.size(), "semantically equal diagnostic source lost a code line"
	);
	return pathutil::toDisplayPath(uri) + ":" + std::to_string(found->second[ordinal] + 1) + ":" +
		   std::to_string(record.at("start_column").get<int>() + 1) + "-" +
		   std::to_string(record.at("end_column").get<int>() + 1);
}

std::optional<std::string> renderCachedDiagnostics(
	const Json &previous, RecordingFileSystem &files, const ParseContext &context, const std::set<std::string> &changedSources
) {
	std::unordered_map<std::string, std::vector<int>> lineNumbers;
	std::string rendered;
	for (const Json &diagnostic : previous.at("diagnostic_records")) {
		std::optional<std::string> range =
			renderCachedRange(diagnostic.at("range"), files, previous.at("comment_prefixes"), changedSources, lineNumbers);
		if (!range)
			return std::nullopt;
		rendered += *range + ": " + diagnostic.at("level").get<std::string>() + ": " +
					renderCachedDiagnosticText(context, diagnostic.at("message"));
		for (const Json &note : diagnostic.at("related")) {
			range = renderCachedRange(note.at("range"), files, previous.at("comment_prefixes"), changedSources, lineNumbers);
			if (!range)
				return std::nullopt;
			rendered += "\n  note: " + renderCachedDiagnosticText(context, note.at("message")) + " " + *range;
		}
		rendered.push_back('\n');
	}
	return rendered;
}
