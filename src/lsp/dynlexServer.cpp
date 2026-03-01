#include "dynlexServer.h"
#include "codeLine.h"
#include "compiler.h"
#include "completion.h"
#include "configDocument.h"
#include "function.h"
#include "lspFileSystem.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "sectionType.h"
#include "semanticTokenBuilder.h"
#include "semanticTokenDebug.h"
#include "sourceFile.h"
#include <algorithm>
#include <filesystem>
#include <regex>
using namespace std::literals;

namespace lsp {

// Ensure a URI is an absolute file:// URI.
// Imported files may have relative paths (e.g. "lib/random.dl") that need
// to be resolved to absolute file:// URIs for the LSP client.
static std::string toAbsoluteUri(const std::string &uri) {
	if (uri.starts_with("file://")) {
		return uri;
	}
	return "file://" + std::filesystem::absolute(uri).string();
}

static std::string toFilesystemPath(std::string_view uriOrPath) {
	if (uriOrPath.starts_with("file://")) {
		return std::string(uriOrPath.substr("file://"sv.size()));
	}
	return std::string(uriOrPath);
}

static bool hasCompilationStage(const ParseContext *context, ParseContext::CompilationStage stage) {
	return context && context->hasCompleted(stage);
}

DynLexServer::DynLexServer(int port) : LanguageServer(port) {}

DynLexServer::DynLexServer(std::unique_ptr<Transport> transport) : LanguageServer(std::move(transport)) {}

DynLexServer::~DynLexServer() = default;

InitializeResult DynLexServer::onInitialize(const InitializeParams &params) {
	if (params.rootUri) {
		workspaceRootPath = toFilesystemPath(*params.rootUri);
	} else {
		workspaceRootPath = std::filesystem::current_path().string();
	}

	InitializeResult result;
	result.capabilities.textDocumentSync = 2; // Incremental
	result.capabilities.definitionProvider = true;
	result.capabilities.completionProvider.supported = true;
	result.capabilities.completionProvider.triggerCharacters = {" ", "/", ".", "_", "-", ":", "(", ")", "\"", "a", "b", "c",
																"d", "e", "f", "g", "h", "i", "j", "k", "l",  "m", "n", "o",
																"p", "q", "r", "s", "t", "u", "v", "w", "x",  "y", "z"};
	result.capabilities.documentSymbolProvider = true;
	result.capabilities.codeActionProvider = true;
	result.capabilities.semanticTokensProvider.full = true;
	result.capabilities.semanticTokensProvider.legend.tokenTypes = getSemanticTokenTypes();
	result.capabilities.semanticTokensProvider.legend.tokenModifiers = getSemanticTokenModifiers();
	return result;
}

void DynLexServer::onDidOpen(const DidOpenTextDocumentParams &params) {
	LanguageServer::onDidOpen(params);
	const std::string &uri = params.textDocument.uri;
	if (isConfigDocumentUri(uri)) {
		auto docIt = documents.find(uri);
		if (docIt != documents.end())
			publishDiagnostics(uri, collectConfigDiagnostics(*docIt->second));
		std::vector<std::string> dependentMainUris;
		for (const auto &[mainUri, context] : parseContexts) {
			if (!context->projectSyntaxConfigPath.empty() && toAbsoluteUri(context->projectSyntaxConfigPath) == uri)
				dependentMainUris.push_back(mainUri);
		}
		for (const std::string &mainUri : dependentMainUris)
			recompileMainDocument(mainUri);
		return;
	}

	// If this file is already compiled as an import of a main document, skip —
	// diagnostics are already published from the main document's compilation.
	if (importedBy.contains(uri) && !importedBy[uri].empty()) {
		return;
	}

	recompileMainDocument(uri);
}

void DynLexServer::onDidChange(const DidChangeTextDocumentParams &params) {
	LanguageServer::onDidChange(params);
	const std::string &uri = params.textDocument.uri;
	if (isConfigDocumentUri(uri)) {
		auto docIt = documents.find(uri);
		if (docIt != documents.end())
			publishDiagnostics(uri, collectConfigDiagnostics(*docIt->second));
		std::vector<std::string> dependentMainUris;
		for (const auto &[mainUri, context] : parseContexts) {
			if (!context->projectSyntaxConfigPath.empty() && toAbsoluteUri(context->projectSyntaxConfigPath) == uri)
				dependentMainUris.push_back(mainUri);
		}
		for (const std::string &mainUri : dependentMainUris)
			recompileMainDocument(mainUri);
		return;
	}

	// If this file is a main document, recompile it
	if (parseContexts.contains(uri)) {
		recompileMainDocument(uri);
	}

	// If this file is imported by main documents, recompile those too
	auto it = importedBy.find(uri);
	if (it != importedBy.end()) {
		// Copy: recompilation may modify importedBy
		auto mainUris = it->second;
		for (const auto &mainUri : mainUris) {
			recompileMainDocument(mainUri);
		}
	}
}

void DynLexServer::onDidClose(const DidCloseTextDocumentParams &params) {
	const std::string &uri = params.textDocument.uri;
	if (isConfigDocumentUri(uri))
		publishDiagnostics(uri, {});

	if (parseContexts.contains(uri)) {
		// Collect file URIs this main document had diagnostics for
		std::unordered_set<std::string> affectedUris;
		auto diagIt = diagnosticsPerMain.find(uri);
		if (diagIt != diagnosticsPerMain.end()) {
			for (const auto &[fileUri, _] : diagIt->second) {
				affectedUris.insert(fileUri);
			}
		}

		// Remove this main document from importedBy entries
		for (auto &[imported, mains] : importedBy) {
			mains.erase(uri);
		}

		// Clean up state
		parseContexts.erase(uri);
		diagnosticsPerMain.erase(uri);

		// Re-publish merged diagnostics for affected files (now without this main's contribution)
		for (const auto &fileUri : affectedUris) {
			publishMergedDiagnostics(fileUri);
		}
	}

	LanguageServer::onDidClose(params);
}

ParseContext *DynLexServer::findContextFor(const std::string &uri) {
	auto ctxIt = parseContexts.find(uri);
	if (ctxIt != parseContexts.end()) {
		return ctxIt->second.get();
	}

	// Check if this file is imported by a main document
	auto importIt = importedBy.find(uri);
	if (importIt != importedBy.end()) {
		for (const auto &mainUri : importIt->second) {
			auto mainCtxIt = parseContexts.find(mainUri);
			if (mainCtxIt != parseContexts.end()) {
				return mainCtxIt->second.get();
			}
		}
	}

	return nullptr;
}

void DynLexServer::recompileMainDocument(const std::string &uri) {
	auto docIt = documents.find(uri);
	if (docIt == documents.end()) {
		return;
	}

	// Collect file URIs that previously had diagnostics from this main document
	std::unordered_set<std::string> previouslyAffected;
	auto oldDiagIt = diagnosticsPerMain.find(uri);
	if (oldDiagIt != diagnosticsPerMain.end()) {
		for (const auto &[fileUri, _] : oldDiagIt->second) {
			previouslyAffected.insert(fileUri);
		}
	}

	// Remove old import tracking for this main document
	for (auto &[imported, mains] : importedBy) {
		mains.erase(uri);
	}

	// Create new parse context with LSP file system
	auto context = std::make_unique<ParseContext>();
	context->fileSystem = std::make_unique<LspFileSystem>(documents);

	// Use the compiler to parse and analyze
	compile(uri, *context);

	// Update import graph
	for (const auto &[path, sourceFile] : context->importedFiles) {
		std::string importedUri = toAbsoluteUri(sourceFile->uri);
		if (importedUri != uri) {
			importedBy[importedUri].insert(uri);
		}
	}

	// Group diagnostics by their actual source file
	auto &diagsForMain = diagnosticsPerMain[uri];
	diagsForMain.clear();
	diagsForMain[uri]; // always include main file so it gets cleared
	for (const auto &diag : context->diagnostics) {
		std::string fileUri = diag.range.line ? toAbsoluteUri(diag.range.line->sourceFile->uri) : uri;
		diagsForMain[fileUri].push_back(convertDiagnostic(diag));
	}

	// Collect all affected file URIs (old and new) and re-publish
	std::unordered_set<std::string> allAffected = previouslyAffected;
	for (const auto &[fileUri, _] : diagsForMain) {
		allAffected.insert(fileUri);
	}
	for (const auto &fileUri : allAffected) {
		publishMergedDiagnostics(fileUri);
	}

	// Store context
	parseContexts[uri] = std::move(context);
}

void DynLexServer::publishMergedDiagnostics(const std::string &fileUri) {
	std::vector<Diagnostic> merged;

	for (const auto &[mainUri, diagsByFile] : diagnosticsPerMain) {
		auto it = diagsByFile.find(fileUri);
		if (it != diagsByFile.end()) {
			for (const auto &diag : it->second) {
				// Deduplicate: skip if same range + message already present
				bool duplicate = std::any_of(merged.begin(), merged.end(), [&](const Diagnostic &existing) {
					return existing.range.start.line == diag.range.start.line &&
						   existing.range.start.character == diag.range.start.character &&
						   existing.range.end.line == diag.range.end.line &&
						   existing.range.end.character == diag.range.end.character && existing.message == diag.message;
				});
				if (!duplicate) {
					merged.push_back(diag);
				}
			}
		}
	}

	publishDiagnostics(fileUri, merged);
}

Range DynLexServer::convertRange(const ::Range &range) const {
	Range lspRange;
	lspRange.start.line = range.line->sourceFileLineIndex;
	lspRange.start.character = range.start();
	lspRange.end.line = range.line->sourceFileLineIndex;
	lspRange.end.character = range.end();
	return lspRange;
}

Diagnostic DynLexServer::convertDiagnostic(const ::Diagnostic &diag) const {
	Diagnostic lspDiag;
	lspDiag.range = convertRange(diag.range);
	lspDiag.message = diag.message;
	lspDiag.source = "dynlex";

	switch (diag.level) {
	case ::Diagnostic::Level::Error:
		lspDiag.severity = DiagnosticSeverity::Error;
		break;
	case ::Diagnostic::Level::Warning:
		lspDiag.severity = DiagnosticSeverity::Warning;
		break;
	case ::Diagnostic::Level::Info:
		lspDiag.severity = DiagnosticSeverity::Information;
		break;
	}

	for (const auto &related : diag.relatedInfo) {
		DiagnosticRelatedInformation info;
		info.message = related.message;
		info.location.uri = toAbsoluteUri(related.range.line->sourceFile->uri);
		info.location.range = convertRange(related.range);
		lspDiag.relatedInformation.push_back(std::move(info));
	}
	if (!diag.quickFixes.empty()) {
		Json quickFixes = Json::array();
		for (const auto &fix : diag.quickFixes) {
			if (!fix.range.line)
				continue;
			quickFixes.push_back(
				Json{{"title", fix.title}, {"replacement", fix.replacement}, {"range", convertRange(fix.range)}}
			);
		}
		if (!quickFixes.empty())
			lspDiag.data = Json{{"quickFixes", quickFixes}};
	}

	return lspDiag;
}

void DynLexServer::publishDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics) {
	PublishDiagnosticsParams params;
	params.uri = uri;
	params.diagnostics = diagnostics;
	sendNotification("textDocument/publishDiagnostics", params);
}

CompletionList DynLexServer::onCompletion(const TextDocumentPositionParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return {};
	auto docIt = documents.find(params.textDocument.uri);
	if (docIt == documents.end()) {
		return {};
	}

	std::string_view line = docIt->second->getLine(params.position.line);
	size_t character = std::min<size_t>(params.position.character, line.size());
	return collectCompletions(
		CompletionContext{
			.parseContext = findContextFor(params.textDocument.uri),
			.uri = params.textDocument.uri,
			.linePrefix = std::string(line.substr(0, character)),
			.workspaceRootPath = workspaceRootPath,
			.line = params.position.line,
			.character = static_cast<int>(character),
		}
	);
}

// Find the deepest function containing the cursor position.
// Depth-first: children (subfunctions) take priority over parents,
// matching the semantic tokenizer's slicing behavior.
static Function *findDeepestFunction(Function *expr, int character) {
	for (Function *arg : expr->arguments) {
		if (arg->range.start() <= character && character < arg->range.end()) {
			Function *deeper = findDeepestFunction(arg, character);
			if (deeper)
				return deeper;
		}
	}
	if (expr->range.start() <= character && character < expr->range.end()) {
		return expr;
	}
	return nullptr;
}

// Get the definition location for an function, following the same
// semantic categories as the tokenizer (Variable, PatternCall, etc.)
static std::optional<::Range> getDefinitionTarget(Function *expr) {
	switch (expr->kind) {
	case Function::Kind::Variable:
		if (expr->variable && expr->variable->definition) {
			return expr->variable->definition->range;
		}
		break;
	case Function::Kind::PatternCall:
		if (expr->patternMatch && expr->patternMatch->matchedEndNode &&
			!expr->patternMatch->matchedEndNode->matchingDefinitions.empty()) {
			return expr->patternMatch->matchedEndNode->matchingDefinitions[0]->range;
		}
		break;
	default:
		break;
	}
	return std::nullopt;
}

std::optional<Location> DynLexServer::onDefinition(const TextDocumentPositionParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return std::nullopt;
	ParseContext *context = findContextFor(params.textDocument.uri);
	if (!hasCompilationStage(context, ParseContext::CompilationStage::ResolvedPatterns)) {
		return std::nullopt;
	}

	// Find the code line at the cursor position
	for (CodeLine *codeLine : context->codeLines) {
		if (codeLine->sourceFileLineIndex != params.position.line) {
			continue;
		}
		if (toAbsoluteUri(codeLine->sourceFile->uri) != params.textDocument.uri) {
			continue;
		}

		// Walk the function tree to find the deepest function at cursor
		if (codeLine->function) {
			Function *expr = findDeepestFunction(codeLine->function, params.position.character);
			if (expr) {
				auto target = getDefinitionTarget(expr);
				if (target) {
					Location loc;
					loc.uri = toAbsoluteUri(target->line->sourceFile->uri);
					loc.range = convertRange(*target);
					return loc;
				}
			}
		}
		break;
	}

	return std::nullopt;
}

// Reconstruct pattern name from definition elements
static std::string getPatternName(const PatternDefinition *def) {
	std::string name;
	for (const auto &elem : def->patternElements) {
		if (elem.type == PatternElement::Choice && !elem.alternatives.empty()) {
			name += elem.alternatives[0][0].text;
		} else {
			name += elem.text;
		}
	}
	return name;
}

static SymbolKind symbolKindForSection(SectionType type) {
	switch (type) {
	case SectionType::Function:
		return SymbolKind::Function;
	case SectionType::Class:
		return SymbolKind::Class;
	case SectionType::Pattern:
		return SymbolKind::Module;
	default:
		return SymbolKind::Namespace;
	}
}

std::vector<DocumentSymbol> DynLexServer::onDocumentSymbol(const DocumentSymbolParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return {};
	ParseContext *context = findContextFor(params.textDocument.uri);
	if (!hasCompilationStage(context, ParseContext::CompilationStage::AnalyzedSections)) {
		return {};
	}

	std::function<void(Section *, std::vector<DocumentSymbol> &)> collectSymbols = [&](Section *section,
																					   std::vector<DocumentSymbol> &out) {
		for (PatternDefinition *def : section->patternDefinitions) {
			if (!def->range.line || toAbsoluteUri(def->range.line->sourceFile->uri) != params.textDocument.uri) {
				continue;
			}

			DocumentSymbol sym;
			sym.name = getPatternName(def);
			std::string typeStr = sectionTypeToString(section->type);
			sym.detail = section->isMacro ? "macro " + typeStr : typeStr;
			sym.kind = symbolKindForSection(section->type);
			sym.selectionRange = convertRange(def->range);

			// Full range: from definition line through last code line of the section
			sym.range = sym.selectionRange;
			if (!section->codeLines.empty()) {
				CodeLine *last = section->codeLines.back();
				if (toAbsoluteUri(last->sourceFile->uri) == params.textDocument.uri &&
					(last->sourceFileLineIndex > sym.range.end.line ||
					 (last->sourceFileLineIndex == sym.range.end.line &&
					  static_cast<int>(last->rightTrimmedText.size()) > sym.range.end.character))) {
					sym.range.end.line = last->sourceFileLineIndex;
					sym.range.end.character = static_cast<int>(last->rightTrimmedText.size());
				}
			}

			// Recurse into child sections
			for (Section *child : section->children) {
				collectSymbols(child, sym.children);
			}

			out.push_back(std::move(sym));
		}

		// Sections without pattern definitions but with children (e.g. main section)
		if (section->patternDefinitions.empty()) {
			for (Section *child : section->children) {
				collectSymbols(child, out);
			}
		}
	};

	std::vector<DocumentSymbol> result;
	collectSymbols(context->mainSection, result);
	return result;
}

std::vector<CodeAction> DynLexServer::onCodeAction(const CodeActionParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return {};
	std::vector<CodeAction> actions;
	for (const Diagnostic &diag : params.context.diagnostics) {
		if (!diag.data || !diag.data->contains("quickFixes"))
			continue;
		for (const Json &fix : (*diag.data)["quickFixes"]) {
			if (!fix.contains("title") || !fix.contains("replacement") || !fix.contains("range"))
				continue;
			CodeAction action;
			action.title = fix.at("title").get<std::string>();
			action.kind = "quickfix";
			action.diagnostics.push_back(diag);
			WorkspaceEdit edit;
			TextEdit textEdit;
			textEdit.range = fix.at("range").get<Range>();
			textEdit.newText = fix.at("replacement").get<std::string>();
			edit.changes[params.textDocument.uri] = Json::array({textEdit});
			action.edit = std::move(edit);
			actions.push_back(std::move(action));
		}
	}
	return actions;
}

SemanticTokens DynLexServer::onSemanticTokensFull(const SemanticTokensParams &params) {
	SemanticTokens result;
	result.data = generateSemanticTokens(params.textDocument.uri);
	return result;
}

std::vector<int> DynLexServer::generateSemanticTokens(const std::string &uri) {
	if (isConfigDocumentUri(uri)) {
		auto docIt = documents.find(uri);
		if (docIt == documents.end())
			return {};
		return encodeConfigSemanticTokens(*docIt->second);
	}
	ParseContext *context = findContextFor(uri);
	if (!hasCompilationStage(context, ParseContext::CompilationStage::AnalyzedSections)) {
		return {};
	}
	auto docIt = documents.find(uri);
	if (docIt == documents.end()) {
		return {};
	}
	return encodeSemanticTokens(collectSemanticTokens(*context, uri, docIt->second->lineCount(), true));
}

} // namespace lsp
