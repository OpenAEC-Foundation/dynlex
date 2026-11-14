class IncrementalSourceTransforms final : public SourceTransformHooks {
  public:
	IncrementalSourceTransforms(ParseContext &context, IncrementalParsedExpressionCache &cache, RecordingFileSystem &files)
		: context(context), cache(cache), files(files) {
		context.sourceTransformHooks = this;
	}
	~IncrementalSourceTransforms() { context.sourceTransformHooks = nullptr; }

	std::optional<size_t> reuse(
		std::span<CodeLine *const> input, size_t start, SourceTransformState &state, std::vector<CodeLine *> &output
	) override {
		auto key = productKey(*input[start]);
		auto previous = cache.priorSourceProduct(key);
		if (!previous)
			return {};
		size_t position = 64;
		size_t count = readParseLength(*previous, position);
		if (!count)
			throw std::runtime_error("empty transformed source input span");
		if (count > input.size() - start)
			return {};
		auto span = input.subspan(start, count);
		if (previous->substr(0, 64) != inputFingerprint(span, state))
			return {};
		readState(*previous, position, state);
		SourceLineCodec codec(context, span);
		auto lines = codec.restore(*previous, position, sectionLines);
		if (position != previous->size())
			throw std::runtime_error("trailing transformed source product data");
		output.insert(output.end(), lines.begin(), lines.end());
		cache.retainSourceProduct(key, *previous);
		work.chunksReused++;
		work.inputLinesReused += count;
		work.sectionLinesReused += lines.size();
		return count;
	}

	void record(
		std::span<CodeLine *const> input, size_t start, size_t end, const SourceTransformState &before,
		const SourceTransformState &after, std::span<CodeLine *const> output, size_t tokenStart
	) override {
		auto span = input.subspan(start, end - start);
		std::string payload;
		appendParseLength(payload, span.size());
		appendState(payload, after, unitFiles(span));
		SourceLineCodec codec(context, span);
		size_t lexicalStart = sectionLines.size();
		for (const CodeLine *line : output)
			sectionLines.push_back(sectionLineSyntax(*line, syntaxConfigForSourceFile(context, line->sourceFile)));
		codec.capture(payload, output, tokenStart, std::span(sectionLines).subspan(lexicalStart));
		cache.recordSourceProduct(productKey(*span.front()), inputFingerprint(span, before), std::move(payload));
		work.chunksProcessed++;
		work.inputLinesProcessed += span.size();
		work.sectionLinesProcessed += output.size();
	}
	SourceTransformWork work;

  private:
	using Files = std::map<std::string, lsp::SourceFile *>;
	Files unitFiles(std::span<CodeLine *const> lines) const {
		Files result;
		for (CodeLine *line : lines)
			result.try_emplace(line->sourceFile->uri, line->sourceFile);
		return result;
	}
	const Files &sourceFiles() {
		if (byUri.empty())
			for (const auto &[_, file] : context.importedFiles)
				byUri.emplace(file->uri, file);
		return byUri;
	}
	std::string productKey(const CodeLine &line) const {
		return "P" + digest(line.sourceFile->uri + ":" + std::to_string(line.sourceFileLineIndex));
	}
	const std::string &lexicalInputs(lsp::SourceFile &file) {
		auto [entry, added] = lexicalFingerprints.try_emplace(&file);
		if (added) {
			const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, &file);
			std::string bytes = files.sourceContentDigest(file);
			for (const std::string *field :
				 {&syntax.commentPrefix,
				  &syntax.importKeyword,
				  &syntax.formattedStringPrefix,
				  &syntax.argumentPackSeparator,
				  &syntax.argumentPackConjunction,
				  &syntax.actionShorthand,
				  &syntax.valueShorthand,
				  &syntax.replacementShorthand,
				  &syntax.sectionOpener,
				  &syntax.functionName,
				  &syntax.conversionName,
				  &syntax.sectionName,
				  &syntax.className,
				  &syntax.flexName,
				  &syntax.localName,
				  &syntax.publicName,
				  &syntax.exposedName,
				  &syntax.implicitName,
				  &syntax.executeSectionName,
				  &syntax.replacementSectionName})
				appendParseString(bytes, *field);
			entry->second = digest(bytes);
		}
		return entry->second;
	}
	std::string inputFingerprint(std::span<CodeLine *const> lines, const SourceTransformState &state) {
		std::string bytes;
		appendParseLength(bytes, lines.size());
		for (size_t cursor = 0; cursor < lines.size();) {
			CodeLine &first = *lines[cursor];
			size_t end = cursor + 1;
			while (end < lines.size() && lines[end]->sourceFile == first.sourceFile &&
				   lines[end]->sourceFileLineIndex == first.sourceFileLineIndex + static_cast<int>(end - cursor))
				end++;
			appendParseString(bytes, first.sourceFile->uri);
			appendParseString(bytes, lexicalInputs(*first.sourceFile));
			appendParseLength(bytes, first.sourceFileLineIndex);
			appendParseLength(bytes, end - cursor);
			cursor = end;
		}
		appendState(bytes, state, unitFiles(lines));
		return digest(bytes);
	}
	void appendState(std::string &bytes, const SourceTransformState &state, const Files &selected) const {
		appendParseString(bytes, state.indentation.indentString);
		appendParseLength(bytes, state.indentation.indentLevel);
		appendParseLength(bytes, selected.size());
		for (const auto &[uri, file] : selected) {
			appendParseString(bytes, uri);
			auto active = state.activeByFile.find(file);
			appendParseLength(bytes, active == state.activeByFile.end() ? 0 : active->second.size());
			if (active != state.activeByFile.end())
				for (int depth : active->second)
					appendParseLength(bytes, depth);
		}
	}
	void readState(std::string_view bytes, size_t &position, SourceTransformState &state) {
		state.indentation.indentString = readParseString(bytes, position);
		state.indentation.indentLevel = readParseLength(bytes, position);
		size_t count = readParseLength(bytes, position);
		for (size_t index = 0; index < count; index++) {
			auto *file = sourceFiles().at(std::string(readParseString(bytes, position)));
			auto &active = state.activeByFile[file];
			active.clear();
			size_t depths = readParseLength(bytes, position);
			for (size_t depth = 0; depth < depths; depth++)
				active.push_back(readParseLength(bytes, position));
		}
	}
	ParseContext &context;
	IncrementalParsedExpressionCache &cache;
	RecordingFileSystem &files;
	Files byUri;
	std::unordered_map<const lsp::SourceFile *, std::string> lexicalFingerprints;
};
