#include "semanticTokenDebug.h"
#include "codeLine.h"
#include "compiler.h"
#include "expression.h"
#include "pattern/patternReference.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "sectionType.h"
#include "semanticTokens.h"
#include "sourceFile.h"
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

static SemanticTokenType classifyExpressionReturnType(const DataType &type) {
	if (type.kind == DataType::Kind::Type)
		return SemanticTokenType::Type;
	return SemanticTokenType::Expression;
}

static SemanticTokenType classifySectionCallTokenType(Section *section, const DataType &resolvedExprType = DataType{}) {
	if (!section)
		return SemanticTokenType::Effect;
	if (section->type == SectionType::Class)
		return SemanticTokenType::Type;
	if (section->type != SectionType::Expression)
		return SemanticTokenType::Effect;

	if (resolvedExprType.kind == DataType::Kind::Type)
		return SemanticTokenType::Type;

	if (!section->instantiations.empty()) {
		const Instantiation &firstInstantiation = section->instantiations.begin()->second;
		if (firstInstantiation.returnType.isDeduced())
			return classifyExpressionReturnType(firstInstantiation.returnType);
	}

	return SemanticTokenType::Expression;
}

static SemanticTokenType getMatchedPatternTokenType(
	const std::vector<PatternDefinition *> &defs, const std::vector<Expression *> &args,
	const std::vector<PatternTreeNode *> &nodesPassed, const DataType &resolvedExprType
) {
	if (defs.empty())
		return SemanticTokenType::Effect;

	std::vector<DataType> argTypes;
	argTypes.reserve(args.size());
	for (const Expression *arg : args)
		argTypes.push_back(arg ? arg->type : DataType{});

	PatternDefinition *matchedDef = selectOverload(defs, args, nodesPassed, argTypes);
	if (!matchedDef || !matchedDef->section)
		matchedDef = defs.front();
	if (!matchedDef || !matchedDef->section)
		return SemanticTokenType::Effect;

	return classifySectionCallTokenType(matchedDef->section, resolvedExprType);
}

static void addPatternReferenceSignatureTokens(
	ParseContext &context, const Range &range, SectionType patternType,
	const std::function<void(const ::Range &, SemanticTokenType, bool)> &addToken
) {
	PatternDefinition *targetDefinition = findDefinitionBySignature(context, patternType, range.subString);
	SemanticTokenType signatureType =
		targetDefinition ? classifySectionCallTokenType(targetDefinition->section) : SemanticTokenType::Expression;

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

std::vector<std::vector<SemanticToken>>
collectSemanticTokens(ParseContext &context, const std::string &uri, int lineCount, bool suppressOnFileErrors) {
	if (suppressOnFileErrors) {
		bool hasErrors = std::any_of(context.diagnostics.begin(), context.diagnostics.end(), [&uri](const ::Diagnostic &d) {
			return d.level == ::Diagnostic::Level::Error && d.range.line && toAbsoluteUri(d.range.line->sourceFile->uri) == uri;
		});
		if (hasErrors)
			return {};
	}

	SemanticTokenBuilder builder(lineCount);
	auto addToken = [&builder, &uri](const ::Range &range, SemanticTokenType type, bool isDefinition) {
		if (toAbsoluteUri(range.line->sourceFile->uri) != uri)
			return;
		int modifiers = isDefinition ? (1 << static_cast<int>(SemanticTokenModifier::Definition)) : 0;
		builder.add(range.line->sourceFileLineIndex, {range.start(), range.end(), type, modifiers});
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

	std::function<void(const Expression *)> tokenizeExpression = [&](const Expression *expr) {
		if (!expr)
			return;
		for (const Expression *arg : expr->arguments)
			tokenizeExpression(arg);
		switch (expr->kind) {
		case Expression::Kind::Literal:
			if (std::holds_alternative<std::string>(expr->literalValue))
				addToken(expr->range, SemanticTokenType::String, false);
			else if (std::holds_alternative<double>(expr->literalValue))
				addToken(expr->range, SemanticTokenType::Number, false);
			break;
		case Expression::Kind::IntrinsicCall:
			addToken(expr->range, SemanticTokenType::Intrinsic, false);
			break;
		default:
			break;
		}
	};

	std::function<void(Section *)> tokenizePatternReferences = [&](Section *section) {
		for (PatternReference *reference : section->patternReferences) {
			if (!reference || !reference->resolved || !reference->match || !reference->match->matchedEndNode)
				continue;
			addToken(
				reference->range(),
				getMatchedPatternTokenType(
					reference->match->matchedEndNode->matchingDefinitions, reference->match->arguments,
					reference->match->nodesPassed, reference->expression ? reference->expression->type : DataType{}
				),
				false
			);
		}
		for (Section *child : section->children)
			tokenizePatternReferences(child);
	};
	tokenizePatternReferences(context.mainSection);

	for (CodeLine *line : context.codeLines) {
		if (toAbsoluteUri(line->sourceFile->uri) != uri || !line->expression)
			continue;
		tokenizeExpression(line->expression);
	}

	for (CodeLine *line : context.codeLines) {
		if (toAbsoluteUri(line->sourceFile->uri) != uri || !line->patternText.starts_with("import "))
			continue;
		std::string_view importKeyword = line->patternText.substr(0, "import"sv.length());
		std::string_view importPath = line->patternText.substr("import "sv.length());
		addToken(::Range(line, importKeyword), SemanticTokenType::Effect, false);
		addToken(::Range(line, importPath), SemanticTokenType::String, false);
	}

	std::function<void(Section *)> tokenizePatternDefinitions = [&](Section *section) {
		for (PatternDefinition *def : section->patternDefinitions)
			addToken(def->range, SemanticTokenType::PatternDefinition, true);
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
		size_t commentPos = line->fullText.find('#');
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
	case SemanticTokenType::Expression:
		return "expression";
	case SemanticTokenType::Effect:
		return "void";
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
