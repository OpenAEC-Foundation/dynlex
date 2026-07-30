#include "dynlexServer.h"
#include "codeLine.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "completion.h"
#include "configDocument.h"
#include "expression.h"
#include "lspAnalysis.h"
#include "lspFileSystem.h"
#include "pathUtils.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "sectionType.h"
#include "semanticTokenBuilder.h"
#include "semanticTokenDebug.h"
#include "sourceFile.h"
#include "variable.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <regex>
#include <sstream>
using namespace std::literals;

namespace lsp {

static bool hasCompilationStage(const ParseContext *context, ParseContext::CompilationStage stage) {
	return context && context->hasCompleted(stage);
}

static std::string lineTerminator(const TextDocument &document, int line) {
	std::string_view withTerminator = document.getLineWithTerminator(line);
	std::string_view withoutTerminator = document.getLine(line);
	return std::string(withTerminator.substr(withoutTerminator.size()));
}

template <typename TLockedLines>
static std::string rebuildDocumentContent(const TextDocument &document, const TLockedLines *lockedLines) {
	std::string rebuilt;
	rebuilt.reserve(document.content.size());
	for (int line = 0; line < document.lineCount(); ++line) {
		if (lockedLines) {
			auto it = lockedLines->find(line);
			if (it != lockedLines->end()) {
				rebuilt += it->second.committedText;
				rebuilt += lineTerminator(document, line);
				continue;
			}
		}
		{
			rebuilt += document.getLineWithTerminator(line);
		}
	}
	return rebuilt;
}

DynLexServer::DynLexServer(int port) : LanguageServer(port) {}

DynLexServer::DynLexServer(std::unique_ptr<Transport> transport) : LanguageServer(std::move(transport)) {}

DynLexServer::~DynLexServer() = default;

InitializeResult DynLexServer::onInitialize(const InitializeParams &params) {
	analysisProfiles = parseAnalysisProfiles(params.initializationOptions);
	if (params.rootUri) {
		workspaceRootPath = pathutil::toFilesystemPath(*params.rootUri);
	} else {
		workspaceRootPath = std::filesystem::current_path().string();
	}

	InitializeResult result;
	result.capabilities.textDocumentSync = 2; // Incremental
	result.capabilities.definitionProvider = true;
	result.capabilities.hoverProvider = true;
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
	syncCompiledDocument(uri, true);
	if (isConfigDocumentUri(uri)) {
		auto docIt = documents.find(uri);
		if (docIt != documents.end())
			publishDiagnostics(uri, collectConfigDiagnostics(*docIt->second));
		recompileDependents(uri);
		requestSemanticTokensRefresh();
		return;
	}

	// If this file is already compiled as an import of a main document, skip —
	// diagnostics are already published from the main document's compilation.
	if (importedBy.contains(uri) && !importedBy[uri].empty()) {
		requestSemanticTokensRefresh();
		return;
	}

	recompileMainDocument(uri);
	requestSemanticTokensRefresh();
}

void DynLexServer::onDidChange(const DidChangeTextDocumentParams &params) {
	LanguageServer::onDidChange(params);
	const std::string &uri = params.textDocument.uri;
	const bool structuralEdit = isStructuralEdit(params);
	bool compiledChanged = false;

	if (isConfigDocumentUri(uri)) {
		compiledChanged = syncCompiledDocument(uri, structuralEdit);
		if (structuralEdit)
			refreshLockedLineBaselines(uri);
		auto docIt = documents.find(uri);
		if (docIt != documents.end())
			publishDiagnostics(uri, collectConfigDiagnostics(*docIt->second));
		if (compiledChanged)
			recompileDependents(uri);
		requestSemanticTokensRefresh();
		return;
	}

	// Single-line edits on locked lines keep compiling against the committed
	// shadow document. Structural edits bypass locks and update the full
	// compiled view immediately.
	compiledChanged = syncCompiledDocument(uri, structuralEdit);
	if (structuralEdit) {
		refreshLockedLineBaselines(uri);
	}

	if (compiledChanged)
		recompileDependents(uri);
	requestSemanticTokensRefresh();
}

void DynLexServer::onDidClose(const DidCloseTextDocumentParams &params) {
	const std::string &uri = params.textDocument.uri;
	if (isConfigDocumentUri(uri))
		publishDiagnostics(uri, {});

	compiledDocuments.erase(uri);
	lockedLinesByUri.erase(uri);
	for (auto cursorIt = activeCursors.begin(); cursorIt != activeCursors.end();) {
		if (cursorIt->second.uri == uri) {
			cursorIt = activeCursors.erase(cursorIt);
		} else {
			++cursorIt;
		}
	}

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
		eraseImportTrackingForMain(importedBy, uri);

		// Clean up state
		parseContexts.erase(uri);
		diagnosticsPerMain.erase(uri);

		// Re-publish merged diagnostics for affected files (now without this main's contribution)
		for (const auto &fileUri : affectedUris) {
			publishMergedDiagnostics(fileUri);
		}
	}

	if (completionParseContexts.contains(uri)) {
		eraseImportTrackingForMain(completionImportedBy, uri);
		completionParseContexts.erase(uri);
	}

	LanguageServer::onDidClose(params);
	requestSemanticTokensRefresh();
}

void DynLexServer::onActiveCursorChanged(const ActiveCursorParams &params) {
	std::optional<CursorState> nextCursor;
	if (params.uri && params.position && params.version) {
		auto docIt = documents.find(*params.uri);
		if (docIt == documents.end() || docIt->second->version != *params.version)
			return;
		CursorState cursor{*params.uri, *params.position};
		nextCursor = cursor;
	}

	if (updateCursorLock(params.clientId, nextCursor))
		requestSemanticTokensRefresh();
}

ParseContext *DynLexServer::findContextFor(const std::string &uri) {
	std::vector<ParseContext *> contexts = findContextsFor(uri);
	return contexts.empty() ? nullptr : contexts.front();
}

ParseContext *DynLexServer::findCompletionContextFor(const std::string &uri) {
	std::vector<ParseContext *> contexts = findContextsForUri(uri, completionParseContexts, completionImportedBy);
	return contexts.empty() ? nullptr : contexts.front();
}

std::vector<ParseContext *> DynLexServer::findContextsFor(const std::string &uri) {
	return findContextsForUri(uri, parseContexts, importedBy);
}

bool DynLexServer::isStructuralEdit(const DidChangeTextDocumentParams &params) const {
	if (params.contentChanges.size() != 1)
		return true;
	const TextDocumentContentChangeEvent &change = params.contentChanges.front();
	if (!change.range)
		return true;
	return change.range->start.line != change.range->end.line || change.text.find_first_of("\r\n") != std::string::npos;
}

bool DynLexServer::syncCompiledDocument(const std::string &uri, bool ignoreLocks) {
	auto liveIt = documents.find(uri);
	if (liveIt == documents.end()) {
		compiledDocuments.erase(uri);
		return false;
	}

	const TextDocument &live = *liveIt->second;
	std::string rebuilt =
		ignoreLocks ? live.content
					: rebuildDocumentContent(live, lockedLinesByUri.contains(uri) ? &lockedLinesByUri.at(uri) : nullptr);

	auto compiledIt = compiledDocuments.find(uri);
	if (compiledIt == compiledDocuments.end()) {
		compiledDocuments[uri] = std::make_unique<TextDocument>(uri, rebuilt, live.version);
		return true;
	}

	bool changed = compiledIt->second->content != rebuilt;
	if (changed) {
		compiledIt->second = std::make_unique<TextDocument>(uri, rebuilt, live.version);
	}
	return changed;
}

void DynLexServer::refreshLockedLineBaselines(const std::string &uri) {
	auto locksIt = lockedLinesByUri.find(uri);
	auto compiledIt = compiledDocuments.find(uri);
	if (locksIt == lockedLinesByUri.end() || compiledIt == compiledDocuments.end())
		return;

	for (auto &[line, state] : locksIt->second) {
		if (line >= 0 && line < compiledIt->second->lineCount())
			state.committedText = std::string(compiledIt->second->getLine(line));
	}
}

bool DynLexServer::updateCursorLock(const std::string &clientId, const std::optional<CursorState> &nextCursor) {
	bool changed = false;
	auto previousIt = activeCursors.find(clientId);
	if (previousIt != activeCursors.end()) {
		const CursorState &previous = previousIt->second;
		if (nextCursor && previous.uri == nextCursor->uri && previous.position.line == nextCursor->position.line)
			return false;

		auto locksIt = lockedLinesByUri.find(previous.uri);
		if (locksIt != lockedLinesByUri.end()) {
			auto lineIt = locksIt->second.find(previous.position.line);
			if (lineIt != locksIt->second.end()) {
				lineIt->second.clients.erase(clientId);
				if (lineIt->second.clients.empty()) {
					locksIt->second.erase(lineIt);
					bool compiledChanged = syncCompiledDocument(previous.uri, false);
					changed = compiledChanged || changed;
					if (compiledChanged)
						recompileDependents(previous.uri);
				}
				if (locksIt->second.empty())
					lockedLinesByUri.erase(locksIt);
			}
		}
		activeCursors.erase(previousIt);
	}

	if (!nextCursor)
		return changed;

	activeCursors[clientId] = *nextCursor;
	auto liveIt = documents.find(nextCursor->uri);
	if (liveIt == documents.end())
		return changed;

	auto compiledIt = compiledDocuments.find(nextCursor->uri);
	if (compiledIt == compiledDocuments.end())
		syncCompiledDocument(nextCursor->uri, true);

	auto &lineState = lockedLinesByUri[nextCursor->uri][nextCursor->position.line];
	if (lineState.clients.empty()) {
		auto baselineDocIt = compiledDocuments.find(nextCursor->uri);
		const TextDocument *baselineDoc =
			baselineDocIt != compiledDocuments.end() ? baselineDocIt->second.get() : liveIt->second.get();
		if (nextCursor->position.line >= 0 && nextCursor->position.line < baselineDoc->lineCount())
			lineState.committedText = std::string(baselineDoc->getLine(nextCursor->position.line));
		else
			lineState.committedText.clear();
	}
	lineState.clients.insert(clientId);
	return changed;
}

void DynLexServer::recompileMainDocument(const std::string &uri) {
	auto docIt = compiledDocuments.find(uri);
	if (docIt == compiledDocuments.end())
		return;

	// Collect file URIs that previously had diagnostics from this main document
	std::unordered_set<std::string> previouslyAffected;
	auto oldDiagIt = diagnosticsPerMain.find(uri);
	if (oldDiagIt != diagnosticsPerMain.end()) {
		for (const auto &[fileUri, _] : oldDiagIt->second) {
			previouslyAffected.insert(fileUri);
		}
	}

	ParseContexts contexts;
	contexts.reserve(analysisProfiles.size());
	for (const ParseContext::Options &analysisProfile : analysisProfiles) {
		auto context = std::make_shared<ParseContext>();
		context->options = analysisProfile;
		context->fileSystem = std::make_unique<LspFileSystem>(compiledDocuments);
		compile(uri, *context);
		contexts.push_back(std::move(context));
	}

	// Update the import graph from every target profile. Imports are available
	// as soon as the ImportedFiles stage completes, even if a later stage fails.
	eraseImportTrackingForMain(importedBy, uri);
	for (const std::shared_ptr<ParseContext> &context : contexts)
		addImportTrackingForMain(importedBy, uri, *context);

	// Group diagnostics from every target profile by their actual source file.
	auto &diagsForMain = diagnosticsPerMain[uri];
	diagsForMain.clear();
	diagsForMain[uri]; // always include main file so it gets cleared
	for (const std::shared_ptr<ParseContext> &context : contexts) {
		for (const auto &diag : context->diagnostics) {
			std::string fileUri = diag.range.line ? pathutil::toAbsoluteUri(diag.range.line->sourceFile->uri) : uri;
			diagsForMain[fileUri].push_back(convertDiagnostic(diag));
		}
	}

	// Collect all affected file URIs (old and new) and re-publish
	std::unordered_set<std::string> allAffected = previouslyAffected;
	for (const auto &[fileUri, _] : diagsForMain) {
		allAffected.insert(fileUri);
	}
	for (const auto &fileUri : allAffected) {
		publishMergedDiagnostics(fileUri);
	}

	// Store every target context for hover, definition, diagnostics, and tokens.
	parseContexts[uri] = contexts;

	// Completion matching only needs symbol/pattern state. Keep the last context
	// per profile that reached ResolvedPatterns so completions stay useful while the latest
	// compile is broken.
	ParseContexts completionContexts;
	std::copy_if(contexts.begin(), contexts.end(), std::back_inserter(completionContexts), [](const auto &context) {
		return context->hasCompleted(ParseContext::CompilationStage::ResolvedPatterns);
	});
	if (!completionContexts.empty()) {
		eraseImportTrackingForMain(completionImportedBy, uri);
		for (const std::shared_ptr<ParseContext> &context : completionContexts)
			addImportTrackingForMain(completionImportedBy, uri, *context);
		completionParseContexts[uri] = std::move(completionContexts);
	}
}

void DynLexServer::recompileDependents(const std::string &uri) {
	if (isConfigDocumentUri(uri)) {
		std::vector<std::string> dependentMainUris;
		for (const auto &[mainUri, contexts] : parseContexts) {
			const bool usesConfig = std::any_of(contexts.begin(), contexts.end(), [&](const auto &context) {
				return !context->projectSyntaxConfigPath.empty() &&
					   pathutil::toAbsoluteUri(context->projectSyntaxConfigPath) == uri;
			});
			if (usesConfig)
				dependentMainUris.push_back(mainUri);
		}
		for (const std::string &mainUri : dependentMainUris)
			recompileMainDocument(mainUri);
		return;
	}

	if (parseContexts.contains(uri))
		recompileMainDocument(uri);

	auto it = importedBy.find(uri);
	if (it == importedBy.end())
		return;

	auto mainUris = it->second;
	for (const auto &mainUri : mainUris)
		recompileMainDocument(mainUri);
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
	SourceLocation mappedStart = range.sourceStart();
	SourceLocation mappedEnd = range.sourceEnd();
	lspRange.start.line = mappedStart.sourceFileLineIndex;
	lspRange.start.character = mappedStart.column;
	lspRange.end.line = mappedEnd.sourceFileLineIndex;
	lspRange.end.character = mappedEnd.column;
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
		info.location.uri = pathutil::toAbsoluteUri(related.range.line->sourceFile->uri);
		info.location.range = convertRange(related.range);
		lspDiag.relatedInformation.push_back(std::move(info));
	}
	if (!diag.quickFixes.empty()) {
		Json quickFixes = Json::array();
		for (const auto &fix : diag.quickFixes) {
			if (!fix.range.line)
				continue;
			quickFixes.push_back(
				Json{
					{"title", fix.title},
					{"replacement", fix.replacement},
					{"range", convertRange(fix.range)},
					{"uri", pathutil::toAbsoluteUri(fix.range.line->sourceFile->uri)},
				}
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

void DynLexServer::requestSemanticTokensRefresh() { sendRequest("workspace/semanticTokens/refresh", Json::object()); }

CompletionList DynLexServer::onCompletion(const TextDocumentPositionParams &params) {
	auto docIt = documents.find(params.textDocument.uri);
	if (docIt == documents.end()) {
		return {};
	}
	if (isConfigDocumentUri(params.textDocument.uri)) {
		return collectConfigCompletions(*docIt->second, params.position.line, params.position.character);
	}

	ParseContext *completionContext = findCompletionContextFor(params.textDocument.uri);
	std::string_view line = docIt->second->getLine(params.position.line);
	size_t character = std::min<size_t>(params.position.character, line.size());
	return collectCompletions(
		CompletionContext{
			.parseContext = completionContext,
			.uri = params.textDocument.uri,
			.linePrefix = std::string(line.substr(0, character)),
			.workspaceRootPath = workspaceRootPath,
			.line = params.position.line,
			.character = static_cast<int>(character),
		}
	);
}

// Find the deepest expression containing the cursor position.
// Depth-first: child expressions take priority over parents,
// matching the semantic tokenizer's slicing behavior.
static Expression *findDeepestExpression(Expression *expr, int character) {
	for (Expression *arg : expr->arguments) {
		if (arg->range.start() <= character && character < arg->range.end()) {
			Expression *deeper = findDeepestExpression(arg, character);
			if (deeper)
				return deeper;
		}
	}
	if (expr->range.start() <= character && character < expr->range.end()) {
		return expr;
	}
	return nullptr;
}

static Expression *findVariableArgumentAtOffset(Expression *expr, int character) {
	if (!expr)
		return nullptr;
	if (expr->kind == Expression::Kind::PatternCall) {
		std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);
		for (Expression *arg : sortedArgs) {
			if (!arg)
				continue;
			if (arg->range.start() <= character && character < arg->range.end()) {
				if (arg->kind == Expression::Kind::Variable && arg->variable)
					return arg;
				if (Expression *nested = findVariableArgumentAtOffset(arg, character))
					return nested;
			}
		}
	}
	return nullptr;
}

// Get the definition location for an expression, following the same
// semantic categories as the tokenizer (Variable, PatternCall, etc.)
static std::optional<::Range> getDefinitionTarget(Expression *expr) {
	switch (expr->kind) {
	case Expression::Kind::Variable:
		if (expr->variable && expr->variable->definition) {
			return expr->variable->definition->range;
		}
		break;
	case Expression::Kind::PatternCall:
		if (expr->patternMatch && !expr->patternMatch->matchingDefinitions.empty()) {
			return expr->patternMatch->matchingDefinitions[0]->range;
		}
		break;
	default:
		break;
	}
	return std::nullopt;
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

static Section *findOwningVariableSectionAtSource(
	ParseContext &context, const std::string &uri, int line, int character, VariableReference *targetDefinition,
	const std::string &name
) {
	for (CodeLine *codeLine : context.codeLines) {
		if (!codeLine || !codeLine->containsSourceLocation(uri, line, character))
			continue;
		Section *owner = findOwningVariableSection(codeLine->section, targetDefinition, name);
		if (owner)
			return owner;
	}
	return nullptr;
}

static bool sameSourceLocation(const SourceLocation &a, const SourceLocation &b) {
	if (!a.sourceFile || !b.sourceFile)
		return false;
	return pathutil::toAbsoluteUri(a.sourceFile->uri) == pathutil::toAbsoluteUri(b.sourceFile->uri) &&
		   a.sourceFileLineIndex == b.sourceFileLineIndex && a.column == b.column;
}

static bool sameVariableDefinitionSource(VariableReference *a, VariableReference *b) {
	if (!a || !b || !a->range.line || !b->range.line)
		return false;
	return sameSourceLocation(a->range.sourceStart(), b->range.sourceStart()) &&
		   sameSourceLocation(a->range.sourceEnd(), b->range.sourceEnd());
}

static Section *
findInstantiatedOwnerSectionForDefinition(ParseContext &context, VariableReference *definition, const std::string &name) {
	if (!definition)
		return nullptr;
	Section *best = nullptr;
	size_t bestInstantiationCount = 0;
	SourceLocation definitionStart = definition->range.sourceStart();
	std::vector<Section *> stack{context.mainSection};
	while (!stack.empty()) {
		Section *section = stack.back();
		stack.pop_back();
		if (!section)
			continue;
		for (Section *child : section->children) {
			if (child)
				stack.push_back(child);
		}
		auto variableIt = section->variables.find(name);
		bool matchesDefinition = false;
		if (variableIt != section->variables.end() && variableIt->second && variableIt->second->definition &&
			sameVariableDefinitionSource(variableIt->second->definition, definition)) {
			matchesDefinition = true;
		}
		if (!matchesDefinition) {
			bool sameDefinitionSection = false;
			for (PatternDefinition *patternDef : section->patternDefinitions) {
				if (!patternDef || !patternDef->range.line)
					continue;
				SourceLocation patternStart = patternDef->range.sourceStart();
				if (!patternStart.sourceFile || !definitionStart.sourceFile)
					continue;
				if (pathutil::toAbsoluteUri(patternStart.sourceFile->uri) !=
						pathutil::toAbsoluteUri(definitionStart.sourceFile->uri) ||
					patternStart.sourceFileLineIndex != definitionStart.sourceFileLineIndex) {
					continue;
				}
				sameDefinitionSection = true;
				break;
			}
			if (!sameDefinitionSection)
				continue;
		}
		size_t instCount = section->instantiations.size();
		if (!best || instCount > bestInstantiationCount) {
			best = section;
			bestInstantiationCount = instCount;
		}
	}
	return best;
}

static Section *findBestSectionForVariableLookup(
	ParseContext &context, Section *ownerSection, VariableReference *definition, const std::string &name
) {
	Section *best = ownerSection;
	size_t bestInstantiationCount = ownerSection ? ownerSection->instantiations.size() : 0;

	std::vector<Section *> stack{context.mainSection};
	while (!stack.empty()) {
		Section *section = stack.back();
		stack.pop_back();
		if (!section)
			continue;
		for (Section *child : section->children) {
			if (child)
				stack.push_back(child);
		}

		Variable *candidateVariable = section->findVariable(name);
		if (!candidateVariable || !candidateVariable->definition)
			continue;
		if (definition && !sameVariableDefinitionSource(candidateVariable->definition, definition))
			continue;

		size_t instantiationCount = section->instantiations.size();
		if (!best || instantiationCount > bestInstantiationCount) {
			best = section;
			bestInstantiationCount = instantiationCount;
		}
	}
	return best;
}

static std::string makeSelectionKey(const ::Range &range) {
	if (!range.line)
		return {};
	SourceLocation start = range.sourceStart();
	SourceLocation end = range.sourceEnd();
	if (!start.sourceFile || !end.sourceFile)
		return {};
	std::ostringstream key;
	key << pathutil::toAbsoluteUri(start.sourceFile->uri) << ':' << start.sourceFileLineIndex << ':' << start.column << '-'
		<< end.sourceFileLineIndex << ':' << end.column;
	return key.str();
}

static std::string makeInstantiationSignature(const std::vector<DataType> &types) {
	if (types.empty())
		return "()";
	std::ostringstream out;
	out << '(';
	for (size_t i = 0; i < types.size(); ++i) {
		if (i > 0)
			out << ", ";
		out << types[i].toString();
	}
	out << ')';
	return out.str();
}

static std::string formatInstantiationKeyValue(const CompileTimeValue &value) {
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return std::to_string(*integer);
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return "9223372036854775808";
	if (const auto *number = std::get_if<double>(&value)) {
		if (std::isfinite(*number)) {
			double rounded = std::round(*number);
			if (std::abs(*number - rounded) < 1e-9) {
				std::ostringstream asInteger;
				asInteger << static_cast<long long>(rounded);
				return asInteger.str();
			}
		}
		std::ostringstream asFloat;
		asFloat << *number;
		return asFloat.str();
	}
	if (const auto *text = std::get_if<std::string>(&value))
		return *text;
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "true" : "false";
	if (const auto *typeRef = std::get_if<TypeReferenceValue>(&value)) {
		return typeRef->type.kind == DataType::Kind::Type ? typeToUserName(typeRef->type.toReferencedType())
														  : typeToUserName(typeRef->type);
	}
	if (const auto *constraint = std::get_if<TypeConstraint>(&value))
		return typeToUserName(*constraint);
	return "?";
}

static std::string makeInstantiationSignature(const InstantiationKey &key) {
	std::string signature = makeInstantiationSignature(key.argumentTypes);
	if (key.compileTimeParameters.empty())
		return signature;
	signature += " {";
	for (size_t i = 0; i < key.compileTimeParameters.size(); ++i) {
		if (i > 0)
			signature += ", ";
		signature +=
			key.compileTimeParameters[i].first + "=" + formatInstantiationKeyValue(key.compileTimeParameters[i].second);
	}
	signature += "}";
	return signature;
}

struct PatternParameterInfo {
	std::string name;
	std::string typeConstraintName;
};

static void collectPatternParameters(
	const std::vector<DefinitionPatternElement> &elements, std::vector<PatternParameterInfo> &outParameters
) {
	for (const auto &elem : elements) {
		if (elem.type == PatternElement::Type::Choice) {
			if (!elem.alternatives.empty())
				collectPatternParameters(elem.alternatives[0], outParameters);
			continue;
		}
		if (elem.type != PatternElement::Type::Variable)
			continue;
		auto it = std::find_if(outParameters.begin(), outParameters.end(), [&](const PatternParameterInfo &existing) {
			return existing.name == elem.text;
		});
		if (it == outParameters.end())
			outParameters.push_back({elem.text, elem.typeConstraintName});
	}
}

static std::string formatInstancePattern(
	const PatternDefinition *definition, const std::vector<DataType> &signatureTypes, const Instantiation &instantiation
) {
	if (!definition)
		return {};

	std::vector<PatternParameterInfo> parameters;
	collectPatternParameters(definition->patternElements, parameters);
	std::unordered_map<std::string, size_t> parameterIndexByName;
	for (size_t i = 0; i < parameters.size(); ++i)
		parameterIndexByName[parameters[i].name] = i;

	auto formatValue = [](const CompileTimeValue &value) -> std::string {
		return formatInstantiationKeyValue(value);
	};

	std::function<void(const std::vector<DefinitionPatternElement> &, std::string &)> appendPatternText =
		[&](const std::vector<DefinitionPatternElement> &elements, std::string &result) {
		for (const auto &elem : elements) {
			if (elem.type == PatternElement::Type::Choice) {
				if (!elem.alternatives.empty())
					appendPatternText(elem.alternatives[0], result);
				continue;
			}
			if (elem.type != PatternElement::Type::Variable) {
				result += elem.text;
				continue;
			}

			const std::string &name = elem.text;
			auto indexIt = parameterIndexByName.find(name);
			std::optional<CompileTimeValue> constantValue;
			auto constIt = instantiation.constantParameterValues.find(name);
			if (constIt != instantiation.constantParameterValues.end() && isCompileTimeKnown(constIt->second))
				constantValue = constIt->second;

			std::string typeName = elem.typeConstraintName;
			if (typeName.empty() && indexIt != parameterIndexByName.end()) {
				size_t index = indexIt->second;
				if (index < signatureTypes.size() && signatureTypes[index].isDeduced())
					typeName = typeToUserName(signatureTypes[index]);
			}

			if (!typeName.empty()) {
				if (constantValue.has_value())
					result += "{" + typeName + ":" + formatValue(*constantValue) + "}";
				else
					result += "{" + typeName + ":" + name + "}";
			} else if (constantValue.has_value()) {
				result += formatValue(*constantValue);
			} else {
				result += name;
			}
		}
	};

	std::string rendered;
	appendPatternText(definition->patternElements, rendered);
	return rendered;
}

static Json buildInstantiationOptions(const Section *ownerSection) {
	Json options = Json::array();
	const PatternDefinition *primaryDefinition =
		(ownerSection && !ownerSection->patternDefinitions.empty()) ? ownerSection->patternDefinitions.front() : nullptr;
	int index = 1;
	for (const auto &[instantiationKey, inst] : ownerSection->instantiations) {
		std::string signature = makeInstantiationSignature(instantiationKey);
		std::string label = formatInstancePattern(primaryDefinition, instantiationKey.argumentTypes, inst);
		if (label.empty())
			label = "DynLex path " + std::to_string(index);
		options.push_back({{"key", signature}, {"label", label}});
		index++;
	}
	return options;
}

static std::vector<DataType> argumentTypesForDefinition(const Expression *expr, PatternDefinition *definition) {
	std::vector<DataType> argTypes;
	if (!expr || !definition || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch)
		return argTypes;

	std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);
	(void)matchingPatternPathIndices(expr->patternMatch->nodesPassed, definition);
	size_t argIndex = 0;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		if (node->type != PatternElement::Type::Variable && node->type != PatternElement::Type::Word)
			continue;
		if (argIndex >= sortedArgs.size())
			break;
		Expression *argExpr = sortedArgs[argIndex++];
		argTypes.push_back(argExpr ? argExpr->type : DataType{});
	}
	return argTypes;
}

#include "dynlexServerInspection.inl"
