#include "semanticTokenDebug.h"
#include "codeLine.h"
#include "compiler.h"
#include "function.h"
#include "pattern/patternReference.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "sectionType.h"
#include "semanticTokens.h"
#include "sourceFile.h"
#include "syntaxConfig.h"
#include <algorithm>
#include <filesystem>
#include <string_view>

using namespace std::literals;

namespace lsp {

static std::string toAbsoluteUri(const std::string &uri) {
	if (uri.starts_with("file://"))
		return uri;
	return "file://" + std::filesystem::absolute(uri).string();
}

static SemanticTokenType classifyFunctionReturnType(const DataType &type) {
	if (type.kind == DataType::Kind::Type)
		return SemanticTokenType::Type;
	return SemanticTokenType::Function;
}

static SemanticTokenType classifySectionCallTokenType(Section *section, const DataType &resolvedExprType = DataType{}) {
	if (!section)
		return SemanticTokenType::Function;
	if (section->type == SectionType::Class)
		return SemanticTokenType::Type;
	if (section->type != SectionType::Function)
		return SemanticTokenType::Section;

	if (resolvedExprType.kind == DataType::Kind::Type)
		return SemanticTokenType::Type;

	if (!section->instantiations.empty()) {
		const Instantiation &firstInstantiation = section->instantiations.begin()->second;
		if (firstInstantiation.returnType.isDeduced())
			return classifyFunctionReturnType(firstInstantiation.returnType);
	}

	return SemanticTokenType::Function;
}

static SemanticTokenType getMatchedPatternTokenType(
	const std::vector<PatternDefinition *> &defs, const std::vector<Function *> &args,
	const std::vector<PatternTreeNode *> &nodesPassed, const DataType &resolvedExprType
) {
	if (defs.empty())
		return SemanticTokenType::Function;

	std::vector<DataType> argTypes;
	argTypes.reserve(args.size());
	for (const Function *arg : args)
		argTypes.push_back(arg ? arg->type : DataType{});

	PatternDefinition *matchedDef = selectOverload(defs, args, nodesPassed, argTypes);
	if (!matchedDef || !matchedDef->section)
		matchedDef = defs.front();
	if (!matchedDef || !matchedDef->section)
		return SemanticTokenType::Function;

	return classifySectionCallTokenType(matchedDef->section, resolvedExprType);
}

static SemanticTokenType getPatternCallTokenType(const Function *expr) {
	if (!expr || expr->kind != Function::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return SemanticTokenType::Function;

	return getMatchedPatternTokenType(
		expr->patternMatch->matchedEndNode->matchingDefinitions, expr->arguments, expr->patternMatch->nodesPassed, expr->type
	);
}

static void addPatternReferenceSignatureTokens(
	ParseContext &context, const Range &range, SectionType patternType,
	const std::function<void(const ::Range &, SemanticTokenType, bool)> &addToken
) {
	PatternDefinition *targetDefinition = findDefinitionBySignature(context, patternType, range.subString);
	SemanticTokenType signatureType =
		targetDefinition ? classifySectionCallTokenType(targetDefinition->section) : SemanticTokenType::Function;

	std::string converted(range.subString);
	for (char &c : converted) {
		if (c == '$')
			c = argumentChar;
	}

	for (const PatternElement &element : getPatternElements(converted)) {
		SemanticTokenType elementType =
			element.type == PatternElement::Type::Variable ? SemanticTokenType::Variable : signatureType;
		addToken(
			Range(
				range.line, range.start() + static_cast<int>(element.startPos),
				range.start() + static_cast<int>(element.startPos + element.text.size())
			),
			elementType, false
		);
	}
}

static void addTokenIfNonEmpty(
	const Range &baseRange, size_t startOffset, size_t endOffset, SemanticTokenType type, bool isDefinition,
	const std::function<void(const ::Range &, SemanticTokenType, bool)> &addToken
) {
	if (startOffset >= endOffset)
		return;
	addToken(
		Range(
			baseRange.line, baseRange.start() + static_cast<int>(startOffset), baseRange.start() + static_cast<int>(endOffset)
		),
		type, isDefinition
	);
}

static void
addPatternDefinitionTokens(const Range &range, const std::function<void(const ::Range &, SemanticTokenType, bool)> &addToken) {
	std::string_view text = range.subString;
	size_t pos = 0;

	while (pos < text.size()) {
		size_t open = text.find('{', pos);
		if (open == std::string_view::npos) {
			addTokenIfNonEmpty(range, pos, text.size(), SemanticTokenType::PatternDefinition, true, addToken);
			return;
		}

		size_t close = text.find('}', open + 1);
		if (close == std::string_view::npos) {
			addTokenIfNonEmpty(range, pos, text.size(), SemanticTokenType::PatternDefinition, true, addToken);
			return;
		}

		size_t colon = text.find(':', open + 1);
		if (colon == std::string_view::npos || colon > close) {
			addTokenIfNonEmpty(range, pos, close + 1, SemanticTokenType::PatternDefinition, true, addToken);
			pos = close + 1;
			continue;
		}

		std::string_view captureType = text.substr(open + 1, colon - open - 1);
		bool isTypedCapture = !captureType.empty() && captureType != "word";

		addTokenIfNonEmpty(range, pos, open + 1, SemanticTokenType::PatternDefinition, true, addToken);
		if (isTypedCapture)
			addTokenIfNonEmpty(range, open + 1, colon, SemanticTokenType::Type, false, addToken);
		else
			addTokenIfNonEmpty(range, open + 1, colon, SemanticTokenType::PatternDefinition, true, addToken);
		addTokenIfNonEmpty(range, colon, colon + 1, SemanticTokenType::PatternDefinition, true, addToken);
		addTokenIfNonEmpty(range, colon + 1, close, SemanticTokenType::Variable, false, addToken);
		addTokenIfNonEmpty(range, close, close + 1, SemanticTokenType::PatternDefinition, true, addToken);
		pos = close + 1;
	}
}

std::vector<std::vector<SemanticToken>>
collectSemanticTokens(ParseContext &context, const std::string &uri, int lineCount, bool suppressOnFileErrors) {
	if (!context.hasCompleted(ParseContext::CompilationStage::AnalyzedSections))
		return {};

	if (suppressOnFileErrors) {
		bool hasErrors = std::any_of(context.diagnostics.begin(), context.diagnostics.end(), [&uri](const ::Diagnostic &d) {
			return d.level == ::Diagnostic::Level::Error && d.range.line && toAbsoluteUri(d.range.line->sourceFile->uri) == uri;
		});
		if (hasErrors)
			return {};
	}

	SemanticTokenBuilder builder(lineCount);
	auto addToken = [&builder, &uri](const ::Range &range, SemanticTokenType type, bool isDefinition) {
		SourceLocation mappedStart = range.sourceStart();
		SourceLocation mappedEnd = range.sourceEnd();
		if (!mappedStart.sourceFile || !mappedEnd.sourceFile)
			return;
		if (toAbsoluteUri(mappedStart.sourceFile->uri) != uri || toAbsoluteUri(mappedEnd.sourceFile->uri) != uri)
			return;
		if (mappedStart.sourceFileLineIndex != mappedEnd.sourceFileLineIndex)
			return;
		int modifiers = isDefinition ? (1 << static_cast<int>(SemanticTokenModifier::Definition)) : 0;
		builder.add(mappedStart.sourceFileLineIndex, {mappedStart.column, mappedEnd.column, type, modifiers});
	};

	std::function<void(Section *)> tokenizeVariables = [&](Section *section) {
		for (auto &[name, refs] : section->variableReferences) {
			(void)name;
			for (VariableReference *ref : refs)
				addToken(ref->range, SemanticTokenType::Variable, ref->isDefinition());
		}
		for (Section *child : section->children)
			tokenizeVariables(child);
	};
	tokenizeVariables(context.mainSection);

	for (const ParseContext::SourceTokenAnnotation &annotation : context.sourceTokenAnnotations) {
		switch (annotation.kind) {
		case ParseContext::SourceTokenKind::Keyword:
			addToken(annotation.range, SemanticTokenType::Keyword, false);
			break;
		case ParseContext::SourceTokenKind::Variable:
			addToken(annotation.range, SemanticTokenType::Variable, false);
			break;
		case ParseContext::SourceTokenKind::Number:
			addToken(annotation.range, SemanticTokenType::Number, false);
			break;
		case ParseContext::SourceTokenKind::PatternReference:
			addPatternReferenceSignatureTokens(context, annotation.range, annotation.referencedPatternType, addToken);
			break;
		}
	}

	std::function<void(const Function *)> tokenizeFunction = [&](const Function *expr) {
		if (!expr)
			return;
		for (const Function *arg : expr->arguments)
			tokenizeFunction(arg);
		switch (expr->kind) {
		case Function::Kind::Literal:
			if (std::holds_alternative<std::string>(expr->literalValue))
				addToken(expr->range, SemanticTokenType::String, false);
			else if (std::holds_alternative<double>(expr->literalValue))
				addToken(expr->range, SemanticTokenType::Number, false);
			break;
		case Function::Kind::IntrinsicCall:
			addToken(expr->range, SemanticTokenType::Intrinsic, false);
			break;
		case Function::Kind::PatternCall:
			addToken(expr->range, getPatternCallTokenType(expr), false);
			break;
		default:
			break;
		}
	};

	for (CodeLine *line : context.codeLines) {
		if (toAbsoluteUri(line->sourceFile->uri) != uri || !line->function)
			continue;
		tokenizeFunction(line->function);
	}

	for (CodeLine *line : context.codeLines) {
		const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
		std::optional<std::string_view> importPath = extractDirectiveArgument(line->patternText, syntax.importKeyword);
		if (toAbsoluteUri(line->sourceFile->uri) != uri || !importPath)
			continue;
		std::string_view importKeyword = line->patternText.substr(0, syntax.importKeyword.length());
		addToken(::Range(line, importKeyword), SemanticTokenType::Keyword, false);
		addToken(::Range(line, *importPath), SemanticTokenType::String, false);
	}

	std::function<void(Section *)> tokenizePatternDefinitions = [&](Section *section) {
		for (PatternDefinition *def : section->patternDefinitions)
			addPatternDefinitionTokens(def->range, addToken);
		for (Section *child : section->children)
			tokenizePatternDefinitions(child);
	};
	tokenizePatternDefinitions(context.mainSection);

	for (CodeLine *line : context.codeLines) {
		if (toAbsoluteUri(line->sourceFile->uri) != uri || !line->sectionOpening)
			continue;
		addToken(::Range(line, line->rightTrimmedText), SemanticTokenType::Section, false);
	}

	for (CodeLine *line : context.codeLines) {
		if (toAbsoluteUri(line->sourceFile->uri) != uri)
			continue;
		const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
		size_t commentPos = findCommentStart(line->fullText, syntax.commentPrefix);
		if (commentPos == std::string_view::npos)
			continue;
		size_t endPos = line->fullText.find_first_of("\r\n", commentPos);
		if (endPos == std::string_view::npos)
			endPos = line->fullText.length();
		builder.add(
			line->sourceFileLineIndex, {static_cast<int>(commentPos), static_cast<int>(endPos), SemanticTokenType::Comment, 0}
		);
	}

	return builder.tokenLines();
}

std::vector<int> encodeSemanticTokens(const std::vector<std::vector<SemanticToken>> &tokensByLine) {
	std::vector<int> data;
	int prevLine = 0;
	int prevChar = 0;

	for (int line = 0; line < static_cast<int>(tokensByLine.size()); ++line) {
		std::vector<SemanticToken> lineTokens = tokensByLine[line];
		std::sort(lineTokens.begin(), lineTokens.end(), [](const SemanticToken &a, const SemanticToken &b) {
			return a.start < b.start;
		});
		for (const SemanticToken &t : lineTokens) {
			int deltaLine = line - prevLine;
			int deltaChar = (deltaLine == 0) ? (t.start - prevChar) : t.start;
			data.push_back(deltaLine);
			data.push_back(deltaChar);
			data.push_back(t.end - t.start);
			data.push_back(static_cast<int>(t.type));
			data.push_back(t.modifiers);
			prevLine = line;
			prevChar = t.start;
		}
	}

	return data;
}

static std::string tokenTagName(SemanticTokenType type) {
	switch (type) {
	case SemanticTokenType::Function:
		return "function";
	case SemanticTokenType::Section:
		return "section";
	case SemanticTokenType::Variable:
		return "variable";
	case SemanticTokenType::Comment:
		return "comment";
	case SemanticTokenType::PatternDefinition:
		return "patternDefinition";
	case SemanticTokenType::Number:
		return "number";
	case SemanticTokenType::String:
		return "string";
	case SemanticTokenType::Intrinsic:
		return "intrinsic";
	case SemanticTokenType::Type:
		return "type";
	case SemanticTokenType::Keyword:
		return "keyword";
	case SemanticTokenType::Count:
		break;
	}
	return "unknown";
}

static std::string escapeTaggedText(std::string_view text) {
	std::string escaped;
	for (char c : text) {
		switch (c) {
		case '&':
			escaped += "&amp;";
			break;
		case '<':
			escaped += "&lt;";
			break;
		case '>':
			escaped += "&gt;";
			break;
		default:
			escaped += c;
			break;
		}
	}
	return escaped;
}

static std::vector<std::string_view> splitLines(std::string_view text) {
	std::vector<std::string_view> lines;
	size_t start = 0;
	while (start < text.size()) {
		size_t end = start;
		while (end < text.size() && text[end] != '\n' && text[end] != '\r')
			end++;
		if (end < text.size()) {
			if (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n')
				end += 2;
			else
				end += 1;
		}
		lines.push_back(text.substr(start, end - start));
		start = end;
	}
	if (text.empty())
		lines.push_back({});
	return lines;
}

std::string renderTaggedSemanticTokens(ParseContext &context, const std::string &path, bool suppressOnFileErrors) {
	lsp::SourceFile *sourceFile = context.fileSystem ? context.fileSystem->getFile(path) : nullptr;
	if (!sourceFile)
		return {};

	std::vector<std::string_view> lines = splitLines(sourceFile->content);
	std::vector<std::vector<SemanticToken>> tokensByLine =
		collectSemanticTokens(context, toAbsoluteUri(path), static_cast<int>(lines.size()), suppressOnFileErrors);

	std::string out;
	for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
		std::string_view fullLine = lines[lineIndex];
		size_t bodyEnd = fullLine.find_last_not_of("\r\n");
		std::string_view body = bodyEnd == std::string_view::npos ? std::string_view{} : fullLine.substr(0, bodyEnd + 1);
		std::string_view ending = bodyEnd == std::string_view::npos ? fullLine : fullLine.substr(bodyEnd + 1);
		std::vector<SemanticToken> lineTokens =
			lineIndex < tokensByLine.size() ? tokensByLine[lineIndex] : std::vector<SemanticToken>{};
		std::sort(lineTokens.begin(), lineTokens.end(), [](const SemanticToken &a, const SemanticToken &b) {
			return a.start < b.start;
		});

		size_t pos = 0;
		for (const SemanticToken &token : lineTokens) {
			size_t start = std::clamp(token.start, 0, static_cast<int>(body.size()));
			size_t end = std::clamp(token.end, 0, static_cast<int>(body.size()));
			if (pos < start)
				out += escapeTaggedText(body.substr(pos, start - pos));
			if (start < end) {
				std::string tag = tokenTagName(token.type);
				out += "<" + tag + ">" + escapeTaggedText(body.substr(start, end - start)) + "</" + tag + ">";
			}
			pos = std::max(pos, end);
		}
		if (pos < body.size())
			out += escapeTaggedText(body.substr(pos));
		out += std::string(ending);
	}

	return out;
}

} // namespace lsp
