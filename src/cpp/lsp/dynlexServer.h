#pragma once
#include "languageServer.h"
#include "parseContext.h"
#include "semanticTokens.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace lsp {

// DynLex-specific language server
// Handles compilation, diagnostics, go-to-definition, and semantic tokens
class DynLexServer : public LanguageServer {
  public:
	explicit DynLexServer(int port = 5007);
	explicit DynLexServer(std::unique_ptr<Transport> transport);
	~DynLexServer() override;

  protected:
	InitializeResult onInitialize(const InitializeParams &params) override;
	void onDidOpen(const DidOpenTextDocumentParams &params) override;
	void onDidChange(const DidChangeTextDocumentParams &params) override;
	void onDidClose(const DidCloseTextDocumentParams &params) override;
	void onActiveCursorChanged(const ActiveCursorParams &params) override;
	CompletionList onCompletion(const TextDocumentPositionParams &params) override;
	std::optional<Location> onDefinition(const TextDocumentPositionParams &params) override;
	std::optional<Hover> onHover(const TextDocumentPositionParams &params) override;
	SemanticTokens onSemanticTokensFull(const SemanticTokensParams &params) override;
	std::string onRenderSemanticTokens(const TextDocumentIdentifier &params) override;
	Json onInstantiationsInDocument(const TextDocumentIdentifier &params) override;
	std::vector<DocumentSymbol> onDocumentSymbol(const DocumentSymbolParams &params) override;
	std::vector<CodeAction> onCodeAction(const CodeActionParams &params) override;
	void onSelectInstantiation(const Json &params) override;

  private:
	struct CursorState {
		std::string uri;
		Position position;
	};

	struct LockedLineState {
		std::unordered_set<std::string> clients;
		std::string committedText;
	};

	// ParseContext per main document URI
	std::unordered_map<std::string, std::shared_ptr<ParseContext>> parseContexts;
	std::unordered_map<std::string, std::shared_ptr<ParseContext>> completionParseContexts;
	std::unordered_map<std::string, std::unique_ptr<TextDocument>> compiledDocuments;
	std::string workspaceRootPath;
	std::unordered_map<std::string, CursorState> activeCursors;
	std::unordered_map<std::string, std::unordered_map<int, LockedLineState>> lockedLinesByUri;

	// Import graph: imported file URI → set of main URIs that import it
	std::unordered_map<std::string, std::unordered_set<std::string>> importedBy;
	std::unordered_map<std::string, std::unordered_set<std::string>> completionImportedBy;

	// Cached LSP diagnostics per main document, grouped by source file URI
	std::unordered_map<std::string, std::unordered_map<std::string, std::vector<Diagnostic>>> diagnosticsPerMain;
	std::unordered_map<std::string, std::string> selectedInstantiationBySelectionKey;

	// Compile a main document and update all tracking state
	void recompileMainDocument(const std::string &uri);
	void recompileDependents(const std::string &uri);
	bool syncCompiledDocument(const std::string &uri, bool ignoreLocks = false);
	void refreshLockedLineBaselines(const std::string &uri);
	bool isStructuralEdit(const DidChangeTextDocumentParams &params) const;
	bool updateCursorLock(const std::string &clientId, const std::optional<CursorState> &nextCursor);

	// Publish merged diagnostics for a file from all main compilations that reference it
	void publishMergedDiagnostics(const std::string &fileUri);

	// Convert DynLex Range to LSP Range
	Range convertRange(const ::Range &range) const;

	// Convert DynLex Diagnostic to LSP Diagnostic
	Diagnostic convertDiagnostic(const ::Diagnostic &diag) const;

	// Publish diagnostics for a document
	void publishDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics);
	void requestSemanticTokensRefresh();

	// Find the ParseContext for a URI (either as a main document or via importedBy)
	ParseContext *findContextFor(const std::string &uri);
	ParseContext *findCompletionContextFor(const std::string &uri);

	// Generate semantic tokens for a document
	std::vector<int> generateSemanticTokens(const std::string &uri);
};

} // namespace lsp
