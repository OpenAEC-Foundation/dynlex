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

Json captureCachedDiagnostics(const ParseContext &context, std::string &rendered, std::set<std::string> &sources) {
	Json locations = Json::array();
	std::unordered_map<const lsp::SourceFile *, std::vector<int>> lineNumbers;
	for (const Diagnostic &diagnostic : context.diagnostics) {
		size_t base = rendered.size();
		auto appendRange = [&](std::string &text, const Range &range) {
			size_t offset = base + text.size();
			std::string location = range.toString();
			text += location;
			SourceLocation start = range.sourceStart();
			SourceLocation end = range.sourceEnd();
			if (end.sourceFile)
				sources.insert(end.sourceFile->uri);
			if (!start.sourceFile)
				return;
			const lsp::SourceFile *source = start.sourceFile;
			sources.insert(source->uri);
			auto [found, inserted] = lineNumbers.try_emplace(source);
			if (inserted) {
				const std::string &prefix =
					source->isInternal ? context.builtinSyntax.commentPrefix : context.projectSyntax.commentPrefix;
				found->second = semanticLineNumbers(source->content, prefix);
			}
			auto line = std::lower_bound(found->second.begin(), found->second.end(), start.sourceFileLineIndex);
			int ordinal = line != found->second.end() && *line == start.sourceFileLineIndex
							  ? static_cast<int>(line - found->second.begin())
							  : -1;
			locations.push_back(Json::array({offset, location.size(), source->uri, ordinal, start.column, end.column}));
		};
		rendered += diagnostic.toString(appendRange);
		rendered.push_back('\n');
	}
	return locations;
}

std::optional<std::string>
relocateCachedDiagnostics(const Json &previous, RecordingFileSystem &files, const std::set<std::string> &changedSources) {
	const std::string &original = previous.at("diagnostics").get_ref<const std::string &>();
	const Json &locations = previous.at("diagnostic_locations");
	std::unordered_map<std::string, std::vector<int>> lineNumbers;
	std::string rendered;
	rendered.reserve(original.size());
	size_t cursor = 0;
	for (const Json &location : locations) {
		size_t offset = location.at(0).get<size_t>();
		size_t length = location.at(1).get<size_t>();
		requireCompilerInvariant(
			offset >= cursor && offset <= original.size() && length <= original.size() - offset,
			"cached diagnostic location lies outside its rendered text"
		);
		rendered.append(original, cursor, offset - cursor);
		const std::string &uri = location.at(2).get_ref<const std::string &>();
		if (!changedSources.contains(uri)) {
			rendered.append(original, offset, length);
		} else {
			int ordinal = location.at(3).get<int>();
			if (ordinal < 0)
				return std::nullopt;
			auto found = lineNumbers.find(uri);
			if (found == lineNumbers.end()) {
				lsp::SourceFile *source = files.cachedFile(uri);
				requireCompilerInvariant(source != nullptr, "cached diagnostic source disappeared");
				const Json &prefixes = previous.at("comment_prefixes");
				const std::string &prefix = source->isInternal ? prefixes.at("builtin").get_ref<const std::string &>()
															   : prefixes.at("project").get_ref<const std::string &>();
				found = lineNumbers.emplace(uri, semanticLineNumbers(source->content, prefix)).first;
			}
			requireCompilerInvariant(
				static_cast<size_t>(ordinal) < found->second.size(), "semantically equal diagnostic source lost a code line"
			);
			rendered += pathutil::toDisplayPath(uri) + ":" + std::to_string(found->second[ordinal] + 1) + ":" +
						std::to_string(location.at(4).get<int>() + 1) + "-" + std::to_string(location.at(5).get<int>() + 1);
		}
		cursor = offset + length;
	}
	rendered.append(original, cursor, std::string::npos);
	return rendered;
}
