#include "compiler.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "pathUtils.h"
#include "pattern/transformedPattern.h"
#include "section/parseUtils.h"
#include "sourcePreparation.h"
#include "sourceSyntax.h"
#include "sourceTransform.h"
#include "syntaxConfig.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

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

static void updateLineTrimming(CodeLine *line, const SyntaxConfig &syntax) {
	size_t commentPos = findCommentStart(line->fullText, syntax.commentPrefix);
	std::string_view withoutComment =
		(commentPos != std::string_view::npos) ? line->fullText.substr(0, commentPos) : line->fullText;
	size_t trimmedEnd = withoutComment.size();
	while (trimmedEnd > 0 && std::isspace(static_cast<unsigned char>(withoutComment[trimmedEnd - 1])) != 0)
		trimmedEnd--;
	line->rightTrimmedText = withoutComment.substr(0, trimmedEnd);
}

static CodeLine *
createSourceLine(ParseContext &context, lsp::SourceFile *sourceFile, int sourceFileLineIndex, std::string_view text) {
	CodeLine *line = context.createCodeLine(
		sourceFile, sourceFileLineIndex, std::string(text),
		{{0, static_cast<int>(text.size()), sourceFile, sourceFileLineIndex, 0}}
	);
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, sourceFile);
	line = expandFormattedStringLine(context, *line);
	if (!line)
		return nullptr;
	updateLineTrimming(line, syntax);
	return line;
}

static bool validateSourceCharacters(ParseContext &context, CodeLine *line, const SyntaxConfig &syntax) {
	bool insideString = false;
	size_t commentStart = findCommentStart(line->fullText, syntax.commentPrefix);
	std::vector<size_t> pendingStringArgumentCharacters;
	for (size_t index = 0; index < line->fullText.size(); index++) {
		char character = line->fullText[index];
		if (index < commentStart && character == '"' && !isEscapedCharacter(line->fullText, index)) {
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

// resolutionRoot keeps nested imports in the tree selected by their importer.
static bool
importSourceFile(const std::string &path, ParseContext &context, const std::string &resolutionRoot, SourceLoadPurpose purpose) {
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
	const SyntaxConfig &syntax =
		sourceFile->uri.empty() ? syntaxConfigForSourcePath(context, path) : syntaxConfigForSourceFile(context, sourceFile);

	auto importLine = [&](CodeLine *line, std::optional<std::string_view> importPathView) {
		if (importPathView) {
			std::string_view importText = trimSourceIndent(line->rightTrimmedText);
			// recursively import the file, replacing this line with the imported content
			std::string importingDir =
				std::filesystem::path(pathutil::toFilesystemPath(sourceFile->uri.empty() ? path : sourceFile->uri))
					.parent_path()
					.string();
			ResolvedImport resolvedImport =
				resolveImportPath(std::string(*importPathView), importingDir, activeResolutionRoot, context.fileSystem.get());
			const std::string &importPath = resolvedImport.path;
			if (!importSourceFile(importPath, context, resolvedImport.root, purpose)) {
				context.addDiagnostic(Diagnostic(
					context, Diagnostic::Level::Error, "failed to import source file",
					Range(
						line,
						static_cast<int>(importText.data() - line->rightTrimmedText.data() + syntax.importKeyword.length()),
						line->rightTrimmedText.length()
					),
					"path", importPath
				));
				return false;
			}
			// Fall through to add import line to codeLines (tokenized during section analysis)
		}
		line->mergedLineIndex = context.codeLines.size();
		context.codeLines.push_back(line);
		return true;
	};
	SourcePreparationHooks *products = purpose == SourceLoadPurpose::Compilation ? context.sourcePreparationHooks : nullptr;
	if (products) {
		if (auto prepared = products->reuse(*sourceFile, syntax)) {
			for (const PreparedSourceLine &entry : *prepared) {
				for (const auto &token : entry.tokens)
					context.addSourceToken(token.range, token.kind, token.referencedPatternType);
				if (!importLine(entry.line, entry.importPath))
					return false;
			}
			return true;
		}
	}
	std::vector<PreparedSourceLine> prepared;

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
		size_t tokenStart = context.sourceTokenAnnotations.size();
		CodeLine *line = createSourceLine(context, sourceFile, sourceFileLineIndex, lineString);
		if (!line)
			return false;
		// Dependency inspection reads the complete import graph even for programs
		// intentionally containing lexical errors, such as diagnostic fixtures.
		if (purpose == SourceLoadPurpose::Compilation && !validateSourceCharacters(context, line, syntax))
			return false;

		std::optional<std::string_view> importPathView =
			extractDirectiveArgument(trimSourceIndent(line->rightTrimmedText), syntax.importKeyword);
		if (products) {
			prepared.push_back(
				{line,
				 importPathView,
				 {context.sourceTokenAnnotations.begin() + tokenStart, context.sourceTokenAnnotations.end()}}
			);
		}
		if (!importLine(line, importPathView))
			return false;
		lineStart = lineEnd;
		sourceFileLineIndex++;
	}
	if (products)
		products->record(*sourceFile, syntax, prepared);
	return true;
}

bool loadSourceFiles(const std::string &path, ParseContext &context, SourceLoadPurpose purpose) {
	return initializeSyntaxConfigs(context, path) && importSourceFile(path, context, "", purpose);
}
