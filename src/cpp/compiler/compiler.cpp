#include "compiler.h"
#include "IndentData.h"
#include "classSection.h"
#include "codegen/codegen.h"
#include "compileTimeValue.h"
#include "compilerUtils.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "pathUtils.h"
#include "pattern/patternDefinition.h"
#include "pattern/patternReference.h"
#include "pattern/pattern_tree/patternElement.h"
#include "pattern/pattern_tree/patternMatch.h"
#include "pattern/pattern_tree/patternTreeNode.h"
#include "pattern/transformedPattern.h"
#include "section/variable.h"
#include "stringFunctions.h"
#include "syntaxConfig.h"
#include "type.h"
#include <cassert>
#include <cctype>
#include <climits>
#include <cmath>
#include <filesystem>
using namespace std::literals;

// A resolved import: the path to load plus the resolution root the file was
// found under. Nested imports of that file first try the same root, so one
// import graph never mixes files from different library locations.
struct ResolvedImport {
	std::string path;
	std::string root;
};

static std::string currentResolutionRoot() {
	std::error_code error;
	std::filesystem::path current = std::filesystem::current_path(error);
	return error ? std::string{} : current.lexically_normal().string();
}

static std::string importPathUnder(std::string_view root, std::string_view path) {
	return (std::filesystem::path(pathutil::toFilesystemPath(root)) / std::filesystem::path(pathutil::toFilesystemPath(path)))
		.lexically_normal()
		.string();
}

// Search paths for library imports (e.g., "lib/std.dl")
// Tries: the importing file's directory, the importing file's resolution root,
// the working directory, then the source tree and installed locations.
static ResolvedImport resolveImportPath(
	const std::string &path, const std::string &importingFileDir, const std::string &importingFileRoot,
	lsp::FileSystem *fileSystem
) {
	auto resolveCandidate = [fileSystem](const std::string &candidate, std::string &resolved) -> bool {
		if (candidate.empty())
			return false;
		std::string normalized = std::filesystem::path(pathutil::toFilesystemPath(candidate)).lexically_normal().string();
		if (!fileSystem->getFile(normalized))
			return false;
		resolved = std::move(normalized);
		return true;
	};

	std::string resolvedPath;

	// Try relative to the importing file's directory first; the sibling stays
	// within the importer's resolution root.
	if (!importingFileDir.empty()) {
		std::string relativePath = importPathUnder(importingFileDir, path);
		if (resolveCandidate(relativePath, resolvedPath))
			return {resolvedPath, importingFileRoot};
	}

	// Try the importing file's resolution root, keeping the import graph on
	// one consistent tree even when it differs from the working directory.
	if (!importingFileRoot.empty()) {
		std::string rootPath = importPathUnder(importingFileRoot, path);
		if (resolveCandidate(rootPath, resolvedPath))
			return {resolvedPath, importingFileRoot};
	}

	// Try the path as-is (relative to CWD)
	if (resolveCandidate(path, resolvedPath)) {
		std::filesystem::path requestedPath = pathutil::toFilesystemPath(path);
		std::string foundRoot =
			requestedPath.is_absolute() ? requestedPath.parent_path().lexically_normal().string() : currentResolutionRoot();
		return {resolvedPath, foundRoot};
	}

#ifdef DYNLEX_WEB
	// Browser mode uses a virtual root. Keep imports deterministic for in-memory and preloaded files.
	if (!path.empty() && path[0] != '/') {
		if (resolveCandidate(importPathUnder("/", path), resolvedPath))
			return {resolvedPath, "/"};
		if (resolveCandidate(importPathUnder("/workspace", path), resolvedPath))
			return {resolvedPath, "/workspace"};
		if (resolveCandidate(importPathUnder("/lib", path), resolvedPath))
			return {resolvedPath, "/"};
	}
#endif

	// Try relative to the project source directory (for development builds)
	// These come before system paths so dev builds use the source tree's libraries
	std::string devPath = importPathUnder(PROJECT_SOURCE_DIR, path);
	if (resolveCandidate(devPath, resolvedPath))
		return {resolvedPath, std::string(PROJECT_SOURCE_DIR)};

	// Try project lib directory (e.g., "std.dl" → "<project>/lib/std.dl")
	std::string devLibPath = importPathUnder(importPathUnder(PROJECT_SOURCE_DIR, "lib"), path);
	if (resolveCandidate(devLibPath, resolvedPath))
		return {resolvedPath, std::string(PROJECT_SOURCE_DIR)};

	// Try installed system path
	std::string systemPath = importPathUnder("/usr/share/dynlex", path);
	if (resolveCandidate(systemPath, resolvedPath))
		return {resolvedPath, "/usr/share/dynlex"};

	// Try installed library path (e.g., "std.dl" → "/usr/share/dynlex/lib/std.dl")
	std::string systemLibPath = importPathUnder("/usr/share/dynlex/lib", path);
	if (resolveCandidate(systemLibPath, resolvedPath))
		return {resolvedPath, "/usr/share/dynlex"};

	return {path, importingFileRoot}; // Return original path (will fail with proper error)
}

static void collectSectionsPreorder(Section *section, std::vector<Section *> &outSections) {
	if (!section)
		return;
	outSections.push_back(section);
	for (Section *child : section->children)
		collectSectionsPreorder(child, outSections);
}

static std::string formatCompileTimeValueForPurityReport(const CompileTimeValue &value) {
	if (std::holds_alternative<std::monostate>(value))
		return "?";
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return std::to_string(*integer);
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return "9223372036854775808";
	if (const auto *number = std::get_if<double>(&value)) {
		double integralPart = 0.0;
		if (std::modf(*number, &integralPart) == 0.0)
			return std::to_string(static_cast<long long>(integralPart));
		return std::to_string(*number);
	}
	if (const auto *text = std::get_if<std::string>(&value))
		return "\"" + *text + "\"";
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "true" : "false";
	if (const auto *type = std::get_if<TypeReferenceValue>(&value)) {
		return type->type.kind == DataType::Kind::Type ? typeToUserName(type->type.toReferencedType())
													   : typeToUserName(type->type);
	}
	if (const auto *constraint = std::get_if<TypeConstraint>(&value))
		return typeToUserName(*constraint);
	crashCompilerBug("unknown compile-time value alternative in purity report");
}

static bool purityReportSectionComesBefore(Section *left, Section *right) {
	PatternDefinition *leftDefinition =
		(left && !left->patternDefinitions.empty()) ? left->patternDefinitions.front() : nullptr;
	PatternDefinition *rightDefinition =
		(right && !right->patternDefinitions.empty()) ? right->patternDefinitions.front() : nullptr;
	if (leftDefinition == rightDefinition)
		return left < right;
	if (!leftDefinition)
		return false;
	if (!rightDefinition)
		return true;
	CodeLine *leftLine = leftDefinition->range.line;
	CodeLine *rightLine = rightDefinition->range.line;
	std::string leftUri = (leftLine && leftLine->sourceFile) ? leftLine->sourceFile->uri : "";
	std::string rightUri = (rightLine && rightLine->sourceFile) ? rightLine->sourceFile->uri : "";
	if (leftUri != rightUri)
		return leftUri < rightUri;
	int leftLineIndex = leftLine ? leftLine->sourceFileLineIndex : INT_MAX;
	int rightLineIndex = rightLine ? rightLine->sourceFileLineIndex : INT_MAX;
	if (leftLineIndex != rightLineIndex)
		return leftLineIndex < rightLineIndex;
	if (leftDefinition->range.start() != rightDefinition->range.start())
		return leftDefinition->range.start() < rightDefinition->range.start();
	return left < right;
}

std::string renderPurityReport(ParseContext &context) {
	std::vector<Section *> sections;
	collectSectionsPreorder(context.mainSection, sections);
	std::vector<Section *> functionSections;
	for (Section *section : sections) {
		if (!section || section->type != SectionType::Function || section->isFlex || section->patternDefinitions.empty() ||
			section->instantiations.empty())
			continue;
		functionSections.push_back(section);
	}
	std::sort(functionSections.begin(), functionSections.end(), purityReportSectionComesBefore);
	std::string report;
	for (Section *section : functionSections) {
		PatternDefinition *definition = section->patternDefinitions.front();
		for (const auto &[key, instantiation] : section->instantiations) {
			if (!report.empty())
				report += '\n';
			report += instantiation.purity == InstantiationPurity::Pure ? "pure" : "impure";
			report += " | ";
			report += definition->toString();
			report += " | args=[";
			for (size_t i = 0; i < key.argumentTypes.size(); i++) {
				if (i > 0)
					report += ", ";
				report += typeToUserName(key.argumentTypes[i]);
			}
			report += "]";
			if (!key.compileTimeParameters.empty()) {
				report += " | fixed=[";
				for (size_t i = 0; i < key.compileTimeParameters.size(); i++) {
					if (i > 0)
						report += ", ";
					report += key.compileTimeParameters[i].first;
					report += "=";
					report += formatCompileTimeValueForPurityReport(key.compileTimeParameters[i].second);
				}
				report += "]";
			}
		}
	}
	return report;
}

static void updateLineTrimming(CodeLine *line, const SyntaxConfig &syntax) {
	size_t commentPos = findCommentStart(line->fullText, syntax.commentPrefix);
	std::string_view withoutComment =
		(commentPos != std::string_view::npos) ? line->fullText.substr(0, commentPos) : line->fullText;
	size_t trimmedEnd = withoutComment.size();
	while (trimmedEnd > 0 && std::isspace(static_cast<unsigned char>(withoutComment[trimmedEnd - 1])) != 0)
		trimmedEnd--;
	line->rightTrimmedText = withoutComment.substr(0, trimmedEnd);
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

static bool validateSourceCharacters(ParseContext &context, CodeLine *line, const SyntaxConfig &syntax) {
	bool insideString = false;
	size_t commentStart = findCommentStart(line->fullText, syntax.commentPrefix);
	std::vector<size_t> pendingStringArgumentCharacters;
	for (size_t index = 0; index < line->fullText.size(); index++) {
		char character = line->fullText[index];
		if (index < commentStart && character == '"' && (index == 0 || line->fullText[index - 1] != '\\')) {
			insideString = !insideString;
			if (!insideString)
				pendingStringArgumentCharacters.clear();
			continue;
		}
		if (character != argumentChar)
			continue;
		if (index < commentStart && insideString) {
			pendingStringArgumentCharacters.push_back(index);
			continue;
		}

		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "disallowed character", Range(line, index, index + 1), "character", "U+0007"
		));
		return false;
	}
	if (!pendingStringArgumentCharacters.empty()) {
		size_t index = pendingStringArgumentCharacters.front();
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "disallowed character", Range(line, index, index + 1), "character", "U+0007"
		));
		return false;
	}
	return true;
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
	return normalized.starts_with("lib/") || normalized.starts_with("/lib/") ||
		   normalized.starts_with("/usr/share/dynlex/lib/") || normalized.starts_with(projectLibPrefix);
}

#include "callableFunctionLookup.inl"

PatternDefinition *findDefinitionBySignature(
	ParseContext &context, SectionType sectionType, std::string_view signature, const lsp::SourceFile *sourceFile
) {
	std::vector<PatternDefinition *> matches = findDefinitionsBySignature(context, sectionType, signature, sourceFile);
	return !matches.empty() ? matches[0] : nullptr;
}

void appendPatternCallBindings(Expression *expr, PatternDefinition *definition, BindingMap &bindings) {
	collectPatternCallBindings(expr, definition, bindings);
}

Expression *createTypeConstraintExpression(ParseContext &context, Section *section, Range sourceRange) {
	if (!section || !sourceRange.line || sourceRange.subString.empty())
		return nullptr;

	Expression *typeExpr = section->detectPatterns(context, sourceRange, SectionType::Function, false);
	if (!typeExpr)
		return nullptr;

	auto materializeTemporaryVariableReferences =
		[&context, section](PatternReference *reference, PatternMatch &match, auto &self) -> void {
		int offset = reference->range().start();
		for (VariableMatch &varMatch : match.discoveredVariables) {
			if (varMatch.variableReference)
				continue;
			varMatch.variableReference = context.createVariableReference(
				Range(reference->range().line, offset + varMatch.lineStartPos, offset + varMatch.lineEndPos), varMatch.name
			);
			Variable *variable = section->findVariable(varMatch.name);
			if (variable && variable->definition)
				varMatch.variableReference->definition = normalizeBindingReference(variable->definition);
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
	return typeExpr;
}

void destroyTypeConstraintExpression(Expression *root) {
	std::unordered_set<Expression *> visited;
	std::function<void(Expression *)> destroy = [&](Expression *expression) {
		if (!expression || !visited.insert(expression).second)
			return;
		for (Expression *argument : expression->arguments)
			destroy(argument);
		delete expression->patternReference;
		delete expression;
	};
	destroy(root);
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

	if (!validate(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::Validated;

	if (!initializeTargetLayout(context))
		return false;

	if (!inferTypes(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::InferredTypes;

	return true;
}

bool importSourceFile(const std::string &path, ParseContext &context, const std::string &resolutionRoot) {
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
	bool isMainSourceFile = !context.mainSourceFile;
	if (isMainSourceFile)
		context.mainSourceFile = sourceFile;
	std::string activeResolutionRoot = resolutionRoot;
	if (isMainSourceFile && activeResolutionRoot.empty()) {
		std::filesystem::path mainPath =
			std::filesystem::absolute(pathutil::toFilesystemPath(sourceFile->uri.empty() ? path : sourceFile->uri));
		activeResolutionRoot = mainPath.parent_path().lexically_normal().string();
	}

	// Iterate over lines, preserving line terminators for exact source mapping.
	std::string_view fileView{sourceFile->content};
	const SyntaxConfig &syntax = syntaxConfigForSourcePath(context, sourceFile->uri.empty() ? path : sourceFile->uri);

	int sourceFileLineIndex = 0;
	size_t lineStart = 0;
	while (lineStart < fileView.size()) {
		size_t contentEnd = lineStart;
		while (contentEnd < fileView.size() && fileView[contentEnd] != '\r' && fileView[contentEnd] != '\n')
			contentEnd++;
		size_t lineEnd = contentEnd;
		if (lineEnd < fileView.size()) {
			if (fileView[lineEnd] == '\r' && lineEnd + 1 < fileView.size() && fileView[lineEnd + 1] == '\n')
				lineEnd += 2;
			else
				lineEnd += 1;
		}

		std::string_view lineString = fileView.substr(lineStart, lineEnd - lineStart);
		CodeLine *line = createSourceLine(context, sourceFile, sourceFileLineIndex, lineString);
		if (!validateSourceCharacters(context, line, syntax))
			return false;

		// check if the line is an import statement
		if (std::optional<std::string_view> importPathView =
				extractDirectiveArgument(line->rightTrimmedText, syntax.importKeyword)) {
			// recursively import the file, replacing this line with the imported content
			std::string importingDir =
				std::filesystem::path(pathutil::toFilesystemPath(sourceFile->uri.empty() ? path : sourceFile->uri))
					.parent_path()
					.string();
			ResolvedImport resolvedImport =
				resolveImportPath(std::string(*importPathView), importingDir, activeResolutionRoot, context.fileSystem.get());
			const std::string &importPath = resolvedImport.path;
			if (!importSourceFile(importPath, context, resolvedImport.root)) {
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
		lineStart = lineEnd;
		sourceFileLineIndex++;
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
			size_t indentLength = 0;
			while (indentLength < line->rightTrimmedText.size() &&
				   std::isspace(static_cast<unsigned char>(line->rightTrimmedText[indentLength])) != 0)
				indentLength++;
			indentString = std::string(line->rightTrimmedText.substr(0, indentLength));
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
				if (!currentSection->processLine(context, line))
					return false;
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

	return true;
}

#include "compilerTypeUtilities.inl"
