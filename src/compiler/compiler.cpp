#include "compiler.h"
#include "IndentData.h"
#include "classSection.h"
#include "compileTimeValue.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "pathUtils.h"
#include "pattern/patternReference.h"
#include "pattern/pattern_tree/patternElement.h"
#include "pattern/pattern_tree/patternMatch.h"
#include "pattern/pattern_tree/patternTreeNode.h"
#include "stringFunctions.h"
#include "syntaxConfig.h"
#include "type.h"
#include <cassert>
#include <cctype>
#include <filesystem>
#include <regex>
using namespace std::literals;

// Search paths for library imports (e.g., "lib/std.dl")
// Tries: original path, then installed location, then source tree
static std::string
resolveImportPath(const std::string &path, const std::string &importingFileDir, lsp::FileSystem *fileSystem) {
	// Try relative to the importing file's directory first
	if (!importingFileDir.empty()) {
		std::string relativePath = importingFileDir + "/" + path;
		if (fileSystem->getFile(relativePath)) {
			return relativePath;
		}
	}

	// Try the path as-is (relative to CWD)
	if (fileSystem->getFile(path)) {
		return path;
	}

	// Try relative to the project source directory (for development builds)
	// These come before system paths so dev builds use the source tree's libraries
	std::string devPath = std::string(PROJECT_SOURCE_DIR) + "/" + path;
	if (fileSystem->getFile(devPath)) {
		return devPath;
	}

	// Try project lib directory (e.g., "std.dl" → "<project>/lib/std.dl")
	std::string devLibPath = std::string(PROJECT_SOURCE_DIR) + "/lib/" + path;
	if (fileSystem->getFile(devLibPath)) {
		return devLibPath;
	}

	// Try installed system path
	std::string systemPath = "/usr/share/dynlex/" + path;
	if (fileSystem->getFile(systemPath)) {
		return systemPath;
	}

	// Try installed library path (e.g., "std.dl" → "/usr/share/dynlex/lib/std.dl")
	std::string systemLibPath = "/usr/share/dynlex/lib/" + path;
	if (fileSystem->getFile(systemLibPath)) {
		return systemLibPath;
	}

	return path; // Return original path (will fail with proper error)
}

// regex for line terminators - matches each line including its terminator
const std::regex lineWithTerminatorRegex("([^\r\n]*(?:\r\n|\r|\n))|([^\r\n]+$)");

static void updateLineTrimming(CodeLine *line, const SyntaxConfig &syntax) {
	size_t commentPos = findCommentStart(line->fullText, syntax.commentPrefix);
	std::string_view withoutComment =
		(commentPos != std::string_view::npos) ? line->fullText.substr(0, commentPos) : line->fullText;
	std::cmatch match;
	std::regex_search(withoutComment.begin(), withoutComment.end(), match, std::regex("[\\s]+$"));
	line->rightTrimmedText = match.empty() ? withoutComment : withoutComment.substr(0, match.position());
}

static CodeLine *createMappedLine(
	ParseContext &context, lsp::SourceFile *primarySourceFile, int primarySourceLineIndex, std::string text,
	std::vector<SourceSlice> sourceSlices
) {
	auto line = std::make_unique<CodeLine>(std::string_view{}, primarySourceFile);
	line->sourceFile = primarySourceFile;
	line->sourceFileLineIndex = primarySourceLineIndex;
	line->setOwnedText(std::move(text));
	line->sourceSlices = std::move(sourceSlices);
	CodeLine *result = line.get();
	context.ownedCodeLines.push_back(std::move(line));
	return result;
}

static CodeLine *
createSourceLine(ParseContext &context, lsp::SourceFile *sourceFile, int sourceFileLineIndex, std::string_view text) {
	CodeLine *line = createMappedLine(
		context, sourceFile, sourceFileLineIndex, std::string(text),
		{{0, static_cast<int>(text.size()), sourceFile, sourceFileLineIndex, 0}}
	);
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, sourceFile);
	updateLineTrimming(line, syntax);
	return line;
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
	if (syntax.sectionOpener.empty())
		return std::nullopt;

	int parentheses = 0;
	int brackets = 0;
	int braces = 0;
	bool inString = false;
	bool escaped = false;
	for (size_t index = 0; index + syntax.sectionOpener.size() <= text.size(); index++) {
		char character = text[index];
		if (inString) {
			if (escaped) {
				escaped = false;
				continue;
			}
			if (character == '\\') {
				escaped = true;
			} else if (character == '"') {
				inString = false;
			}
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

		if (parentheses != 0 || brackets != 0 || braces != 0)
			continue;
		if (text.substr(index, syntax.sectionOpener.size()) != syntax.sectionOpener)
			continue;

		size_t patternEnd = text.substr(0, index).find_last_not_of(" \t");
		if (patternEnd == std::string_view::npos)
			continue;

		size_t bodyOffset = index + syntax.sectionOpener.size();
		while (bodyOffset < text.size() && std::isspace(static_cast<unsigned char>(text[bodyOffset])))
			bodyOffset++;
		if (bodyOffset >= text.size())
			return std::nullopt;

		return InlineBodySplit{index, bodyOffset};
	}

	return std::nullopt;
}

static CodeLine *createLogicalInlineLine(
	ParseContext &context, CodeLine *sourceLine, std::string_view indent, std::string_view text, int sourceColumnStart,
	int logicalIndentOffset
) {
	std::string logicalText;
	logicalText.reserve(indent.size() + text.size());
	logicalText += indent;
	logicalText += text;

	std::vector<SourceSlice> sourceSlices;
	if (!indent.empty()) {
		sourceSlices.push_back({0, static_cast<int>(indent.size()), sourceLine->sourceFile, sourceLine->sourceFileLineIndex, 0}
		);
	}
	sourceSlices.push_back({
		static_cast<int>(indent.size()),
		static_cast<int>(indent.size() + text.size()),
		sourceLine->sourceFile,
		sourceLine->sourceFileLineIndex,
		sourceColumnStart,
	});

	CodeLine *logicalLine = createMappedLine(
		context, sourceLine->sourceFile, sourceLine->sourceFileLineIndex, std::move(logicalText), std::move(sourceSlices)
	);
	logicalLine->rightTrimmedText = logicalLine->fullText;
	logicalLine->logicalIndentOffset = logicalIndentOffset;
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
		int logicalIndentOffset = 0;
		bool expanded = false;

		while (std::optional<InlineBodySplit> split = findTopLevelInlineBodySplit(remaining, syntax)) {
			size_t openerEnd = split->openerOffset + syntax.sectionOpener.size();
			transformedLines.push_back(createLogicalInlineLine(
				context, line, indent, remaining.substr(0, openerEnd), static_cast<int>(remainingSourceColumn),
				logicalIndentOffset
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
			context, line, indent, remaining, static_cast<int>(remainingSourceColumn), logicalIndentOffset
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

static std::string_view trimLeadingWhitespace(std::string_view text) {
	size_t first = text.find_first_not_of(" \t");
	return first == std::string_view::npos ? std::string_view{} : text.substr(first);
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
			piece = trimLeadingWhitespace(piece);
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

	CodeLine *merged = createMappedLine(
		context, lines.front()->sourceFile, lines.front()->sourceFileLineIndex, std::move(mergedText), std::move(slices)
	);
	merged->rightTrimmedText = merged->fullText;
	return merged;
}

static void applyBracketContinuations(ParseContext &context) {
	std::vector<CodeLine *> transformedLines;
	for (size_t i = 0; i < context.codeLines.size(); i++) {
		CodeLine *line = context.codeLines[i];
		if (line->rightTrimmedText.empty()) {
			transformedLines.push_back(line);
			continue;
		}

		BracketContinuationState state;
		advanceContinuationState(line->rightTrimmedText, state);
		if (!needsContinuation(state)) {
			transformedLines.push_back(line);
			continue;
		}

		std::vector<CodeLine *> mergedLines = {line};
		while (needsContinuation(state) && i + 1 < context.codeLines.size()) {
			CodeLine *next = context.codeLines[++i];
			mergedLines.push_back(next);
			advanceContinuationState(next->rightTrimmedText, state);
		}
		transformedLines.push_back(mergeContinuationLines(context, mergedLines));
	}
	context.codeLines = std::move(transformedLines);
}

static std::string canonicalSourceKey(std::string_view path) {
	std::filesystem::path fsPath = pathutil::toFilesystemPath(path);
	std::error_code ec;
	std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);
	if (ec) {
		canonical = std::filesystem::absolute(fsPath, ec);
		if (ec)
			return fsPath.lexically_normal().string();
	}
	return canonical.lexically_normal().string();
}

bool isInternalSourcePath(std::string_view path) {
	if (path.empty())
		return false;

	std::string normalized = canonicalSourceKey(path);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');

	const std::string projectLibPrefix = std::string(PROJECT_SOURCE_DIR) + "/lib/";
	return normalized.starts_with("lib/") || normalized.starts_with("/usr/share/dynlex/lib/") ||
		   normalized.starts_with(projectLibPrefix);
}

std::vector<PatternDefinition *>
findDefinitionsBySignature(ParseContext &context, SectionType sectionType, std::string_view signature) {
	std::string converted(signature);
	for (char &c : converted) {
		if (c == '$')
			c = argumentChar;
	}

	auto elements = getPatternElements(converted);
	PatternTreeNode *node = context.patternTrees[(int)sectionType];
	for (const auto &elem : elements) {
		if (!node)
			return {};
		if (elem.type == PatternElement::Type::Variable) {
			node = node->argumentChild;
		} else {
			auto it = node->literalChildren.find(elem.text);
			node = (it != node->literalChildren.end()) ? it->second : nullptr;
		}
	}

	return node ? node->matchingDefinitions : std::vector<PatternDefinition *>{};
}

PatternDefinition *findDefinitionBySignature(ParseContext &context, SectionType sectionType, std::string_view signature) {
	std::vector<PatternDefinition *> matches = findDefinitionsBySignature(context, sectionType, signature);
	return !matches.empty() ? matches[0] : nullptr;
}

static DataType concretizeClassType(DataType type) {
	if (type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex < 0 &&
		!type.classDefinition->instantiations.empty()) {
		type.classInstIndex = 0;
	}
	return type;
}

static bool evaluateCompileTimeInteger(ParseContext &context, Expression *expr, const BindingMap &bindings, int &outValue) {
	CompileTimeValue value = evaluateCompileTimeValue(expr, context, makeBindingFrameStack(bindings));
	auto *number = std::get_if<double>(&value);
	if (!number)
		return false;
	outValue = static_cast<int>(*number);
	return *number == static_cast<double>(outValue);
}

void appendPatternCallBindings(Expression *expr, PatternDefinition *definition, BindingMap &bindings) {
	collectPatternCallBindings(expr, definition, bindings);
}

static bool tryParseIntrinsicTypeReference(Expression *intrinsicExpr, DataType &outTypeRef) {
	if (!intrinsicExpr || intrinsicKind(intrinsicExpr->intrinsicName) != IntrinsicKind::Type ||
		intrinsicExpr->arguments.size() < 2)
		return false;

	Expression *kindExpr = intrinsicExpr->arguments[1];
	auto *kindStr = std::get_if<std::string>(&kindExpr->literalValue);
	if (!kindStr)
		return false;

	DataType typeRef;
	typeRef.kind = DataType::Kind::Type;
	if (*kindStr == "int") {
		typeRef.referencedKind = DataType::Kind::Int;
		typeRef.numericSize = 4;
	} else if (*kindStr == "float") {
		typeRef.referencedKind = DataType::Kind::Float;
		typeRef.numericSize = 8;
	} else if (*kindStr == "bool") {
		typeRef.referencedKind = DataType::Kind::Bool;
	} else if (*kindStr == "void") {
		typeRef.referencedKind = DataType::Kind::Void;
	} else if (*kindStr == "string") {
		typeRef.referencedKind = DataType::Kind::Int;
		typeRef.numericSize = 1;
		typeRef.pointerDepth = 1;
	} else if (*kindStr == "type") {
		typeRef.referencedKind = DataType::Kind::Type;
	} else {
		return false;
	}

	if (intrinsicExpr->arguments.size() > 2) {
		Expression *bitsExpr = intrinsicExpr->arguments[2];
		auto *bits = std::get_if<double>(&bitsExpr->literalValue);
		if (!bits)
			return false;
		typeRef.numericSize = (int)*bits / 8;
	}

	outTypeRef = typeRef;
	return true;
}

static bool
resolveTypeReferenceExpression(ParseContext &context, Expression *expr, const BindingMap &bindings, DataType &outTypeRef);

static Expression *createTemporaryTypeReferenceExpression(Range sourceRange) {
	Expression *expr = new Expression();
	expr->range = sourceRange;
	expr->kind = Expression::Kind::Pending;
	PatternReference *reference = new PatternReference(expr, SectionType::Function);
	expr->patternReference = reference;

	std::string patternSnapshot = reference->pattern.text;
	std::vector<std::tuple<size_t, size_t, std::string>> numMatches;
	for (size_t pos = 0; pos < patternSnapshot.size();) {
		size_t start = pos;
		if (pos > 0) {
			unsigned char prev = static_cast<unsigned char>(patternSnapshot[pos - 1]);
			if (std::isalnum(prev) || prev == '_') {
				pos = start + 1;
				continue;
			}
		}
		if (pos >= patternSnapshot.size() || !std::isdigit(static_cast<unsigned char>(patternSnapshot[pos]))) {
			pos = start + 1;
			continue;
		}
		size_t intStart = pos;
		while (pos < patternSnapshot.size() && std::isdigit(static_cast<unsigned char>(patternSnapshot[pos])))
			pos++;
		if (pos < patternSnapshot.size() && patternSnapshot[pos] == '.') {
			size_t dotPos = pos;
			pos++;
			size_t fracStart = pos;
			while (pos < patternSnapshot.size() && std::isdigit(static_cast<unsigned char>(patternSnapshot[pos])))
				pos++;
			if (fracStart == pos)
				pos = dotPos;
		}
		if (pos < patternSnapshot.size() && std::isalnum(static_cast<unsigned char>(patternSnapshot[pos]))) {
			pos = intStart + 1;
			continue;
		}
		numMatches.emplace_back(intStart, pos, std::string(patternSnapshot.substr(intStart, pos - intStart)));
	}

	std::vector<Expression *> numExprs;
	for (auto it = numMatches.rbegin(); it != numMatches.rend(); ++it) {
		auto &[pos, endPos, numStr] = *it;
		Expression *numExpr = new Expression();
		size_t lineStart = reference->pattern.getLinePos(pos);
		size_t lineEnd = reference->pattern.getLinePos(endPos);
		numExpr->range = sourceRange.subRange(static_cast<int>(lineStart), static_cast<int>(lineEnd));
		numExpr->kind = Expression::Kind::Literal;
		numExpr->literalValue = std::stod(numStr);
		numExprs.push_back(numExpr);
		reference->pattern.replacePattern(pos, endPos);
	}
	std::reverse(numExprs.begin(), numExprs.end());
	for (Expression *numExpr : numExprs)
		expr->arguments.push_back(numExpr);
	expr->arguments = sortArgumentsByPosition(expr->arguments);
	reference->patternElements = getPatternElements(reference->pattern.text);
	return expr;
}

bool resolveTypeConstraintExpression(
	ParseContext &context, Section *section, Range sourceRange, std::string_view typeConstraintExpression, DataType &outTypeRef
) {
	if (!section || !sourceRange.line || typeConstraintExpression.empty())
		return false;

	size_t diagnosticCount = context.diagnostics.size();
	Expression *typeExpr = createTemporaryTypeReferenceExpression(sourceRange);
	auto deleteTemporaryExpressionTree = [](Expression *root) {
		std::unordered_set<Expression *> visited;
		std::function<void(Expression *)> visit = [&](Expression *expression) {
			if (!expression || !visited.insert(expression).second)
				return;
			for (Expression *argument : expression->arguments)
				visit(argument);
			delete expression->patternReference;
			delete expression;
		};
		visit(root);
	};

	if (!typeExpr) {
		context.diagnostics.resize(diagnosticCount);
		return false;
	}

	auto materializeTemporaryVariableReferences =
		[&context](PatternReference *reference, PatternMatch &match, auto &self) -> void {
		int offset = reference->range().start();
		for (VariableMatch &varMatch : match.discoveredVariables) {
			if (varMatch.variableReference)
				continue;
			varMatch.variableReference = context.createVariableReference(
				Range(reference->range().line, offset + varMatch.lineStartPos, offset + varMatch.lineEndPos), varMatch.name
			);
		}
		for (PatternMatch &subMatch : match.subMatches)
			self(reference, subMatch, self);
	};

	std::function<void(Expression *)> prepareMatches = [&](Expression *expression) {
		if (!expression)
			return;
		for (Expression *argument : expression->arguments)
			prepareMatches(argument);
		if (expression->kind != Expression::Kind::Pending || !expression->patternReference)
			return;
		PatternReference *reference = expression->patternReference;
		reference->patternElements = getPatternElements(reference->pattern.text);
		if (!reference->match)
			reference->match = context.match(reference);
		if (reference->match)
			materializeTemporaryVariableReferences(reference, *reference->match, materializeTemporaryVariableReferences);
	};

	prepareMatches(typeExpr);
	if (typeExpr->kind == Expression::Kind::Pending)
		expandPendingTypeReferenceExpression(typeExpr, section);

	bool resolved =
		resolveTypeReferenceExpression(context, typeExpr, {}, outTypeRef) && outTypeRef.kind == DataType::Kind::Type;
	deleteTemporaryExpressionTree(typeExpr);
	if (!resolved)
		context.diagnostics.resize(diagnosticCount);
	return resolved;
}

static bool instantiateClassTypeReference(
	ParseContext &context, ClassDefinition *classDef, const BindingMap &bindings, DataType &outTypeRef
) {
	if (!classDef)
		return false;

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(classDef->fields.size());
	for (FieldDefinition &field : classDef->fields) {
		DataType fieldType = field.declaredType;
		if (fieldType.kind == DataType::Kind::Any) {
			outTypeRef = {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
			return true;
		}
		if (fieldType.kind == DataType::Kind::Unresolved && fieldType.typeExpression) {
			expandPendingTypeReferenceExpression(
				fieldType.typeExpression, field.range.line ? field.range.line->section : nullptr
			);

			DataType fieldTypeRef;
			if (!resolveTypeReferenceExpression(context, fieldType.typeExpression, bindings, fieldTypeRef) ||
				fieldTypeRef.kind != DataType::Kind::Type) {
				outTypeRef = {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
				return true;
			}
			fieldType = concretizeClassType(fieldTypeRef.toReferencedType());
		} else if (fieldType.kind == DataType::Kind::Class && fieldType.classInstIndex < 0) {
			fieldType = concretizeClassType(fieldType);
		}

		if (!fieldType.isDeduced()) {
			outTypeRef = {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
			return true;
		}
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	outTypeRef = {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
	return true;
}

static std::string_view singleTokenPendingName(Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::Pending || !expr->patternReference)
		return {};
	auto &elements = expr->patternReference->patternElements;
	if (elements.empty())
		elements = getPatternElements(expr->patternReference->pattern.text);
	if (elements.size() != 1)
		return {};
	if (elements[0].type != PatternElement::Type::Variable && elements[0].type != PatternElement::Type::VariableLike)
		return {};
	return elements[0].text;
}

static bool
resolveTypeReferenceExpression(ParseContext &context, Expression *expr, const BindingMap &bindings, DataType &outTypeRef) {
	if (!expr)
		return false;

	if (std::string_view pendingName = singleTokenPendingName(expr); !pendingName.empty()) {
		auto it = bindings.find(std::string(pendingName));
		if (it != bindings.end())
			return resolveTypeReferenceExpression(context, it->second, bindings, outTypeRef);
		if (pendingName == "pointer") {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Int;
			outTypeRef.numericSize = 1;
			outTypeRef.pointerDepth = 1;
			return true;
		}
		DataType shorthandType = DataType::fromString(std::string(pendingName));
		if (shorthandType.isDeduced()) {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = shorthandType.kind;
			outTypeRef.numericSize = shorthandType.numericSize;
			outTypeRef.pointerDepth = shorthandType.pointerDepth;
			return true;
		}
	}

	if (expr->kind == Expression::Kind::Variable && expr->variable) {
		auto it = bindings.find(expr->variable->name);
		if (it != bindings.end())
			return resolveTypeReferenceExpression(context, it->second, bindings, outTypeRef);
		if (expr->variable->name == "pointer") {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Int;
			outTypeRef.numericSize = 1;
			outTypeRef.pointerDepth = 1;
			return true;
		}
		DataType shorthandType = DataType::fromString(expr->variable->name);
		if (shorthandType.isDeduced()) {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = shorthandType.kind;
			outTypeRef.numericSize = shorthandType.numericSize;
			outTypeRef.pointerDepth = shorthandType.pointerDepth;
			return true;
		}
		return false;
	}

	if (expr->kind == Expression::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		if (tryParseIntrinsicTypeReference(expr, outTypeRef))
			return true;
		if (kind == IntrinsicKind::AddPointerDepth) {
			DataType innerTypeRef;
			if (!resolveTypeReferenceExpression(context, expr->arguments[1], bindings, innerTypeRef) ||
				innerTypeRef.kind != DataType::Kind::Type)
				return false;
			innerTypeRef.pointerDepth++;
			outTypeRef = innerTypeRef;
			return true;
		}
		if (kind == IntrinsicKind::Array) {
			int arraySize = 0;
			if (!evaluateCompileTimeInteger(context, expr->arguments[1], bindings, arraySize))
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Array;
			outTypeRef.arraySize = arraySize;
			if (expr->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveTypeReferenceExpression(context, expr->arguments[2], bindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		return false;
	}

	if (expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return false;

	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return false;

	PatternDefinition *def = defs.front();
	if (!def || !def->section)
		return false;

	if (!def->section->isMacro && def->section->type == SectionType::Class) {
		auto *classSec = static_cast<ClassSection *>(def->section);
		BindingMap classBindings = bindings;
		appendPatternCallBindings(expr, def, classBindings);
		return instantiateClassTypeReference(context, classSec->classDefinition, classBindings, outTypeRef);
	}

	BindingMap innerBindings;
	Expression *bodyExpr = expandMacroPatternCall(context, expr, innerBindings);
	if (!bodyExpr)
		return false;

	BindingMap mergedBindings = bindings;
	for (const auto &[name, argExpr] : innerBindings)
		mergedBindings[name] = argExpr;
	return resolveTypeReferenceExpression(context, bodyExpr, mergedBindings, outTypeRef);
}

static bool resolveDeclaredClassFieldTypes(ParseContext &context) {
	std::vector<ClassDefinition *> classDefinitions;
	std::function<void(Section *)> collectClasses = [&](Section *section) {
		if (section->type == SectionType::Class) {
			auto *classSec = static_cast<ClassSection *>(section);
			classDefinitions.push_back(classSec->classDefinition);
		}
		for (Section *child : section->children)
			collectClasses(child);
	};
	collectClasses(context.mainSection);

	bool madeProgress = true;
	for (int iteration = 0; iteration < context.options.maxResolutionIterations && madeProgress; iteration++) {
		madeProgress = false;

		for (ClassDefinition *classDef : classDefinitions) {
			for (FieldDefinition &field : classDef->fields) {
				if (field.declaredType.kind == DataType::Kind::Unresolved && field.declaredType.typeExpression) {
					expandPendingTypeReferenceExpression(
						field.declaredType.typeExpression, field.range.line ? field.range.line->section : nullptr
					);
					DataType typeRef;
					if (resolveTypeReferenceExpression(context, field.declaredType.typeExpression, {}, typeRef) &&
						typeRef.kind == DataType::Kind::Type) {
						field.declaredType = concretizeClassType(typeRef.toReferencedType());
						madeProgress = true;
					}
				} else if (field.declaredType.kind == DataType::Kind::Class && field.declaredType.classInstIndex < 0) {
					DataType concretized = concretizeClassType(field.declaredType);
					if (concretized != field.declaredType) {
						field.declaredType = concretized;
						madeProgress = true;
					}
				}
			}
		}

		for (ClassDefinition *classDef : classDefinitions) {
			if (classDef->fields.empty() || !classDef->instantiations.empty())
				continue;

			bool allDeclared = true;
			std::vector<DataType> fieldTypes;
			for (const FieldDefinition &field : classDef->fields) {
				if (!field.declaredType.isDeduced()) {
					allDeclared = false;
					break;
				}
				fieldTypes.push_back(field.declaredType);
			}
			if (allDeclared) {
				classDef->instantiations.push_back({fieldTypes});
				madeProgress = true;
			}
		}
	}

	return true;
}

bool compile(const std::string &path, ParseContext &context) {
	context.compilationStage = ParseContext::CompilationStage::NotStarted;
	if (!initializeSyntaxConfigs(context, path))
		return false;

	if (!importSourceFile(path, context))
		return false;
	applyBracketContinuations(context);
	expandOneLineSections(context);
	context.compilationStage = ParseContext::CompilationStage::ImportedFiles;

	if (!analyzeSections(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::AnalyzedSections;

	if (!resolvePatterns(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::ResolvedPatterns;

	if (!resolveDeclaredClassFieldTypes(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::ResolvedDeclaredTypes;

	if (!validate(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::Validated;

	if (!inferTypes(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::InferredTypes;

	return true;
}

bool importSourceFile(const std::string &path, ParseContext &context) {
	lsp::SourceFile *sourceFile = context.fileSystem->getFile(path);
	if (!sourceFile) {
		if (context.importedFiles.empty()) {
			// If this is the main file, report error
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "could not import main file", Range(), "path", path)
			);
		}
		return false;
	}

	// Canonicalize path to prevent duplicate imports via different path strings
	std::string canonicalPath = canonicalSourceKey(sourceFile->uri.empty() ? path : sourceFile->uri);
	if (context.importedFiles.contains(canonicalPath)) {
		return true; // Already processed, skip
	}

	context.importedFiles[canonicalPath] = sourceFile;
	// The first file imported is the main source file
	if (!context.mainSourceFile)
		context.mainSourceFile = sourceFile;

	// iterate over lines, each match includes the line terminator
	std::string_view fileView{sourceFile->content};
	const SyntaxConfig &syntax = syntaxConfigForSourcePath(context, sourceFile->uri.empty() ? path : sourceFile->uri);

	std::cregex_iterator iter(fileView.begin(), fileView.end(), lineWithTerminatorRegex);
	std::cregex_iterator end;
	int sourceFileLineIndex = 0;
	for (; iter != end; ++iter, ++sourceFileLineIndex) {
		std::string_view lineString = fileView.substr(iter->position(), iter->length());
		CodeLine *line = createSourceLine(context, sourceFile, sourceFileLineIndex, lineString);

		// check if the line is an import statement
		if (std::optional<std::string_view> importPathView =
				extractDirectiveArgument(line->rightTrimmedText, syntax.importKeyword)) {
			// recursively import the file, replacing this line with the imported content
			std::string importingDir =
				std::filesystem::path(pathutil::toFilesystemPath(sourceFile->uri.empty() ? path : sourceFile->uri))
					.parent_path()
					.string();
			std::string importPath = resolveImportPath(std::string(*importPathView), importingDir, context.fileSystem.get());
			if (!importSourceFile(importPath, context)) {
				context.addDiagnostic(Diagnostic(
					context, Diagnostic::Level::Error, "failed to import source file",
					Range(line, static_cast<int>(syntax.importKeyword.length()), line->rightTrimmedText.length()), "path",
					importPath
				));
				return false;
			}
			// Fall through to add import line to codeLines (tokenized during section analysis)
		}
		line->mergedLineIndex = context.codeLines.size();
		context.codeLines.push_back(line);
	}
	return true;
}

// step 2: analyze sections
bool analyzeSections(ParseContext &context) {
	IndentData data{};
	Section *currentSection = context.mainSection = new Section(SectionType::Custom);
	int compiledLineIndex = 0;
	auto closeSection = [&](Section *section, int endLineIndex) {
		section->endLineIndex = endLineIndex;
		return section->finalize(context);
	};
	// code lines are added in import order, meaning lines get replaced with
	// code from imported files. we assume that the indent level of the code of
	// imported files and the import statements both match.
	for (CodeLine *line : context.codeLines) {
		line->mergedLineIndex = compiledLineIndex;
		const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
		// skip empty lines (blank or comment-only) for indent tracking
		if (line->rightTrimmedText.empty()) {
			line->section = currentSection;
			currentSection->codeLines.push_back(line);
			line->resolved = true;
			compiledLineIndex++;
			continue;
		}

		int oldIndentLevel = data.indentLevel;
		// check indent level
		std::string indentString;
		if (line->hasIndentOverride) {
			indentString = line->indentOverride;
		} else {
			std::cmatch match;
			std::regex_search(line->rightTrimmedText.begin(), line->rightTrimmedText.end(), match, std::regex("^(\\s*)"));
			indentString = match[0];
		}
		int physicalIndentLevel = 0;
		if (data.indentString.empty()) {
			data.indentString = indentString;
			physicalIndentLevel = !indentString.empty();
		} else if (indentString.length() % data.indentString.length() != 0) {
			// check amount of indents
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "invalid indentation amount", Range(line, 0, indentString.length()),
				"expected",
				std::to_string(data.indentString.length() * data.indentLevel) + " " + charName(data.indentString[0]) + "s",
				"found", std::to_string(indentString.length())
			));
		}
		// check type of indent. indentation is only important for section
		// exits, since colons determine section starts.
		if (indentString.length()) {
			char expectedIndentChar = data.indentString[0];
			size_t invalidCharIndex = indentString.find_first_not_of(expectedIndentChar);
			if (invalidCharIndex != std::string::npos) {
				context.addDiagnostic(Diagnostic(
					context, Diagnostic::Level::Error, "invalid indentation character",
					Range(line, invalidCharIndex, indentString.length()), "expected", charName(expectedIndentChar) + "s",
					"found", "a " + charName(indentString[invalidCharIndex])
				));
			} else {
				physicalIndentLevel = indentString.length() / data.indentString.length();
			}
		} else {
			data.indentString = "";
			physicalIndentLevel = 0;
		}
		data.indentLevel = physicalIndentLevel + line->logicalIndentOffset;

		if (data.indentLevel != oldIndentLevel) {
			// section change
			if (data.indentLevel > oldIndentLevel) {
				// cannot go up sections twice in a time
				context.addDiagnostic(Diagnostic(
					context, Diagnostic::Level::Error, "invalid indentation increase", Range(line, 0, indentString.length()),
					"expected",
					std::to_string(data.indentString.length() * oldIndentLevel) + " " + charName(data.indentString[0]) + "s",
					"found", std::to_string(indentString.length())
				));

				// fatal for compilation, since no sections will be made
				return false;
			} else {
				// exit some sections
				for (int popIndentLevel = oldIndentLevel; popIndentLevel != data.indentLevel; popIndentLevel--) {
					Section *closingSection = currentSection;
					currentSection = currentSection->parent;
					if (!closeSection(closingSection, compiledLineIndex + 1))
						return false;
				}
			}
		}

		line->section = currentSection;
		currentSection->codeLines.push_back(line);

		std::string_view trimmedText = line->rightTrimmedText.substr(indentString.length());

		// check if this line starts a section
		if (trimmedText.ends_with(syntax.sectionOpener)) {
			line->patternText = trimmedText.substr(0, trimmedText.length() - syntax.sectionOpener.length());

			// set the current section to the new section for the next line
			currentSection = currentSection->createSection(context, line);
			if (!currentSection)
				return false;
			currentSection->startLineIndex = compiledLineIndex + 1;
			currentSection->openingLine = line;
			line->sectionOpening = currentSection;
			data.indentLevel++;
		} else {
			line->patternText = trimmedText;
			if (extractDirectiveArgument(line->patternText, syntax.importKeyword)) {
				// Import lines are already processed during importSourceFile;
				// tokenize "import" as a keyword and the path as a string
				line->resolved = true;
			} else if (line->patternText.length()) {
				currentSection->processLine(context, line);
			} else {
				line->resolved = true;
			}
		}
		++compiledLineIndex;
	}
	while (currentSection != context.mainSection) {
		Section *closingSection = currentSection;
		currentSection = currentSection->parent;
		if (!closeSection(closingSection, compiledLineIndex + 1))
			return false;
	}

	// Create instantiations for class definitions where all fields have declared types
	std::function<void(Section *)> createDeclaredInstantiations = [&](Section *section) {
		if (section->type == SectionType::Class) {
			auto *classSec = static_cast<ClassSection *>(section);
			ClassDefinition *classDef = classSec->classDefinition;
			if (!classDef->fields.empty() && classDef->instantiations.empty()) {
				bool allDeclared = true;
				std::vector<DataType> fieldTypes;
				for (const auto &field : classDef->fields) {
					if (!field.declaredType.isDeduced()) {
						allDeclared = false;
						break;
					}
					fieldTypes.push_back(field.declaredType);
				}
				if (allDeclared) {
					classDef->instantiations.push_back({fieldTypes});
				}
			}
		}
		for (Section *child : section->children)
			createDeclaredInstantiations(child);
	};
	createDeclaredInstantiations(context.mainSection);

	return true;
}

bool isArithmeticOperator(const std::string &name) { return isArithmeticIntrinsic(arithmeticIntrinsicKind(name)); }

bool isPointerArithmeticOperator(const std::string &name) {
	return isPointerArithmeticIntrinsic(arithmeticIntrinsicKind(name));
}

bool isComparisonOperator(const std::string &name) { return isComparisonIntrinsicKind(intrinsicKind(name)); }

bool isMathFunction(const std::string &name) {
	const IntrinsicInfo *info = findIntrinsic(name);
	return info && info->returnKind == IntrinsicReturnKind::SameAsArgs && !isArithmeticOperator(name) &&
		   intrinsicKind(name) != IntrinsicKind::Negate && intrinsicKind(name) != IntrinsicKind::Min &&
		   intrinsicKind(name) != IntrinsicKind::Max;
}

PatternDefinition *selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> & /*sortedArgs*/,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes
) {
	if (definitions.size() <= 1)
		return definitions.empty() ? nullptr : definitions[0];

	// Score each candidate: count how many type constraints match
	PatternDefinition *best = nullptr;
	int bestScore = -1;

	for (auto *candidate : definitions) {
		int score = 0;
		bool constraintFailed = false;

		size_t argIdx = 0;
		forEachPatternParameterName(nodesPassed, candidate, [&](const std::string &paramName, PatternTreeNode *) {
			if (constraintFailed || argIdx >= argTypes.size()) {
				argIdx++;
				return;
			}
			// Find the corresponding element in the candidate's definition to get its type constraint
			for (auto &elem : candidate->patternElements) {
				if (elem.type == PatternElement::Type::Variable && elem.text == paramName) {
					const DataType &argType = argTypes[argIdx];
					if (argType.kind == DataType::Kind::Void) {
						bool acceptsVoid = elem.resolvedTypeConstraint.isDeduced() &&
										   elem.resolvedTypeConstraint.kind == DataType::Kind::Void &&
										   elem.resolvedTypeConstraint.pointerDepth == 0;
						if (acceptsVoid) {
							score++;
						} else {
							constraintFailed = true;
						}
						break;
					}
					if (elem.resolvedTypeConstraint.isDeduced()) {
						// DataType constraint exists — check if argument type matches
						const DataType &constraint = elem.resolvedTypeConstraint;
						if (!argType.isDeduced()) {
							// Keep candidate viable until the argument type is known.
							break;
						}
						bool matches = false;
						if (constraint.kind == DataType::Kind::Class) {
							matches =
								argType.kind == DataType::Kind::Class && argType.classDefinition == constraint.classDefinition;
						} else if (constraint.kind == DataType::Kind::Type) {
							// {type:x} accepts any compile-time type value, including pointer/array/etc. type refs.
							matches = argType.kind == DataType::Kind::Type;
						} else if (constraint.isNumeric()) {
							// Numeric constraint: match exact kind (Int or Float) and pointer depth
							// {integer:x} matches Int, {float:x} matches Float
							matches = argType.kind == constraint.kind && argType.pointerDepth == 0;
						} else {
							// Other primitive type constraint: match kind and pointer depth
							matches = argType.kind == constraint.kind && argType.pointerDepth == constraint.pointerDepth;
						}
						if (matches) {
							score++;
						} else {
							constraintFailed = true;
						}
					}
					break;
				}
			}
			argIdx++;
		});

		if (constraintFailed)
			continue;

		if (score > bestScore) {
			bestScore = score;
			best = candidate;
		}
	}

	return best;
}
