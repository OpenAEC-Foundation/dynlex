#include "sourceTransform.h"
#include "IndentData.h"
#include "diagnostic.h"
#include "parseContext.h"
#include "sourceFile.h"
#include "syntaxConfig.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct ActiveShorthand {
	int sourceIndentLevel = 0;
};

static std::string_view trimRight(std::string_view text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		text.remove_suffix(1);
	return text;
}

static size_t leadingWhitespaceLength(std::string_view text) {
	size_t result = 0;
	while (result < text.size() && std::isspace(static_cast<unsigned char>(text[result])))
		result++;
	return result;
}

} // namespace

std::optional<TopLevelSectionOpener> findTopLevelSectionOpener(std::string_view text, std::string_view opener) {
	if (opener.empty())
		return std::nullopt;
	int parentheses = 0;
	int brackets = 0;
	int braces = 0;
	bool inString = false;
	bool escaped = false;
	for (size_t index = 0; index + opener.size() <= text.size(); index++) {
		char character = text[index];
		if (inString) {
			if (escaped) {
				escaped = false;
				continue;
			}
			if (character == '\\')
				escaped = true;
			else if (character == '"')
				inString = false;
			continue;
		}
		if (character == '"') {
			inString = true;
			continue;
		}
		if (character == '(') {
			parentheses++;
			continue;
		}
		if (character == ')' && parentheses > 0) {
			parentheses--;
			continue;
		}
		if (character == '[') {
			brackets++;
			continue;
		}
		if (character == ']' && brackets > 0) {
			brackets--;
			continue;
		}
		if (character == '{') {
			braces++;
			continue;
		}
		if (character == '}' && braces > 0) {
			braces--;
			continue;
		}
		if (parentheses != 0 || brackets != 0 || braces != 0 || text.substr(index, opener.size()) != opener)
			continue;
		if (text.substr(0, index).find_last_not_of(" \t") == std::string_view::npos)
			continue;
		size_t bodyOffset = index + opener.size();
		while (bodyOffset < text.size() && std::isspace(static_cast<unsigned char>(text[bodyOffset])))
			bodyOffset++;
		return TopLevelSectionOpener{index, bodyOffset};
	}
	return std::nullopt;
}

namespace {

static bool phraseStartsText(std::string_view text, std::string_view phrase) {
	return text.starts_with(phrase) &&
		   (text.size() == phrase.size() || std::isspace(static_cast<unsigned char>(text[phrase.size()])));
}

static std::optional<size_t> suffixPhraseStart(std::string_view text, std::string_view phrase) {
	text = trimRight(text);
	if (!text.ends_with(phrase))
		return std::nullopt;
	size_t start = text.size() - phrase.size();
	if (start > 0 && !std::isspace(static_cast<unsigned char>(text[start - 1])))
		return std::nullopt;
	return start;
}

static bool startsExplicitDefinition(std::string_view text, const SyntaxConfig &syntax) {
	size_t tokenEnd = text.find_first_of(" \t");
	std::string_view firstToken = text.substr(0, tokenEnd);
	return firstToken == syntax.functionName || firstToken == syntax.conversionName || firstToken == syntax.sectionName ||
		   firstToken == syntax.className || firstToken == syntax.flexName || firstToken == syntax.localName ||
		   firstToken == syntax.exposedName || firstToken == syntax.implicitName;
}

static void appendSourceSlices(
	std::vector<SourceSlice> &target, const CodeLine &source, size_t sourceStart, size_t sourceEnd, int transformedStart
) {
	if (sourceStart >= sourceEnd)
		return;
	if (source.sourceSlices.empty()) {
		target.push_back({
			transformedStart,
			transformedStart + static_cast<int>(sourceEnd - sourceStart),
			source.sourceFile,
			source.sourceFileLineIndex,
			static_cast<int>(sourceStart),
		});
		return;
	}
	for (const SourceSlice &slice : source.sourceSlices) {
		size_t sliceStart = static_cast<size_t>(std::max(0, slice.transformedStart));
		size_t sliceEnd = static_cast<size_t>(std::max(0, slice.transformedEnd));
		size_t overlapStart = std::max(sourceStart, sliceStart);
		size_t overlapEnd = std::min(sourceEnd, sliceEnd);
		if (overlapStart >= overlapEnd)
			continue;
		target.push_back({
			transformedStart + static_cast<int>(overlapStart - sourceStart),
			transformedStart + static_cast<int>(overlapEnd - sourceStart),
			slice.sourceFile,
			slice.sourceFileLineIndex,
			slice.sourceColumnStart + static_cast<int>(overlapStart - sliceStart),
		});
	}
}

class LogicalLineBuilder {
  public:
	LogicalLineBuilder(ParseContext &context, CodeLine &source) : context(context), source(source) {}

	void appendSource(size_t start, size_t end) {
		int transformedStart = static_cast<int>(text.size());
		text += source.fullText.substr(start, end - start);
		appendSourceSlices(slices, source, start, end, transformedStart);
	}

	void appendSynthetic(std::string_view value, size_t sourceAnchor) {
		int transformedStart = static_cast<int>(text.size());
		text += value;
		SourceLocation anchor = source.mapOffsetToSource(static_cast<int>(sourceAnchor));
		slices.push_back({transformedStart, transformedStart, anchor.sourceFile, anchor.sourceFileLineIndex, anchor.column});
	}

	CodeLine *finish(
		std::string_view indent, int logicalIndentOffset, DefinitionShorthand shorthand = DefinitionShorthand::None,
		bool synthetic = false
	) {
		CodeLine *result =
			context.createCodeLine(source.sourceFile, source.sourceFileLineIndex, std::move(text), std::move(slices));
		result->rightTrimmedText = result->fullText;
		result->logicalIndentOffset = logicalIndentOffset;
		result->hasIndentOverride = !indent.empty();
		result->indentOverride = std::string(indent);
		result->definitionShorthand = shorthand;
		result->synthetic = synthetic;
		return result;
	}

  private:
	ParseContext &context;
	CodeLine &source;
	std::string text;
	std::vector<SourceSlice> slices;
};

static void annotatePhrase(ParseContext &context, CodeLine *line, size_t start, std::string_view phrase) {
	size_t offset = 0;
	while (offset < phrase.size()) {
		while (offset < phrase.size() && std::isspace(static_cast<unsigned char>(phrase[offset])))
			offset++;
		size_t tokenStart = offset;
		while (offset < phrase.size() && !std::isspace(static_cast<unsigned char>(phrase[offset])))
			offset++;
		if (tokenStart < offset)
			context.addSourceToken(Range(line, start + tokenStart, start + offset), ParseContext::SourceTokenKind::Keyword);
	}
}

static CodeLine *createHeaderLine(
	ParseContext &context, CodeLine *source, std::string_view indent, size_t patternStart, size_t patternEnd,
	size_t openerStart, size_t openerEnd, int logicalIndentOffset, DefinitionShorthand shorthand
) {
	LogicalLineBuilder builder(context, *source);
	builder.appendSource(0, indent.size());
	builder.appendSource(patternStart, patternEnd);
	builder.appendSource(openerStart, openerEnd);
	return builder.finish(indent, logicalIndentOffset, shorthand);
}

static CodeLine *createBodySectionLine(
	ParseContext &context, CodeLine *source, std::string_view indent, std::string_view sectionName,
	std::string_view sectionOpener, size_t sourceAnchor, int logicalIndentOffset
) {
	LogicalLineBuilder builder(context, *source);
	builder.appendSource(0, indent.size());
	builder.appendSynthetic(std::string(sectionName) + std::string(sectionOpener), sourceAnchor);
	return builder.finish(indent, logicalIndentOffset, DefinitionShorthand::None, true);
}

static bool reportMissingPattern(ParseContext &context, CodeLine *line, size_t keywordStart, std::string_view keyword) {
	context.addDiagnostic(Diagnostic(
		context, Diagnostic::Level::Error, "shorthand definition requires pattern",
		Range(line, keywordStart, keywordStart + keyword.size()), "keyword", keyword
	));
	return false;
}

} // namespace

bool expandDefinitionShorthands(ParseContext &context) {
	std::vector<CodeLine *> transformedLines;
	transformedLines.reserve(context.codeLines.size());
	std::unordered_map<lsp::SourceFile *, std::vector<ActiveShorthand>> activeByFile;
	IndentData sourceIndentData{};

	for (CodeLine *line : context.codeLines) {
		if (line->rightTrimmedText.empty()) {
			transformedLines.push_back(line);
			continue;
		}

		size_t indentLength = leadingWhitespaceLength(line->rightTrimmedText);
		std::string_view indent = line->rightTrimmedText.substr(0, indentLength);
		std::string_view measuredIndent = line->hasIndentOverride ? std::string_view(line->indentOverride) : indent;
		IndentMeasurement indentation = measureIndent(sourceIndentData, measuredIndent);
		int sourceIndentLevel = indentation.physicalIndentLevel + line->logicalIndentOffset;
		auto &active = activeByFile[line->sourceFile];
		while (!active.empty() && sourceIndentLevel <= active.back().sourceIndentLevel) {
			active.pop_back();
		}
		int baseLogicalIndent = line->logicalIndentOffset + static_cast<int>(active.size());
		std::string_view text = line->rightTrimmedText.substr(indentLength);
		const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
		std::optional<TopLevelSectionOpener> opener = findTopLevelSectionOpener(text, syntax.sectionOpener);
		if (!opener) {
			line->logicalIndentOffset = baseLogicalIndent;
			transformedLines.push_back(line);
			continue;
		}

		std::string_view beforeOpener = text.substr(0, opener->offset);
		DefinitionShorthand shorthand = DefinitionShorthand::None;
		std::string_view keyword;
		size_t keywordStart = 0;
		size_t patternStart = 0;
		size_t patternEnd = 0;
		bool prefixShorthand = false;
		auto considerPrefix = [&](std::string_view candidate, DefinitionShorthand kind) {
			if (!phraseStartsText(beforeOpener, candidate) || (!keyword.empty() && candidate.size() <= keyword.size()))
				return;
			shorthand = kind;
			keyword = candidate;
			prefixShorthand = true;
			patternStart = candidate.size();
		};
		considerPrefix(syntax.actionShorthand, DefinitionShorthand::Action);
		considerPrefix(syntax.valueShorthand, DefinitionShorthand::Value);
		if (prefixShorthand) {
			while (patternStart < beforeOpener.size() && std::isspace(static_cast<unsigned char>(beforeOpener[patternStart]))) {
				patternStart++;
			}
			patternEnd = trimRight(beforeOpener).size();
		}

		if (!prefixShorthand && !startsExplicitDefinition(beforeOpener, syntax)) {
			std::optional<size_t> replacementStart = suffixPhraseStart(beforeOpener, syntax.replacementShorthand);
			if (replacementStart) {
				shorthand = DefinitionShorthand::Replacement;
				keyword = syntax.replacementShorthand;
				keywordStart = *replacementStart;
				patternStart = 0;
				patternEnd = trimRight(beforeOpener.substr(0, *replacementStart)).size();
			}
		}

		if (shorthand == DefinitionShorthand::None) {
			line->logicalIndentOffset = baseLogicalIndent;
			transformedLines.push_back(line);
			continue;
		}

		keywordStart += indentLength;
		patternStart += indentLength;
		patternEnd += indentLength;
		size_t openerStart = indentLength + opener->offset;
		size_t openerEnd = openerStart + syntax.sectionOpener.size();
		if (patternStart >= patternEnd)
			return reportMissingPattern(context, line, keywordStart, keyword);
		annotatePhrase(context, line, keywordStart, keyword);
		bool hasInlineBody = line->inlineBodyFollows;
		if (shorthand == DefinitionShorthand::Replacement && !hasInlineBody) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "replacement shorthand requires inline body",
				Range(line, keywordStart, keywordStart + keyword.size()), "keyword", keyword
			));
			return false;
		}
		if (shorthand == DefinitionShorthand::Value && hasInlineBody) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "value shorthand requires multiline body",
				Range(line, keywordStart, keywordStart + keyword.size()), "keyword", keyword
			));
			return false;
		}

		transformedLines.push_back(createHeaderLine(
			context, line, indent, patternStart, patternEnd, openerStart, openerEnd, baseLogicalIndent, shorthand
		));
		std::string_view bodySection =
			shorthand == DefinitionShorthand::Replacement ? syntax.replacementSectionName : syntax.executeSectionName;
		transformedLines.push_back(
			createBodySectionLine(context, line, indent, bodySection, syntax.sectionOpener, openerEnd, baseLogicalIndent + 1)
		);
		active.push_back({sourceIndentLevel});
	}

	context.codeLines = std::move(transformedLines);
	return true;
}
