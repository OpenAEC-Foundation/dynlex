#include "dynlexServer.h"
#include "codeLine.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "completion.h"
#include "configDocument.h"
#include "expression.h"
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
	requestSemanticTokensRefresh();
}

void DynLexServer::onActiveCursorChanged(const ActiveCursorParams &params) {
	std::optional<CursorState> nextCursor;
	if (params.uri && params.position && params.version) {
		auto docIt = documents.find(*params.uri);
		if (docIt == documents.end() || docIt->second->version != *params.version)
			return;
		CursorState cursor{*params.uri, *params.version, *params.position};
		nextCursor = cursor;
	}

	updateCursorLock(params.clientId, nextCursor);
}

ParseContext *DynLexServer::findContextFor(const std::string &uri) {
	struct Candidate {
		ParseContext *context{};
		bool fromImporter = false;
		int score = -1;
	};

	auto scoreContextForUri = [&](ParseContext *context) -> int {
		if (!context || !context->mainSection)
			return -1;
		int score = 0;
		std::vector<Section *> stack{context->mainSection};
		while (!stack.empty()) {
			Section *section = stack.back();
			stack.pop_back();
			if (!section)
				continue;
			for (Section *child : section->children) {
				if (child)
					stack.push_back(child);
			}

			bool hasDefinitionInUri = false;
			for (PatternDefinition *def : section->patternDefinitions) {
				if (!def || !def->range.line || !def->range.line->sourceFile)
					continue;
				if (pathutil::toAbsoluteUri(def->range.line->sourceFile->uri) == uri) {
					hasDefinitionInUri = true;
					break;
				}
			}
			if (!hasDefinitionInUri)
				continue;

			int instCount = static_cast<int>(section->instantiations.size());
			if (instCount > 1)
				score += 1000 + instCount;
			else if (instCount == 1)
				score += 10;
		}
		return score;
	};

	std::vector<Candidate> candidates;
	auto addCandidate = [&](ParseContext *context, bool fromImporter) {
		if (!context)
			return;
		for (const Candidate &existing : candidates) {
			if (existing.context == context)
				return;
		}
		candidates.push_back({context, fromImporter, scoreContextForUri(context)});
	};

	auto ownIt = parseContexts.find(uri);
	if (ownIt != parseContexts.end())
		addCandidate(ownIt->second.get(), false);

	auto importIt = importedBy.find(uri);
	if (importIt != importedBy.end()) {
		for (const auto &mainUri : importIt->second) {
			auto mainCtxIt = parseContexts.find(mainUri);
			if (mainCtxIt != parseContexts.end())
				addCandidate(mainCtxIt->second.get(), true);
		}
	}

	if (candidates.empty())
		return nullptr;

	auto better = [](const Candidate &a, const Candidate &b) {
		if (a.score != b.score)
			return a.score > b.score;
		if (a.fromImporter != b.fromImporter)
			return a.fromImporter;
		return a.context < b.context;
	};
	return std::max_element(candidates.begin(), candidates.end(), [&](const Candidate &lhs, const Candidate &rhs) {
		return better(rhs, lhs);
	})->context;
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
					changed = syncCompiledDocument(previous.uri, false) || changed;
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
	if (docIt == compiledDocuments.end()) {
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
	context->fileSystem = std::make_unique<LspFileSystem>(compiledDocuments);

	// Use the compiler to parse and analyze
	compile(uri, *context);

	// Update import graph
	for (const auto &[path, sourceFile] : context->importedFiles) {
		std::string importedUri = pathutil::toAbsoluteUri(sourceFile->uri);
		if (importedUri != uri) {
			importedBy[importedUri].insert(uri);
		}
	}

	// Group diagnostics by their actual source file
	auto &diagsForMain = diagnosticsPerMain[uri];
	diagsForMain.clear();
	diagsForMain[uri]; // always include main file so it gets cleared
	for (const auto &diag : context->diagnostics) {
		std::string fileUri = diag.range.line ? pathutil::toAbsoluteUri(diag.range.line->sourceFile->uri) : uri;
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

void DynLexServer::recompileDependents(const std::string &uri) {
	if (isConfigDocumentUri(uri)) {
		std::vector<std::string> dependentMainUris;
		for (const auto &[mainUri, context] : parseContexts) {
			if (!context->projectSyntaxConfigPath.empty() && pathutil::toAbsoluteUri(context->projectSyntaxConfigPath) == uri)
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

void DynLexServer::requestSemanticTokensRefresh() { sendRequest("workspace/semanticTokens/refresh", Json::object()); }

CompletionList DynLexServer::onCompletion(const TextDocumentPositionParams &params) {
	auto docIt = documents.find(params.textDocument.uri);
	if (docIt == documents.end()) {
		return {};
	}
	if (isConfigDocumentUri(params.textDocument.uri)) {
		return collectConfigCompletions(*docIt->second, params.position.line, params.position.character);
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

static std::string typeToUserPatternName(const ParseContext &parseContext, const DataType &type) {
	if (type.pointerDepth == 0) {
		if (type.kind == DataType::Kind::Int && type.numericSize > 0)
			return "a " + std::to_string(type.numericSize * 8) + " bit integer";
		if (type.kind == DataType::Kind::Float && type.numericSize > 0)
			return "a " + std::to_string(type.numericSize * 8) + " bit float";
		if (type.kind == DataType::Kind::Bool)
			return "a boolean";
		if (type.kind == DataType::Kind::Void)
			return "nothing";
	}
	auto it = parseContext.typeAliasNames.find(type);
	if (it != parseContext.typeAliasNames.end())
		return it->second;
	return type.toString();
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
	ParseContext &parseContext, const PatternDefinition *definition, const std::vector<DataType> &signatureTypes,
	const Instantiation &instantiation
) {
	if (!definition)
		return {};

	std::vector<PatternParameterInfo> parameters;
	collectPatternParameters(definition->patternElements, parameters);
	std::unordered_map<std::string, size_t> parameterIndexByName;
	for (size_t i = 0; i < parameters.size(); ++i)
		parameterIndexByName[parameters[i].name] = i;

	auto formatValue = [](const CompileTimeValue &value) -> std::string {
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
		return "?";
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
					typeName = typeToUserPatternName(parseContext, signatureTypes[index]);
			}

			if (!elem.typeConstraintName.empty()) {
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

static Json buildInstantiationOptions(ParseContext &parseContext, const Section *ownerSection) {
	Json options = Json::array();
	const PatternDefinition *primaryDefinition =
		(ownerSection && !ownerSection->patternDefinitions.empty()) ? ownerSection->patternDefinitions.front() : nullptr;
	int index = 1;
	for (const auto &[signatureTypes, inst] : ownerSection->instantiations) {
		std::string signature = makeInstantiationSignature(signatureTypes);
		std::string label = formatInstancePattern(parseContext, primaryDefinition, signatureTypes, inst);
		if (label.empty())
			label = "DynLex path " + std::to_string(index) + " " + signature;
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
	size_t argIndex = 0;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		auto paramIt = node->parameterNames.find(definition);
		if (paramIt == node->parameterNames.end())
			continue;
		if (argIndex >= sortedArgs.size())
			break;
		Expression *argExpr = sortedArgs[argIndex++];
		argTypes.push_back(argExpr ? argExpr->type : DataType{});
	}
	return argTypes;
}

static void storeInstantiationSelectionForSection(
	std::unordered_map<std::string, std::string> &selectedInstantiationBySelectionKey, Section *section,
	const std::string &instantiationKey
) {
	if (!section || instantiationKey.empty())
		return;
	for (PatternDefinition *definition : section->patternDefinitions) {
		if (!definition)
			continue;
		std::string key = makeSelectionKey(definition->range);
		if (!key.empty())
			selectedInstantiationBySelectionKey[key] = instantiationKey;
	}
	for (const auto &[_, refs] : section->variableReferences) {
		for (VariableReference *reference : refs) {
			if (!reference)
				continue;
			std::string key = makeSelectionKey(reference->range);
			if (!key.empty())
				selectedInstantiationBySelectionKey[key] = instantiationKey;
		}
	}
}

static bool rangeContainsSource(const ::Range &range, const std::string &uri, int line, int character) {
	if (!range.line)
		return false;
	SourceLocation start = range.sourceStart();
	SourceLocation end = range.sourceEnd();
	if (!start.sourceFile || !end.sourceFile)
		return false;
	if (pathutil::toAbsoluteUri(start.sourceFile->uri) != uri || start.sourceFileLineIndex != line ||
		end.sourceFileLineIndex != line) {
		return false;
	}
	return character >= start.column && character < end.column;
}

static bool rangeContainsSourceForHover(const ::Range &range, const std::string &uri, int line, int character) {
	if (rangeContainsSource(range, uri, line, character))
		return true;
	if (character > 0 && rangeContainsSource(range, uri, line, character - 1))
		return true;
	return false;
}

static VariableReference *findVariableReferenceAt(Section *fromSection, const std::string &uri, int line, int character) {
	for (Section *section = fromSection; section; section = section->parent) {
		for (const auto &[_, refs] : section->variableReferences) {
			for (VariableReference *reference : refs) {
				if (!reference)
					continue;
				if (rangeContainsSource(reference->range, uri, line, character))
					return reference;
			}
		}
	}
	return nullptr;
}

static VariableReference *findVariableReferenceInDocument(
	Section *rootSection, const std::string &uri, int line, int character, Section **outReferenceSection = nullptr
) {
	if (outReferenceSection)
		*outReferenceSection = nullptr;
	if (!rootSection)
		return nullptr;

	std::vector<Section *> stack{rootSection};
	while (!stack.empty()) {
		Section *section = stack.back();
		stack.pop_back();
		if (!section)
			continue;

		for (Section *child : section->children) {
			if (child)
				stack.push_back(child);
		}

		for (const auto &[_, refs] : section->variableReferences) {
			for (VariableReference *reference : refs) {
				if (!reference)
					continue;
				if (!rangeContainsSource(reference->range, uri, line, character))
					continue;
				if (outReferenceSection)
					*outReferenceSection = section;
				return reference;
			}
		}
	}
	return nullptr;
}

static Expression *findVariableExpressionAtSource(Expression *expr, const std::string &uri, int line, int character) {
	if (!expr)
		return nullptr;
	for (Expression *arg : expr->arguments) {
		if (!arg)
			continue;
		if (Expression *nested = findVariableExpressionAtSource(arg, uri, line, character))
			return nested;
	}
	if (expr->kind == Expression::Kind::Variable && rangeContainsSource(expr->range, uri, line, character))
		return expr;
	return nullptr;
}

static std::string formatCompileTimeValue(const CompileTimeValue &value) {
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
		return "\"" + *text + "\"";
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "true" : "false";
	return "?";
}

static Json makeVariableHoverContents(const std::string &typeText, const std::optional<CompileTimeValue> &value) {
	std::ostringstream markdown;
	if (!typeText.empty()) {
		markdown << "type:\n\n```dynlex\n" << typeText << "\n```";
	}
	bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
	if (hasKnownValue) {
		if (!typeText.empty())
			markdown << "\n\n";
		markdown << "value: `" << formatCompileTimeValue(*value) << "`";
	}
	return Json{{"kind", "markdown"}, {"value", markdown.str()}};
}

static std::optional<CompileTimeValue> lookupConstantValueInInstantiation(
	Section *ownerSection, const Instantiation &instantiation, VariableReference *referenceAtHover,
	VariableReference *variableDefinition, const std::string &variableName
) {
	if (referenceAtHover) {
		auto valueIt = instantiation.constantValuesByReference.find(referenceAtHover);
		if (valueIt != instantiation.constantValuesByReference.end())
			return valueIt->second;
	}
	if (variableDefinition) {
		auto defIt = instantiation.constantValuesByReference.find(variableDefinition);
		if (defIt != instantiation.constantValuesByReference.end())
			return defIt->second;
	}
	auto parameterIt = instantiation.constantParameterValues.find(variableName);
	if (parameterIt == instantiation.constantParameterValues.end() && variableDefinition &&
		variableDefinition->name != variableName) {
		parameterIt = instantiation.constantParameterValues.find(variableDefinition->name);
	}
	if (parameterIt != instantiation.constantParameterValues.end() && isCompileTimeKnown(parameterIt->second))
		return parameterIt->second;
	(void)ownerSection;
	return std::nullopt;
}

static std::optional<CompileTimeValue> lookupHoverConstantValue(
	const ParseContext &parseContext, Section *ownerSection, VariableReference *referenceAtHover,
	VariableReference *variableDefinition, const std::string &variableName, const std::string &selectionKey,
	const std::unordered_map<std::string, std::string> &selectedInstantiationBySelectionKey
) {
	if (ownerSection && ownerSection->instantiations.size() == 1) {
		const Instantiation &inst = ownerSection->instantiations.begin()->second;
		return lookupConstantValueInInstantiation(ownerSection, inst, referenceAtHover, variableDefinition, variableName);
	}

	if (ownerSection && ownerSection->instantiations.size() > 1) {
		auto selectedIt = selectedInstantiationBySelectionKey.find(selectionKey);
		std::string selectedKey;
		if (selectedIt != selectedInstantiationBySelectionKey.end())
			selectedKey = selectedIt->second;
		else if (!ownerSection->instantiations.empty())
			selectedKey = makeInstantiationSignature(ownerSection->instantiations.begin()->first);
		for (const auto &[signatureTypes, inst] : ownerSection->instantiations) {
			if (makeInstantiationSignature(signatureTypes) != selectedKey)
				continue;
			return lookupConstantValueInInstantiation(ownerSection, inst, referenceAtHover, variableDefinition, variableName);
		}
		return std::nullopt;
	}

	if (referenceAtHover) {
		auto valueIt = parseContext.constantValuesByReference.find(referenceAtHover);
		if (valueIt != parseContext.constantValuesByReference.end())
			return valueIt->second;
	}
	if (variableDefinition) {
		auto defIt = parseContext.constantValuesByReference.find(variableDefinition);
		if (defIt != parseContext.constantValuesByReference.end())
			return defIt->second;
	}

	return std::nullopt;
}

static std::optional<CompileTimeValue> lookupConstantValueByNameInOwnerSection(
	const ParseContext &parseContext, Section *ownerSection, VariableReference *definition, const std::string &name,
	const std::string &selectionKey, const std::unordered_map<std::string, std::string> &selectedInstantiationBySelectionKey
) {
	return lookupHoverConstantValue(
		parseContext, ownerSection, nullptr, definition, name, selectionKey, selectedInstantiationBySelectionKey
	);
}

static PatternDefinition *matchedPatternDefinitionForHover(const Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;

	const std::vector<PatternDefinition *> &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return nullptr;

	std::vector<DataType> argTypes;
	argTypes.reserve(expr->arguments.size());
	for (const Expression *arg : expr->arguments)
		argTypes.push_back(arg ? arg->type : DataType{});

	PatternDefinition *matched = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypes);
	if (!matched)
		matched = defs.front();
	return matched;
}

struct CursorResolution {
	CodeLine *codeLine{};
	int localOffset = -1;
	Expression *expr{};
	PatternDefinition *matchedPattern{};
	VariableReference *referenceAtCursor{};
	VariableReference *definitionAtCursor{};
	std::string variableName;
};

static std::optional<CursorResolution>
resolveCursorData(ParseContext &context, const std::string &uri, int line, int character) {
	for (CodeLine *codeLine : context.codeLines) {
		if (!codeLine || !codeLine->containsSourceLocation(uri, line, character) || !codeLine->expression)
			continue;
		int localOffset = codeLine->mapSourceToOffset(uri, line, character);
		if (localOffset < 0)
			continue;

		Expression *expr = findDeepestExpression(codeLine->expression, localOffset);
		if (Expression *sourceVariable = findVariableArgumentAtOffset(codeLine->expression, localOffset))
			expr = sourceVariable;
		else if (Expression *sourceVariable = findVariableExpressionAtSource(codeLine->expression, uri, line, character))
			expr = sourceVariable;

		CursorResolution resolved;
		resolved.codeLine = codeLine;
		resolved.localOffset = localOffset;
		resolved.expr = expr;
		resolved.matchedPattern = matchedPatternDefinitionForHover(expr);
		if (expr && expr->kind == Expression::Kind::Variable && expr->variable) {
			resolved.referenceAtCursor = expr->variable;
			resolved.definitionAtCursor = expr->variable->definition ? expr->variable->definition : expr->variable;
			resolved.variableName = expr->variable->name;
		} else {
			VariableReference *reference = findVariableReferenceAt(codeLine->section, uri, line, character);
			if (reference) {
				resolved.referenceAtCursor = reference;
				resolved.definitionAtCursor = reference->definition ? reference->definition : reference;
				resolved.variableName = reference->name;
			}
		}
		return resolved;
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

	std::optional<CursorResolution> resolved =
		resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character);
	if (!resolved.has_value())
		return std::nullopt;
	if (resolved->expr && resolved->expr->kind == Expression::Kind::PatternCall && resolved->matchedPattern) {
		Section *targetSection = resolved->matchedPattern->section;
		if (targetSection && targetSection->instantiations.size() > 1) {
			std::vector<DataType> callArgTypes = argumentTypesForDefinition(resolved->expr, resolved->matchedPattern);
			std::string key = makeInstantiationSignature(callArgTypes);
			if (targetSection->instantiations.contains(callArgTypes))
				storeInstantiationSelectionForSection(selectedInstantiationBySelectionKey, targetSection, key);
		}
	}
	if (resolved->expr) {
		auto target = getDefinitionTarget(resolved->expr);
		if (target) {
			Location loc;
			loc.uri = pathutil::toAbsoluteUri(target->line->sourceFile->uri);
			loc.range = convertRange(*target);
			return loc;
		}
	}

	return std::nullopt;
}

std::optional<Hover> DynLexServer::onHover(const TextDocumentPositionParams &params) {
	if (isConfigDocumentUri(params.textDocument.uri))
		return std::nullopt;

	ParseContext *context = findContextFor(params.textDocument.uri);
	if (!hasCompilationStage(context, ParseContext::CompilationStage::InferredTypes))
		return std::nullopt;

	for (const ParseContext::SourceTokenAnnotation &annotation : context->sourceTokenAnnotations) {
		if (annotation.kind != ParseContext::SourceTokenKind::Variable)
			continue;
		if (!rangeContainsSourceForHover(
				annotation.range, params.textDocument.uri, params.position.line, params.position.character
			))
			continue;
		if (!annotation.range.line || !annotation.range.line->section)
			continue;
		const std::string variableName = std::string(annotation.range.subString);
		Variable *resolvedVariable = annotation.range.line->section->findVariable(variableName);
		if (!resolvedVariable)
			continue;
		Section *ownerSection = (resolvedVariable->definition && resolvedVariable->definition->range.line)
									? resolvedVariable->definition->range.line->section
									: annotation.range.line->section;
		if (!ownerSection)
			continue;
		Variable *ownedVariable = resolvedVariable;
		if (ownedVariable->definition) {
			if (Section *instantiatedOwner =
					findInstantiatedOwnerSectionForDefinition(*context, ownedVariable->definition, variableName)) {
				ownerSection = instantiatedOwner;
			}
		}
		ownerSection = findBestSectionForVariableLookup(*context, ownerSection, ownedVariable->definition, variableName);
		std::string selectionKey = makeSelectionKey(annotation.range);
		std::optional<CompileTimeValue> value = lookupConstantValueByNameInOwnerSection(
			*context, ownerSection, ownedVariable->definition, variableName, selectionKey, selectedInstantiationBySelectionKey
		);
		bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
		std::string typeText = typeToUserPatternName(*context, ownedVariable->type);
		if (typeText.empty() && !hasKnownValue)
			continue;

		Hover hover;
		hover.contents = makeVariableHoverContents(typeText, value);
		hover.range = convertRange(annotation.range);
		return hover;
	}

	for (const ParseContext::SourceTokenAnnotation &annotation : context->sourceTokenAnnotations) {
		if (annotation.kind != ParseContext::SourceTokenKind::PatternReference)
			continue;
		if (!rangeContainsSourceForHover(
				annotation.range, params.textDocument.uri, params.position.line, params.position.character
			))
			continue;
		PatternDefinition *definition =
			findDefinitionBySignature(*context, annotation.referencedPatternType, annotation.range.subString);
		if (!definition)
			return std::nullopt;
		Hover hover;
		hover.contents = definition->toString();
		hover.range = convertRange(annotation.range);
		return hover;
	}

	std::optional<Hover> definitionHover;
	{
		std::vector<Section *> stack{context->mainSection};
		while (!stack.empty()) {
			Section *section = stack.back();
			stack.pop_back();
			if (!section)
				continue;
			for (Section *child : section->children) {
				if (child)
					stack.push_back(child);
			}
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition)
					continue;
				if (!rangeContainsSource(
						definition->range, params.textDocument.uri, params.position.line, params.position.character
					))
					continue;
				Hover hover;
				hover.contents = definition->toString();
				hover.range = convertRange(definition->range);
				definitionHover = std::move(hover);
				stack.clear();
				break;
			}
		}
	}

	std::optional<CursorResolution> resolved =
		resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character);
	if (!resolved.has_value() && params.position.character > 0) {
		resolved = resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character - 1);
	} else if (resolved.has_value() && !resolved->referenceAtCursor && params.position.character > 0) {
		std::optional<CursorResolution> previousCharResolved =
			resolveCursorData(*context, params.textDocument.uri, params.position.line, params.position.character - 1);
		if (previousCharResolved.has_value() && previousCharResolved->referenceAtCursor)
			resolved = std::move(previousCharResolved);
	}
	if (resolved.has_value()) {
		Expression *expr = resolved->expr;
		PatternDefinition *matchedPattern = resolved->matchedPattern;
		VariableReference *referenceAtHover = resolved->referenceAtCursor;
		VariableReference *variableDefinition = resolved->definitionAtCursor;
		std::string variableName = resolved->variableName;
		if (!referenceAtHover || !variableDefinition) {
			if (matchedPattern) {
				Hover hover;
				hover.contents = matchedPattern->toString();
				hover.range = convertRange(expr->range);
				return hover;
			}
		} else {
			Section *ownerSection = nullptr;
			if (variableDefinition && variableDefinition->range.line && variableDefinition->range.line->section)
				ownerSection = variableDefinition->range.line->section;
			if (!ownerSection && referenceAtHover->range.line && referenceAtHover->range.line->section)
				ownerSection =
					findOwningVariableSection(referenceAtHover->range.line->section, variableDefinition, variableName);
			if (!ownerSection) {
				ownerSection = findOwningVariableSectionAtSource(
					*context, params.textDocument.uri, params.position.line, params.position.character, variableDefinition,
					variableName
				);
			}
			if (Section *instantiatedOwner =
					findInstantiatedOwnerSectionForDefinition(*context, variableDefinition, variableName)) {
				ownerSection = instantiatedOwner;
			}
			ownerSection = findBestSectionForVariableLookup(*context, ownerSection, variableDefinition, variableName);
			if (ownerSection) {
				std::string selectionKey = makeSelectionKey(referenceAtHover->range);

				Variable *ownedVariable = nullptr;
				auto variableIt = ownerSection->variables.find(variableName);
				if (variableIt != ownerSection->variables.end())
					ownedVariable = variableIt->second;
				if (!ownedVariable)
					ownedVariable = ownerSection->findVariable(variableName);

				std::string typeText;
				if (ownedVariable)
					typeText = typeToUserPatternName(*context, ownedVariable->type);
				else if (expr && expr->kind == Expression::Kind::Variable)
					typeText = typeToUserPatternName(*context, expr->type);

				std::optional<CompileTimeValue> value = lookupHoverConstantValue(
					*context, ownerSection, referenceAtHover, variableDefinition, variableName, selectionKey,
					selectedInstantiationBySelectionKey
				);
				bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
				if (typeText.empty() && !hasKnownValue)
					return std::nullopt;

				Hover hover;
				hover.contents = makeVariableHoverContents(typeText, value);
				if (expr)
					hover.range = convertRange(expr->range);
				return hover;
			}
			if (matchedPattern) {
				Hover hover;
				hover.contents = matchedPattern->toString();
				hover.range = convertRange(expr->range);
				return hover;
			}
		}
	}

	Section *referenceSection = nullptr;
	VariableReference *referenceAtHover = findVariableReferenceInDocument(
		context->mainSection, params.textDocument.uri, params.position.line, params.position.character, &referenceSection
	);
	if (!referenceAtHover) {
		if (definitionHover.has_value())
			return definitionHover;
		return std::nullopt;
	}

	std::string variableName = referenceAtHover->name;
	VariableReference *variableDefinition = referenceAtHover->definition ? referenceAtHover->definition : referenceAtHover;
	Section *ownerSection = findOwningVariableSectionAtSource(
		*context, params.textDocument.uri, params.position.line, params.position.character, variableDefinition, variableName
	);
	if (!ownerSection) {
		if (definitionHover.has_value())
			return definitionHover;
		return std::nullopt;
	}
	std::string selectionKey = makeSelectionKey(referenceAtHover->range);

	Variable *ownedVariable = nullptr;
	auto variableIt = ownerSection->variables.find(variableName);
	if (variableIt != ownerSection->variables.end())
		ownedVariable = variableIt->second;

	std::string typeText;
	if (ownedVariable)
		typeText = typeToUserPatternName(*context, ownedVariable->type);

	std::optional<CompileTimeValue> value = lookupHoverConstantValue(
		*context, ownerSection, referenceAtHover, variableDefinition, variableName, selectionKey,
		selectedInstantiationBySelectionKey
	);
	bool hasKnownValue = value.has_value() && isCompileTimeKnown(*value);
	if (typeText.empty() && !hasKnownValue) {
		if (definitionHover.has_value())
			return definitionHover;
		return std::nullopt;
	}

	Hover hover;
	hover.contents = makeVariableHoverContents(typeText, value);
	hover.range = convertRange(referenceAtHover->range);
	return hover;
}

Json DynLexServer::onInstantiationsInDocument(const TextDocumentIdentifier &params) {
	if (isConfigDocumentUri(params.uri))
		return Json::array();

	ParseContext *context = findContextFor(params.uri);
	if (!hasCompilationStage(context, ParseContext::CompilationStage::InferredTypes))
		return Json::array();

	Json entries = Json::array();
	std::unordered_set<std::string> seenSelectionKeys;
	std::vector<Section *> stack{context->mainSection};
	while (!stack.empty()) {
		Section *section = stack.back();
		stack.pop_back();
		if (!section)
			continue;
		for (Section *child : section->children) {
			if (child)
				stack.push_back(child);
		}
		for (const auto &[_, refs] : section->variableReferences) {
			for (VariableReference *referenceAtHover : refs) {
				if (!referenceAtHover || !referenceAtHover->range.line || !referenceAtHover->range.line->sourceFile)
					continue;
				if (pathutil::toAbsoluteUri(referenceAtHover->range.line->sourceFile->uri) != params.uri)
					continue;

				std::string variableName = referenceAtHover->name;
				VariableReference *variableDefinition =
					referenceAtHover->definition ? referenceAtHover->definition : referenceAtHover;
				Section *ownerSection = findOwningVariableSection(section, variableDefinition, variableName);
				if (!ownerSection || ownerSection->instantiations.empty())
					continue;

				std::string selectionKey = makeSelectionKey(referenceAtHover->range);
				if (selectionKey.empty() || seenSelectionKeys.contains(selectionKey))
					continue;
				seenSelectionKeys.insert(selectionKey);

				Json options = buildInstantiationOptions(*context, ownerSection);
				if (options.empty())
					continue;

				std::string currentKey;
				auto selectedIt = selectedInstantiationBySelectionKey.find(selectionKey);
				if (selectedIt != selectedInstantiationBySelectionKey.end())
					currentKey = selectedIt->second;
				if (currentKey.empty())
					currentKey = options[0].at("key").get<std::string>();

				entries.push_back({
					{"selectionKey", selectionKey},
					{"currentKey", currentKey},
					{"range", convertRange(referenceAtHover->range)},
					{"options", options},
				});
			}
		}

		if (!section->instantiations.empty()) {
			Json options = buildInstantiationOptions(*context, section);
			if (!options.empty()) {
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition || !definition->range.line || !definition->range.line->sourceFile)
						continue;
					if (pathutil::toAbsoluteUri(definition->range.line->sourceFile->uri) != params.uri)
						continue;
					std::string selectionKey = makeSelectionKey(definition->range);
					if (selectionKey.empty() || seenSelectionKeys.contains(selectionKey))
						continue;
					seenSelectionKeys.insert(selectionKey);

					std::string currentKey;
					auto selectedIt = selectedInstantiationBySelectionKey.find(selectionKey);
					if (selectedIt != selectedInstantiationBySelectionKey.end())
						currentKey = selectedIt->second;
					if (currentKey.empty())
						currentKey = options[0].at("key").get<std::string>();

					entries.push_back({
						{"selectionKey", selectionKey},
						{"currentKey", currentKey},
						{"range", convertRange(definition->range)},
						{"options", options},
					});
				}
			}
		}
	}
	return entries;
}

void DynLexServer::onSelectInstantiation(const Json &params) {
	if (!params.is_object())
		return;
	if (!params.contains("selectionKey") || !params.contains("instantiationKey"))
		return;
	if (!params.at("selectionKey").is_string() || !params.at("instantiationKey").is_string())
		return;

	std::string selectionKey = params.at("selectionKey").get<std::string>();
	std::string instantiationKey = params.at("instantiationKey").get<std::string>();
	if (selectionKey.empty())
		return;
	if (instantiationKey.empty()) {
		selectedInstantiationBySelectionKey.erase(selectionKey);
		return;
	}
	selectedInstantiationBySelectionKey[selectionKey] = instantiationKey;
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
			if (!def->range.line || pathutil::toAbsoluteUri(def->range.line->sourceFile->uri) != params.textDocument.uri) {
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
				if (pathutil::toAbsoluteUri(last->sourceFile->uri) == params.textDocument.uri &&
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

std::string DynLexServer::onRenderSemanticTokens(const TextDocumentIdentifier &params) {
	auto docIt = documents.find(params.uri);
	if (docIt == documents.end())
		return {};
	return renderTaggedSemanticTokensFromData(docIt->second->content, generateSemanticTokens(params.uri));
}

std::vector<int> DynLexServer::generateSemanticTokens(const std::string &uri) {
	if (isConfigDocumentUri(uri)) {
		auto docIt = documents.find(uri);
		if (docIt == documents.end())
			return {};
		return encodeConfigSemanticTokens(*docIt->second);
	}
	ParseContext *context = findContextFor(uri);
	auto docIt = documents.find(uri);
	if (docIt == documents.end()) {
		return {};
	}

	std::vector<std::vector<SemanticToken>> tokensByLine(docIt->second->lineCount());
	if (hasCompilationStage(context, ParseContext::CompilationStage::AnalyzedSections)) {
		tokensByLine = collectSemanticTokens(*context, uri, docIt->second->lineCount(), true);
		if (tokensByLine.size() < static_cast<size_t>(docIt->second->lineCount()))
			tokensByLine.resize(docIt->second->lineCount());
	}

	auto lockedIt = lockedLinesByUri.find(uri);
	if (lockedIt != lockedLinesByUri.end()) {
		for (const auto &[lineIndex, state] : lockedIt->second) {
			if (lineIndex < 0 || lineIndex >= docIt->second->lineCount())
				continue;
			// Keep compiler semantic tokens when the locked line still matches
			// the last compiled baseline; only fall back to live lexing for
			// actively edited (diverged) lines.
			if (std::string(docIt->second->getLine(lineIndex)) == state.committedText)
				continue;
			tokensByLine[lineIndex] = collectLiveLineSemanticTokens(context, *docIt->second, uri, lineIndex);
		}
	}

	return encodeSemanticTokens(tokensByLine);
}

} // namespace lsp
