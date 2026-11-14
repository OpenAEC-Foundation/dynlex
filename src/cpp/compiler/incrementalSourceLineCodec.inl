// Preserve transformed lines and token-only intermediate lines in one pool.
// Unchanged physical lines borrow the current file's text and source mapping.
class SourceLineCodec {
  public:
	SourceLineCodec(ParseContext &context, std::span<CodeLine *const> input) : context(context), input(input) {
		for (CodeLine *line : input) {
			auto [_, added] = fileIndices.try_emplace(line->sourceFile, files.size());
			if (added)
				files.push_back(line->sourceFile);
		}
	}
	void capture(
		std::string &bytes, std::span<CodeLine *const> output, size_t tokenStart, std::span<const SectionLineSyntax> lexical
	) {
		requireCompilerInvariant(lexical.size() == output.size(), "source chunk lost its section-line products");
		std::vector<CodeLine *> pool;
		std::unordered_map<CodeLine *, size_t> indices, raw;
		for (size_t i = 0; i < input.size(); i++)
			raw.emplace(input[i], i);
		auto index = [&](CodeLine *line) {
			auto [entry, added] = indices.try_emplace(line, pool.size());
			if (added)
				pool.push_back(line);
			return entry->second;
		};
		for (CodeLine *line : output)
			(void)index(line);
		for (size_t i = tokenStart; i < context.sourceTokenAnnotations.size(); i++)
			(void)index(context.sourceTokenAnnotations[i].range.line);
		appendParseLength(bytes, pool.size());
		for (CodeLine *line : pool) {
			auto original = raw.find(line);
			appendParseByte(bytes, original != raw.end());
			if (original != raw.end())
				appendParseLength(bytes, original->second);
			else {
				appendParseLength(bytes, fileIndex(line->sourceFile));
				appendParseLength(bytes, line->sourceFileLineIndex);
				appendParseString(bytes, line->fullText);
				appendParseLength(bytes, line->sourceSlices.size());
				for (const SourceSlice &slice : line->sourceSlices) {
					appendParseLength(bytes, slice.transformedStart);
					appendParseLength(bytes, slice.transformedEnd);
					appendParseLength(bytes, fileIndex(slice.sourceFile));
					appendParseLength(bytes, slice.sourceFileLineIndex);
					appendParseLength(bytes, slice.sourceColumnStart);
				}
			}
			appendParseLength(bytes, line->rightTrimmedText.size());
			appendParseLength(bytes, line->logicalIndentOffset);
			appendParseByte(bytes, line->inlineBodyFollows | (line->synthetic << 1) | (line->hasIndentOverride << 2));
			appendParseString(bytes, line->indentOverride);
			appendParseByte(bytes, static_cast<unsigned char>(line->definitionShorthand));
		}
		appendParseLength(bytes, output.size());
		for (size_t i = 0; i < output.size(); i++) {
			appendParseLength(bytes, indices.at(output[i]));
			appendParseLength(bytes, lexical[i].patternStart);
			appendParseLength(bytes, lexical[i].patternLength);
			appendParseByte(bytes, lexical[i].opensSection | (lexical[i].importDirective << 1));
		}
		appendParseLength(bytes, context.sourceTokenAnnotations.size() - tokenStart);
		for (size_t i = tokenStart; i < context.sourceTokenAnnotations.size(); i++) {
			const auto &token = context.sourceTokenAnnotations[i];
			appendParseLength(bytes, indices.at(token.range.line));
			appendParseLength(bytes, token.range.start());
			appendParseLength(bytes, token.range.end());
			appendParseByte(bytes, static_cast<unsigned char>(token.kind));
			appendParseByte(bytes, static_cast<unsigned char>(token.referencedPatternType));
		}
	}
	std::vector<CodeLine *> restore(std::string_view bytes, size_t &position, std::vector<SectionLineSyntax> &sectionLines) {
		size_t count = readParseLength(bytes, position);
		if (count > bytes.size() - position)
			throw std::runtime_error("invalid source line pool size");
		std::vector<CodeLine *> pool;
		for (size_t i = 0; i < count; i++) {
			CodeLine *line;
			if (readParseByte(bytes, position)) {
				size_t raw = readParseLength(bytes, position);
				if (raw >= input.size())
					throw std::runtime_error("invalid transformed source input reference");
				line = input[raw];
			} else {
				auto *file = files.at(readParseLength(bytes, position));
				int physical = readParseLength(bytes, position);
				std::string text(readParseString(bytes, position));
				std::vector<SourceSlice> slices;
				size_t sliceCount = readParseLength(bytes, position);
				for (size_t n = 0; n < sliceCount; n++) {
					int start = readParseLength(bytes, position), end = readParseLength(bytes, position);
					auto *source = files.at(readParseLength(bytes, position));
					int sourceLine = readParseLength(bytes, position), column = readParseLength(bytes, position);
					slices.push_back({start, end, source, sourceLine, column});
				}
				line = context.createCodeLine(file, physical, std::move(text), std::move(slices));
			}
			size_t trim = readParseLength(bytes, position);
			if (trim > line->fullText.size())
				throw std::runtime_error("invalid transformed source trim");
			line->rightTrimmedText = line->fullText.substr(0, trim);
			line->logicalIndentOffset = readParseLength(bytes, position);
			unsigned flags = readParseByte(bytes, position);
			line->inlineBodyFollows = flags & 1;
			line->synthetic = flags & 2;
			line->hasIndentOverride = flags & 4;
			line->indentOverride = readParseString(bytes, position);
			line->definitionShorthand = static_cast<DefinitionShorthand>(readParseByte(bytes, position));
			pool.push_back(line);
		}
		std::vector<CodeLine *> output;
		size_t outputs = readParseLength(bytes, position);
		if (outputs > (bytes.size() - position) / 4)
			throw std::runtime_error("invalid transformed section input count");
		output.reserve(outputs);
		for (size_t i = 0; i < outputs; i++) {
			CodeLine *line = pool.at(readParseLength(bytes, position));
			SectionLineSyntax lexical;
			lexical.patternStart = readParseLength(bytes, position);
			lexical.patternLength = readParseLength(bytes, position);
			size_t trim = line->rightTrimmedText.size();
			if (lexical.patternStart > trim || lexical.patternLength > trim - lexical.patternStart)
				throw std::runtime_error("invalid transformed section input range");
			unsigned kind = readParseByte(bytes, position);
			if (kind > 3)
				throw std::runtime_error("invalid transformed section input kind");
			lexical.opensSection = kind & 1;
			lexical.importDirective = kind & 2;
			sectionLines.push_back(lexical);
			output.push_back(line);
		}
		size_t tokens = readParseLength(bytes, position);
		for (size_t i = 0; i < tokens; i++) {
			CodeLine *line = pool.at(readParseLength(bytes, position));
			int start = readParseLength(bytes, position), end = readParseLength(bytes, position);
			auto kind = static_cast<ParseContext::SourceTokenKind>(readParseByte(bytes, position));
			auto type = static_cast<SectionType>(readParseByte(bytes, position));
			context.addSourceToken(Range(line, start, end), kind, type);
		}
		return output;
	}

  private:
	ParseContext &context;
	std::span<CodeLine *const> input;
	size_t fileIndex(lsp::SourceFile *file) const {
		auto found = fileIndices.find(file);
		requireCompilerInvariant(found != fileIndices.end(), "transformed source escaped its input file span");
		return found->second;
	}
	std::unordered_map<lsp::SourceFile *, size_t> fileIndices;
	std::vector<lsp::SourceFile *> files;
};
