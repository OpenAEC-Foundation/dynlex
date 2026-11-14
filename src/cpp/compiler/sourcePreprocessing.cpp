#include "sourcePreprocessing.h"
#include "parseContext.h"
#include "section/parseUtils.h"
#include "sourceSyntax.h"
#include "syntaxConfig.h"
#include <cctype>

SectionLineSyntax sectionLineSyntax(const CodeLine &line, const SyntaxConfig &syntax) {
	SectionLineSyntax result;
	if (line.rightTrimmedText.empty())
		return result;
	if (line.hasIndentOverride)
		result.patternStart = line.indentOverride.size();
	else
		while (result.patternStart < line.rightTrimmedText.size() &&
			   std::isspace(static_cast<unsigned char>(line.rightTrimmedText[result.patternStart])))
			result.patternStart++;
	std::string_view text = line.rightTrimmedText.substr(result.patternStart);
	result.opensSection = text.ends_with(syntax.sectionOpener);
	result.patternLength = text.size() - (result.opensSection ? syntax.sectionOpener.size() : 0);
	result.importDirective = !result.opensSection && extractDirectiveArgument(text, syntax.importKeyword).has_value();
	return result;
}

struct InlineBodySplit {
	size_t openerOffset{};
	size_t bodyOffset{};
};

static std::string extractLeadingIndent(std::string_view text) {
	size_t indentLength = 0;
	while (indentLength < text.size() && std::isspace(static_cast<unsigned char>(text[indentLength])))
		indentLength++;
	return std::string(text.substr(0, indentLength));
}

static std::optional<InlineBodySplit> findTopLevelInlineBodySplit(std::string_view text, const SyntaxConfig &syntax) {
	std::optional<TopLevelSectionOpener> opener = findTopLevelSectionOpener(text, syntax.sectionOpener);
	if (!opener || opener->bodyOffset >= text.size())
		return std::nullopt;
	return InlineBodySplit{opener->offset, opener->bodyOffset};
}

static CodeLine *createLogicalInlineLine(
	ParseContext &context, CodeLine *sourceLine, std::string_view indent, std::string_view text, int sourceColumnStart,
	int logicalIndentOffset, bool inlineBodyFollows
) {
	std::string logicalText;
	logicalText.reserve(indent.size() + text.size());
	logicalText += indent;
	logicalText += text;

	std::vector<SourceSlice> sourceSlices;
	sourceLine->appendSourceSlices(sourceSlices, 0, indent.size(), 0);
	sourceLine->appendSourceSlices(
		sourceSlices, sourceColumnStart, sourceColumnStart + text.size(), static_cast<int>(indent.size())
	);

	CodeLine *logicalLine = context.createCodeLine(
		sourceLine->sourceFile, sourceLine->sourceFileLineIndex, std::move(logicalText), std::move(sourceSlices)
	);
	logicalLine->rightTrimmedText = logicalLine->fullText;
	logicalLine->logicalIndentOffset = logicalIndentOffset;
	logicalLine->inlineBodyFollows = inlineBodyFollows;
	logicalLine->hasIndentOverride = !indent.empty();
	logicalLine->indentOverride = std::string(indent);
	return logicalLine;
}

static void expandOneLineSections(ParseContext &context) {
	std::vector<CodeLine *> transformedLines;
	transformedLines.reserve(context.codeLines.size());

	for (CodeLine *line : context.codeLines) {
		if (line->rightTrimmedText.empty()) {
			transformedLines.push_back(line);
			continue;
		}

		const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
		std::string indent = extractLeadingIndent(line->rightTrimmedText);
		std::string_view remaining = line->rightTrimmedText.substr(indent.size());
		size_t remainingSourceColumn = indent.size();
		int logicalIndentOffset = line->logicalIndentOffset;
		bool expanded = false;

		while (std::optional<InlineBodySplit> split = findTopLevelInlineBodySplit(remaining, syntax)) {
			size_t openerEnd = split->openerOffset + syntax.sectionOpener.size();
			transformedLines.push_back(createLogicalInlineLine(
				context, line, indent, remaining.substr(0, openerEnd), static_cast<int>(remainingSourceColumn),
				logicalIndentOffset, true
			));
			remainingSourceColumn += split->bodyOffset;
			remaining = remaining.substr(split->bodyOffset);
			logicalIndentOffset++;
			expanded = true;
		}

		if (!expanded) {
			transformedLines.push_back(line);
			continue;
		}

		transformedLines.push_back(createLogicalInlineLine(
			context, line, indent, remaining, static_cast<int>(remainingSourceColumn), logicalIndentOffset, false
		));
	}

	context.codeLines = std::move(transformedLines);
}

struct BracketContinuationState {
	int parentheses = 0;
	int brackets = 0;
	bool inString = false;
	bool escaped = false;
};

static void advanceContinuationState(std::string_view text, BracketContinuationState &state) {
	for (char ch : text) {
		if (state.inString) {
			if (state.escaped) {
				state.escaped = false;
				continue;
			}
			if (ch == '\\') {
				state.escaped = true;
			} else if (ch == '"') {
				state.inString = false;
			}
			continue;
		}
		if (ch == '"') {
			state.inString = true;
		} else if (ch == '(') {
			state.parentheses++;
		} else if (ch == ')' && state.parentheses > 0) {
			state.parentheses--;
		} else if (ch == '[') {
			state.brackets++;
		} else if (ch == ']' && state.brackets > 0) {
			state.brackets--;
		}
	}
}

static bool needsContinuation(const BracketContinuationState &state) {
	return state.inString || state.parentheses > 0 || state.brackets > 0;
}

static CodeLine *mergeContinuationLines(ParseContext &context, const std::vector<CodeLine *> &lines) {
	if (lines.empty())
		return nullptr;
	if (lines.size() == 1)
		return lines.front();

	std::string mergedText;
	std::vector<SourceSlice> slices;
	for (size_t i = 0; i < lines.size(); i++) {
		std::string_view piece = lines[i]->rightTrimmedText;
		int sourceColumnStart = 0;
		if (i > 0) {
			piece = trimSourceIndent(piece);
			sourceColumnStart = static_cast<int>(piece.begin() - lines[i]->fullText.begin());
			if (!mergedText.empty() && !std::isspace(static_cast<unsigned char>(mergedText.back())) && !piece.empty()) {
				mergedText += ' ';
			}
		}
		if (i == 0)
			sourceColumnStart = static_cast<int>(piece.begin() - lines[i]->fullText.begin());
		int transformedStart = static_cast<int>(mergedText.size());
		mergedText += piece;
		slices.push_back({
			transformedStart,
			static_cast<int>(mergedText.size()),
			lines[i]->sourceFile,
			lines[i]->sourceFileLineIndex,
			sourceColumnStart,
		});
	}

	CodeLine *merged = context.createCodeLine(
		lines.front()->sourceFile, lines.front()->sourceFileLineIndex, std::move(mergedText), std::move(slices)
	);
	merged->rightTrimmedText = merged->fullText;
	return merged;
}

bool preprocessSourceLines(ParseContext &context) {
	std::vector<CodeLine *> input = std::move(context.codeLines), output;
	SourceTransformState state;
	size_t cursor = 0;
	while (cursor < input.size()) {
		if (context.sourceTransformHooks) {
			if (auto consumed = context.sourceTransformHooks->reuse(input, cursor, state, output)) {
				cursor += *consumed;
				continue;
			}
		}
		size_t start = cursor;
		auto *file = input[cursor]->sourceFile;
		std::vector<CodeLine *> grouped;
		do {
			CodeLine *line = input[cursor++];
			if (line->rightTrimmedText.empty()) {
				grouped.push_back(line);
				continue;
			}
			BracketContinuationState continuation;
			advanceContinuationState(line->rightTrimmedText, continuation);
			if (!needsContinuation(continuation)) {
				grouped.push_back(line);
				continue;
			}
			std::vector<CodeLine *> lines{line};
			while (needsContinuation(continuation) && cursor < input.size()) {
				CodeLine *next = input[cursor++];
				lines.push_back(next);
				advanceContinuationState(next->rightTrimmedText, continuation);
			}
			grouped.push_back(mergeContinuationLines(context, lines));
		} while (cursor < input.size() && input[cursor]->sourceFile == file);
		SourceTransformState before;
		before.indentation = state.indentation;
		if (context.sourceTransformHooks)
			for (size_t index = start; index < cursor; index++) {
				auto *source = input[index]->sourceFile;
				auto prior = state.activeByFile.find(source);
				before.activeByFile.try_emplace(source, prior == state.activeByFile.end() ? std::vector<int>{} : prior->second);
			}
		size_t tokens = context.sourceTokenAnnotations.size();
		context.codeLines = std::move(grouped);
		expandOneLineSections(context);
		if (!expandDefinitionShorthands(context, state))
			return false;
		if (context.sourceTransformHooks)
			context.sourceTransformHooks->record(input, start, cursor, before, state, context.codeLines, tokens);
		output.insert(output.end(), context.codeLines.begin(), context.codeLines.end());
	}
	context.codeLines = std::move(output);
	return true;
}
