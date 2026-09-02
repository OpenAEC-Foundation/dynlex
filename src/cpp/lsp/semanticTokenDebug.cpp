#include "semanticTokenDebug.h"
#include "codeLine.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "expression.h"
#include "pathUtils.h"
#include "pattern/patternReference.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "sectionType.h"
#include "semanticTokens.h"
#include "sourceFile.h"
#include "syntaxConfig.h"
#include "textDocument.h"
#include "variable.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>

using namespace std::literals;

namespace lsp {

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

	if (resolvedExprType.isMetaType())
		return SemanticTokenType::Type;

	if (!section->instantiations.empty()) {
		const Instantiation &firstInstantiation = section->instantiations.begin()->second;
		if (firstInstantiation.returnType.isDeduced())
			return classifyFunctionReturnType(firstInstantiation.returnType);
	}

	return SemanticTokenType::Function;
}

static SemanticTokenType getPatternCallTokenType(const Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return SemanticTokenType::Function;
	PatternDefinition *definition = expr->selectedPatternDefinition;
	const auto &definitions = expr->patternMatch->matchingDefinitions;
	if (!definition && definitions.size() == 1)
		definition = definitions.front();
	if (!definition || !definition->section)
		return SemanticTokenType::Function;
	return classifySectionCallTokenType(definition->section, expr->type);
}

static int semanticTokenModifiers(bool isDefinition, bool isConstant) {
	int modifiers = 0;
	if (isDefinition)
		modifiers |= 1 << static_cast<int>(SemanticTokenModifier::Definition);
	if (isConstant)
		modifiers |= 1 << static_cast<int>(SemanticTokenModifier::Constant);
	return modifiers;
}

static Section *findOwningVariableSection(Section *fromSection, VariableReference *targetDefinition, const std::string &name) {
	for (Section *section = fromSection; section; section = section->parent) {
		auto it = section->variables.find(name);
		if (it != section->variables.end() && it->second && it->second->definition == targetDefinition)
			return section;
	}
	for (Section *section = fromSection; section; section = section->parent) {
		auto it = section->variables.find(name);
		if (it != section->variables.end() && it->second)
			return section;
	}
	return nullptr;
}

static bool directExpressionHasConstant(const ParseContext &context, const VariableReference *reference) {
	if (!reference)
		return false;
	for (CodeLine *line : context.codeLines) {
		if (!line || !line->expression)
			continue;
		bool found = false;
		visitExpressionTree(line->expression, [&](Expression *expression) {
			found = expression->kind == Expression::Kind::Variable && expression->variable == reference &&
					isCompileTimeKnown(expression->compileTimeValue);
			return found;
		});
		if (found)
			return true;
	}
	return false;
}

static bool hasStoredConstantValue(const ParseContext &context, const VariableReference *reference) {
	if (!reference)
		return false;
	if (directExpressionHasConstant(context, reference))
		return true;
	VariableReference *definition = reference->definition ? reference->definition : const_cast<VariableReference *>(reference);
	if (directExpressionHasConstant(context, definition))
		return true;

	Section *startSection = reference->range.line ? reference->range.line->section : nullptr;
	Section *ownerSection = findOwningVariableSection(startSection, definition, reference->name);
	Section *section = ownerSection ? ownerSection : startSection;
	if (!section)
		return false;
	bool matchesOwnedDefinition = false;
	if (ownerSection) {
		auto variableIt = ownerSection->variables.find(reference->name);
		matchesOwnedDefinition = variableIt != ownerSection->variables.end() && variableIt->second;
	}
	for (const auto &[argTypes, inst] : section->instantiations) {
		(void)argTypes;
		if (inst.body && inst.body->compileTimeValueForReference(reference).has_value())
			return true;
		if (inst.body && inst.body->compileTimeValueForReference(definition).has_value())
			return true;
		if (matchesOwnedDefinition) {
			auto paramIt = inst.constantParameterValues.find(reference->name);
			if (paramIt != inst.constantParameterValues.end() && isCompileTimeKnown(paramIt->second))
				return true;
		}
	}
	return false;
}

static bool isCompileTimeVariableReference(const ParseContext &context, const VariableReference *reference) {
	return hasStoredConstantValue(context, reference);
}

static bool isCompileTimeVariableByName(const ParseContext &context, const ::Range &range) {
	if (!range.line || !range.line->section)
		return false;
	const std::string name = std::string(range.subString);
	Section *owner = findOwningVariableSection(range.line->section, nullptr, name);
	if (!owner)
		return false;
	auto variableIt = owner->variables.find(name);
	if (variableIt == owner->variables.end() || !variableIt->second)
		return false;
	VariableReference *definition = variableIt->second->definition;
	if (!definition)
		return false;
	if (directExpressionHasConstant(context, definition))
		return true;
	for (const auto &[argTypes, inst] : owner->instantiations) {
		(void)argTypes;
		if (inst.body && inst.body->compileTimeValueForReference(definition).has_value())
			return true;
		auto paramIt = inst.constantParameterValues.find(name);
		if (paramIt != inst.constantParameterValues.end() && isCompileTimeKnown(paramIt->second))
			return true;
	}
	return false;
}

static void addPatternReferenceSignatureTokens(
	ParseContext &context, const ::Range &range, SectionType patternType,
	const std::function<void(const ::Range &, SemanticTokenType, bool)> &addToken
) {
	PatternDefinition *targetDefinition =
		findDefinitionBySignature(context, patternType, range.subString, range.line ? range.line->sourceFile : nullptr);
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
			::Range(
				range.line, range.start() + static_cast<int>(element.startPos),
				range.start() + static_cast<int>(element.startPos + element.text.size())
			),
			elementType, false
		);
	}
}

static void addTokenIfNonEmpty(
	const ::Range &baseRange, size_t startOffset, size_t endOffset, SemanticTokenType type, bool isDefinition,
	const std::function<void(const ::Range &, SemanticTokenType, bool)> &addToken
) {
	if (startOffset >= endOffset)
		return;
	addToken(
		::Range(
			baseRange.line, baseRange.start() + static_cast<int>(startOffset), baseRange.start() + static_cast<int>(endOffset)
		),
		type, isDefinition
	);
}

static void addPatternDefinitionTokens(
	const ::Range &range, const std::function<void(const ::Range &, SemanticTokenType, bool)> &addToken
) {
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

static size_t trimRightIndex(std::string_view text) {
	size_t end = text.size();
	while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
		--end;
	return end;
}

std::vector<SemanticToken> collectLiveLineSemanticTokens(
	const ParseContext *context, const TextDocument &document, const std::string &uri, int lineIndex
) {
	std::vector<SemanticToken> tokens;
	if (lineIndex < 0 || lineIndex >= document.lineCount())
		return tokens;

	const SyntaxConfig &syntax = context ? syntaxConfigForSourcePath(*context, uri) : SyntaxConfig{};
	std::string_view line = document.getLine(lineIndex);
	size_t trimmedEnd = trimRightIndex(line);
	std::string_view code = line.substr(0, trimmedEnd);
	size_t commentPos = findCommentStart(code, syntax.commentPrefix);
	if (commentPos != std::string_view::npos)
		code = code.substr(0, commentPos);

	size_t indent = 0;
	while (indent < code.size() && std::isspace(static_cast<unsigned char>(code[indent])))
		++indent;
	std::string_view trimmed = code.substr(indent);

	if (context && context->hasCompleted(ParseContext::CompilationStage::ImportedFiles)) {
		if (std::optional<std::string_view> importPath = extractDirectiveArgument(trimmed, syntax.importKeyword)) {
			tokens.push_back(
				{static_cast<int>(indent), static_cast<int>(indent + syntax.importKeyword.size()), SemanticTokenType::Keyword,
				 0}
			);
			size_t importStart = indent + static_cast<size_t>(importPath->data() - trimmed.data());
			tokens.push_back(
				{static_cast<int>(importStart), static_cast<int>(importStart + importPath->size()), SemanticTokenType::String,
				 0}
			);
		}
	}

	SemanticTokenType wordType = SemanticTokenType::Function;
	if (context && context->hasCompleted(ParseContext::CompilationStage::AnalyzedSections) &&
		trimmed.ends_with(syntax.sectionOpener)) {
		wordType = SemanticTokenType::Section;
	}

	size_t i = 0;
	while (i < code.size()) {
		if (std::isspace(static_cast<unsigned char>(code[i]))) {
			++i;
			continue;
		}
		if (code[i] == '"') {
			size_t start = i++;
			bool escaped = false;
			while (i < code.size()) {
				char c = code[i++];
				if (!escaped && c == '"')
					break;
				if (!escaped && c == '\\') {
					escaped = true;
				} else {
					escaped = false;
				}
			}
			tokens.push_back({static_cast<int>(start), static_cast<int>(std::min(i, code.size())), SemanticTokenType::String, 0}
			);
			continue;
		}
		if (std::isdigit(static_cast<unsigned char>(code[i]))) {
			size_t start = i++;
			while (i < code.size() && (std::isdigit(static_cast<unsigned char>(code[i])) || code[i] == '.'))
				++i;
			tokens.push_back({static_cast<int>(start), static_cast<int>(i), SemanticTokenType::Number, 0});
			continue;
		}
		if (std::isalpha(static_cast<unsigned char>(code[i])) || code[i] == '_') {
			size_t start = i++;
			while (i < code.size() && (std::isalnum(static_cast<unsigned char>(code[i])) || code[i] == '_' || code[i] == '-'))
				++i;
			if (!(context && context->hasCompleted(ParseContext::CompilationStage::ImportedFiles) &&
				  code.substr(start).starts_with(syntax.importKeyword) && i - start == syntax.importKeyword.size())) {
				tokens.push_back({static_cast<int>(start), static_cast<int>(i), wordType, 0});
			}
			continue;
		}
		++i;
	}

	if (commentPos != std::string_view::npos) {
		tokens.push_back({static_cast<int>(commentPos), static_cast<int>(line.size()), SemanticTokenType::Comment, 0});
	}

	return tokens;
}

static std::vector<std::string_view> splitLines(std::string_view text);

std::vector<std::vector<SemanticToken>>
collectSemanticTokens(ParseContext &context, const std::string &uri, int lineCount, bool suppressOnFileErrors) {
	if (!context.hasCompleted(ParseContext::CompilationStage::AnalyzedSections))
		return {};

	if (suppressOnFileErrors) {
		bool hasErrors = std::any_of(context.diagnostics.begin(), context.diagnostics.end(), [&uri](const ::Diagnostic &d) {
			return d.level == ::Diagnostic::Level::Error && d.range.line &&
				   pathutil::toAbsoluteUri(d.range.line->sourceFile->uri) == uri;
		});
		if (hasErrors)
			return {};
	}

	SemanticTokenBuilder builder(lineCount);
	using MappedRange = std::pair<SourceLocation, SourceLocation>;
	auto mappedRangeForUri = [&uri](const ::Range &range) -> std::optional<MappedRange> {
		SourceLocation mappedStart = range.sourceStart();
		SourceLocation mappedEnd = range.sourceEnd();
		if (!mappedStart.sourceFile || !mappedEnd.sourceFile || pathutil::toAbsoluteUri(mappedStart.sourceFile->uri) != uri ||
			pathutil::toAbsoluteUri(mappedEnd.sourceFile->uri) != uri ||
			mappedStart.sourceFileLineIndex != mappedEnd.sourceFileLineIndex)
			return std::nullopt;
		return MappedRange{mappedStart, mappedEnd};
	};
	auto addMappedTokenWithModifiers = [&builder](const MappedRange &mappedRange, SemanticTokenType type, int modifiers) {
		const auto &[mappedStart, mappedEnd] = mappedRange;
		builder.add(mappedStart.sourceFileLineIndex, {mappedStart.column, mappedEnd.column, type, modifiers});
	};
	auto addTokenWithModifiers = [&mappedRangeForUri,
								  &addMappedTokenWithModifiers](const ::Range &range, SemanticTokenType type, int modifiers) {
		std::optional<MappedRange> mappedRange = mappedRangeForUri(range);
		if (!mappedRange)
			return;
		addMappedTokenWithModifiers(*mappedRange, type, modifiers);
	};
	auto addToken = [&addTokenWithModifiers](const ::Range &range, SemanticTokenType type, bool isDefinition) {
		addTokenWithModifiers(range, type, semanticTokenModifiers(isDefinition, false));
	};

	std::function<void(Section *)> tokenizeVariables = [&](Section *section) {
		for (auto &[name, refs] : section->variableReferences) {
			(void)name;
			for (VariableReference *ref : refs) {
				std::optional<MappedRange> mappedRange = mappedRangeForUri(ref->range);
				if (!mappedRange)
					continue;
				addMappedTokenWithModifiers(
					*mappedRange, SemanticTokenType::Variable,
					semanticTokenModifiers(ref->isDefinition(), isCompileTimeVariableReference(context, ref))
				);
			}
		}
		for (Section *child : section->children)
			tokenizeVariables(child);
	};
	tokenizeVariables(context.mainSection);

	for (const ParseContext::SourceTokenAnnotation &annotation : context.sourceTokenAnnotations) {
		if (!mappedRangeForUri(annotation.range))
			continue;
		switch (annotation.kind) {
		case ParseContext::SourceTokenKind::Keyword:
			addToken(annotation.range, SemanticTokenType::Keyword, false);
			break;
		case ParseContext::SourceTokenKind::Variable:
			addTokenWithModifiers(
				annotation.range, SemanticTokenType::Variable,
				semanticTokenModifiers(false, isCompileTimeVariableByName(context, annotation.range))
			);
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
			else if (std::holds_alternative<std::int64_t>(expr->literalValue) ||
					 std::holds_alternative<MinimumSignedIntegerMagnitude>(expr->literalValue) ||
					 std::holds_alternative<double>(expr->literalValue))
				addToken(expr->range, SemanticTokenType::Number, false);
			break;
		case Expression::Kind::IntrinsicCall:
			addToken(expr->range, SemanticTokenType::Intrinsic, false);
			break;
		case Expression::Kind::PatternCall:
			addToken(expr->range, getPatternCallTokenType(expr), false);
			break;
		case Expression::Kind::Variable:
			if (expr->variable) {
				addTokenWithModifiers(
					expr->range, SemanticTokenType::Variable,
					semanticTokenModifiers(
						expr->variable->isDefinition(), isCompileTimeVariableReference(context, expr->variable)
					)
				);
			}
			break;
		default:
			break;
		}
	};

	for (CodeLine *line : context.codeLines) {
		if (pathutil::toAbsoluteUri(line->sourceFile->uri) != uri || !line->expression)
			continue;
		tokenizeExpression(line->expression);
	}

	for (CodeLine *line : context.codeLines) {
		const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
		std::optional<std::string_view> importPath = extractDirectiveArgument(line->patternText, syntax.importKeyword);
		if (pathutil::toAbsoluteUri(line->sourceFile->uri) != uri || !importPath)
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
		if (pathutil::toAbsoluteUri(line->sourceFile->uri) != uri || !line->sectionOpening || line->synthetic)
			continue;
		addToken(::Range(line, line->rightTrimmedText), SemanticTokenType::Section, false);
	}

	lsp::SourceFile *authoredSource = nullptr;
	for (CodeLine *line : context.codeLines) {
		if (line->sourceFile && pathutil::toAbsoluteUri(line->sourceFile->uri) == uri) {
			authoredSource = line->sourceFile;
			break;
		}
	}
	if (!authoredSource)
		return builder.tokenLines();
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, authoredSource);
	std::vector<std::string_view> authoredLines = splitLines(authoredSource->content);
	for (int lineIndex = 0; lineIndex < lineCount && lineIndex < static_cast<int>(authoredLines.size()); lineIndex++) {
		std::string_view line = authoredLines[static_cast<size_t>(lineIndex)];
		size_t commentPos = findCommentStart(line, syntax.commentPrefix);
		if (commentPos == std::string_view::npos)
			continue;
		size_t endPos = line.find_first_of("\r\n", commentPos);
		if (endPos == std::string_view::npos)
			endPos = line.length();
		builder.add(lineIndex, {static_cast<int>(commentPos), static_cast<int>(endPos), SemanticTokenType::Comment, 0});
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

std::string renderTaggedSemanticTokensFromData(std::string_view text, const std::vector<int> &data) {
	std::vector<std::string_view> lines = splitLines(text);
	std::vector<std::vector<SemanticToken>> tokensByLine(lines.size());
	int line = 0;
	int prevChar = 0;

	for (size_t i = 0; i + 4 < data.size(); i += 5) {
		int deltaLine = data[i];
		int deltaChar = data[i + 1];
		int length = data[i + 2];
		int tokenType = data[i + 3];
		int modifiers = data[i + 4];
		line += deltaLine;
		int start = deltaLine == 0 ? prevChar + deltaChar : deltaChar;
		prevChar = start;
		if (line < 0 || line >= static_cast<int>(tokensByLine.size()))
			continue;
		tokensByLine[line].push_back({start, start + length, static_cast<SemanticTokenType>(tokenType), modifiers});
	}

	std::string out;
	for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
		std::string_view fullLine = lines[lineIndex];
		size_t bodyEnd = fullLine.find_last_not_of("\r\n");
		std::string_view body = bodyEnd == std::string_view::npos ? std::string_view{} : fullLine.substr(0, bodyEnd + 1);
		std::string_view ending = bodyEnd == std::string_view::npos ? fullLine : fullLine.substr(bodyEnd + 1);
		std::vector<SemanticToken> lineTokens = tokensByLine[lineIndex];
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

std::string renderTaggedSemanticTokens(ParseContext &context, const std::string &path, bool suppressOnFileErrors) {
	lsp::SourceFile *sourceFile = context.fileSystem ? context.fileSystem->getFile(path) : nullptr;
	if (!sourceFile)
		return {};

	std::vector<std::string_view> lines = splitLines(sourceFile->content);
	std::vector<std::vector<SemanticToken>> tokensByLine =
		collectSemanticTokens(context, pathutil::toAbsoluteUri(path), static_cast<int>(lines.size()), suppressOnFileErrors);
	return renderTaggedSemanticTokensFromData(sourceFile->content, encodeSemanticTokens(tokensByLine));
}

} // namespace lsp
