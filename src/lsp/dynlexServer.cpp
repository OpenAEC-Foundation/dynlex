#include "dynlexServer.h"
#include "codeLine.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "completion.h"
#include "configDocument.h"
#include "function.h"
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

static Section *findOwningVariableSection(Section *fromSection, VariableReference *targetDefinition, const std::string &name) {
	for (Section *section = fromSection; section; section = section->parent) {
		auto it = section->variables.find(name);
		if (it != section->variables.end() && it->second && it->second->definition == targetDefinition)
			return section;
	}
	return nullptr;
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

static PatternDefinition *matchedPatternDefinitionForHover(const Function *expr) {
	if (!expr || expr->kind != Function::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;

	const std::vector<PatternDefinition *> &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return nullptr;

	std::vector<DataType> argTypes;
	argTypes.reserve(expr->arguments.size());
	for (const Function *arg : expr->arguments)
		argTypes.push_back(arg ? arg->type : DataType{});

	PatternDefinition *matched = selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypes);
	if (!matched)
		matched = defs.front();
	return matched;
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
		if (!codeLine->containsSourceLocation(params.textDocument.uri, params.position.line, params.position.character)) {
			continue;
		}
		int localOffset = codeLine->mapSourceToOffset(params.textDocument.uri, params.position.line, params.position.character);

		// Walk the function tree to find the deepest function at cursor
		if (codeLine->function) {
			Function *expr = findDeepestFunction(codeLine->function, localOffset);
			if (expr) {
				auto target = getDefinitionTarget(expr);
				if (target) {
					Location loc;
					loc.uri = pathutil::toAbsoluteUri(target->line->sourceFile->uri);
					loc.range = convertRange(*target);
					return loc;
				}
			}
		}
		break;
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
		if (annotation.kind != ParseContext::SourceTokenKind::PatternReference)
			continue;
		if (!rangeContainsSource(annotation.range, params.textDocument.uri, params.position.line, params.position.character))
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

	for (CodeLine *codeLine : context->codeLines) {
		if (!codeLine->containsSourceLocation(params.textDocument.uri, params.position.line, params.position.character))
			continue;

		int localOffset = codeLine->mapSourceToOffset(params.textDocument.uri, params.position.line, params.position.character);
		if (!codeLine->function)
			return std::nullopt;

		Function *expr = findDeepestFunction(codeLine->function, localOffset);
		if (PatternDefinition *matchedPattern = matchedPatternDefinitionForHover(expr)) {
			Hover hover;
			hover.contents = matchedPattern->toString();
			hover.range = convertRange(expr->range);
			return hover;
		}
		std::string variableName;
		VariableReference *referenceAtHover = nullptr;
		VariableReference *variableDefinition = nullptr;
		if (expr && expr->kind == Function::Kind::Variable && expr->variable) {
			variableName = expr->variable->name;
			referenceAtHover = expr->variable;
			variableDefinition = expr->variable->definition ? expr->variable->definition : expr->variable;
		} else {
			VariableReference *reference = findVariableReferenceAt(
				codeLine->section, params.textDocument.uri, params.position.line, params.position.character
			);
			if (!reference)
				return std::nullopt;
			variableName = reference->name;
			referenceAtHover = reference;
			variableDefinition = reference->definition ? reference->definition : reference;
		}

		Section *ownerSection = findOwningVariableSection(codeLine->section, variableDefinition, variableName);
		if (!ownerSection || !referenceAtHover)
			return std::nullopt;

		// Multi-instantiation functions are ambiguous without an instantiation picker.
		if (ownerSection->instantiations.size() > 1)
			return std::nullopt;

		std::optional<CompileTimeValue> value;
		if (ownerSection->instantiations.size() == 1) {
			const Instantiation &inst = ownerSection->instantiations.begin()->second;
			auto valueIt = inst.constantValuesByReference.find(referenceAtHover);
			if (valueIt != inst.constantValuesByReference.end())
				value = valueIt->second;
		} else {
			auto valueIt = context->constantValuesByReference.find(referenceAtHover);
			if (valueIt != context->constantValuesByReference.end())
				value = valueIt->second;
		}
		if (!value.has_value() || !isCompileTimeKnown(*value))
			return std::nullopt;

		Hover hover;
		hover.contents = "value: " + formatCompileTimeValue(*value);
		if (expr)
			hover.range = convertRange(expr->range);
		return hover;
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
