class IncrementalSourcePreparation final : public SourcePreparationHooks {
  public:
	IncrementalSourcePreparation(ParseContext &context, IncrementalParsedExpressionCache &cache, RecordingFileSystem &files)
		: context(context), cache(cache), files(files) {
		context.sourcePreparationHooks = this;
	}
	~IncrementalSourcePreparation() { context.sourcePreparationHooks = nullptr; }

	std::optional<std::vector<PreparedSourceLine>> reuse(lsp::SourceFile &file, const SyntaxConfig &syntax) override {
		auto payload = cache.sourceProduct("S" + digest(file.uri), inputs(file, syntax));
		if (!payload) {
			work.filesProcessed++;
			return std::nullopt;
		}
		std::vector<PreparedSourceLine> result = restore(file, *payload);
		work.filesReused++;
		work.linesReused += result.size();
		return result;
	}

	void record(lsp::SourceFile &file, const SyntaxConfig &syntax, const std::vector<PreparedSourceLine> &lines) override {
		std::string payload;
		appendParseLength(payload, lines.size());
		for (size_t index = 0; index < lines.size(); index++) {
			const PreparedSourceLine &entry = lines[index];
			const CodeLine &line = *entry.line;
			requireCompilerInvariant(
				line.sourceFile == &file && line.sourceFileLineIndex == static_cast<int>(index),
				"prepared source line lost its physical file identity"
			);
			bool transformed = !line.hasIdentitySourceMapping();
			appendParseByte(payload, static_cast<unsigned char>(transformed) | (entry.importPath ? 2 : 0));
			if (transformed) {
				appendParseString(payload, line.fullText);
				appendParseLength(payload, line.sourceSlices.size());
				for (const SourceSlice &slice : line.sourceSlices) {
					requireCompilerInvariant(
						slice.sourceFile == &file && slice.sourceFileLineIndex == static_cast<int>(index),
						"file preparation unexpectedly contains a cross-line source slice"
					);
					appendParseLength(payload, slice.transformedStart);
					appendParseLength(payload, slice.transformedEnd);
					appendParseLength(payload, slice.sourceColumnStart);
				}
			}
			appendParseLength(payload, line.rightTrimmedText.size());
			if (entry.importPath) {
				appendParseLength(payload, entry.importPath->data() - line.fullText.data());
				appendParseLength(payload, entry.importPath->size());
			}
			appendParseLength(payload, entry.tokens.size());
			for (const auto &token : entry.tokens) {
				requireCompilerInvariant(
					token.range.line->sourceFile == &file && token.range.line->sourceFileLineIndex == static_cast<int>(index),
					"prepared source token belongs to another physical line"
				);
				appendParseLength(payload, token.range.start());
				appendParseLength(payload, token.range.end());
				appendParseByte(payload, static_cast<unsigned char>(token.kind));
				appendParseByte(payload, static_cast<unsigned char>(token.referencedPatternType));
			}
		}
		cache.recordSourceProduct("S" + digest(file.uri), inputs(file, syntax), std::move(payload));
	}

	SourcePreparationWork work;

  private:
	std::string inputs(const lsp::SourceFile &file, const SyntaxConfig &syntax) const {
		std::string value = files.sourceContentDigest(file);
		for (const std::string *field :
			 {&syntax.commentPrefix, &syntax.importKeyword, &syntax.formattedStringPrefix, &syntax.argumentPackSeparator,
			  &syntax.argumentPackConjunction})
			appendParseString(value, *field);
		return digest(value);
	}

	std::vector<PreparedSourceLine> restore(lsp::SourceFile &file, std::string_view payload) {
		size_t position = 0;
		size_t count = readParseLength(payload, position);
		size_t physicalCount = file.content.empty() ? 0 : file.lineCount();
		if (physicalCount && file.getLineWithTerminator(physicalCount - 1).empty())
			physicalCount--;
		if (count != physicalCount)
			throw std::runtime_error("prepared source line count disagrees with its source");
		std::vector<PreparedSourceLine> lines;
		lines.reserve(count);
		for (size_t index = 0; index < count; index++) {
			std::string_view raw = file.getLineWithTerminator(index);
			unsigned char flags = readParseByte(payload, position);
			if (flags > 3)
				throw std::runtime_error("invalid prepared source line flags");
			CodeLine *line;
			if (flags & 1) {
				std::string text(readParseString(payload, position));
				std::vector<SourceSlice> slices;
				size_t sliceCount = readParseLength(payload, position);
				if (sliceCount > payload.size() - position)
					throw std::runtime_error("invalid prepared source slice count");
				for (size_t sliceIndex = 0; sliceIndex < sliceCount; sliceIndex++) {
					size_t start = readParseLength(payload, position), end = readParseLength(payload, position);
					size_t column = readParseLength(payload, position);
					if (start > end || end > text.size() || column > raw.size() || end - start > raw.size() - column)
						throw std::runtime_error("invalid prepared source slice");
					slices.push_back(
						{static_cast<int>(start), static_cast<int>(end), &file, static_cast<int>(index),
						 static_cast<int>(column)}
					);
				}
				line = context.createCodeLine(&file, index, std::move(text), std::move(slices));
			} else
				line = context.createBorrowedSourceLine(&file, index, raw);
			size_t trimmed = readParseLength(payload, position);
			if (trimmed > line->fullText.size())
				throw std::runtime_error("invalid prepared source trimmed range");
			line->rightTrimmedText = line->fullText.substr(0, trimmed);
			std::optional<std::string_view> importPath;
			if (flags & 2) {
				size_t start = readParseLength(payload, position), length = readParseLength(payload, position);
				if (start > trimmed || length > trimmed - start)
					throw std::runtime_error("invalid prepared source import range");
				importPath = line->fullText.substr(start, length);
			}
			size_t tokenCount = readParseLength(payload, position);
			if (tokenCount > payload.size() - position)
				throw std::runtime_error("invalid prepared source token count");
			CodeLine *authored = line;
			if (tokenCount && (flags & 1))
				authored = context.createBorrowedSourceLine(&file, index, raw);
			std::vector<ParseContext::SourceTokenAnnotation> tokens;
			for (size_t tokenIndex = 0; tokenIndex < tokenCount; tokenIndex++) {
				size_t start = readParseLength(payload, position), end = readParseLength(payload, position);
				unsigned char kind = readParseByte(payload, position), type = readParseByte(payload, position);
				if (start > end || end > raw.size() ||
					kind > static_cast<unsigned char>(ParseContext::SourceTokenKind::PatternReference) ||
					type >= static_cast<unsigned char>(SectionType::Count))
					throw std::runtime_error("invalid prepared source token");
				tokens.push_back(
					{Range(authored, start, end), static_cast<ParseContext::SourceTokenKind>(kind),
					 static_cast<SectionType>(type)}
				);
			}
			lines.push_back({line, importPath, std::move(tokens)});
		}
		if (position != payload.size())
			throw std::runtime_error("trailing prepared source data");
		return lines;
	}

	ParseContext &context;
	IncrementalParsedExpressionCache &cache;
	RecordingFileSystem &files;
};
