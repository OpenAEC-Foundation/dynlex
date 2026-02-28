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
	std::optional<Location> onDefinition(const TextDocumentPositionParams &params) override;
	SemanticTokens onSemanticTokensFull(const SemanticTokensParams &params) override;
	std::vector<DocumentSymbol> onDocumentSymbol(const DocumentSymbolParams &params) override;
	std::vector<CodeAction> onCodeAction(const CodeActionParams &params) override;

  private:
	// ParseContext per main document URI
	std::unordered_map<std::string, std::unique_ptr<ParseContext>> parseContexts;

	// Import graph: imported file URI → set of main URIs that import it
	std::unordered_map<std::string, std::unordered_set<std::string>> importedBy;

	// Cached LSP diagnostics per main document, grouped by source file URI
	std::unordered_map<std::string, std::unordered_map<std::string, std::vector<Diagnostic>>> diagnosticsPerMain;

	// Compile a main document and update all tracking state
	void recompileMainDocument(const std::string &uri);

	// Publish merged diagnostics for a file from all main compilations that reference it
	void publishMergedDiagnostics(const std::string &fileUri);

	// Convert DynLex Range to LSP Range
	Range convertRange(const ::Range &range) const;

	// Convert DynLex Diagnostic to LSP Diagnostic
	Diagnostic convertDiagnostic(const ::Diagnostic &diag) const;

	// Publish diagnostics for a document
	void publishDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics);

	// Find the ParseContext for a URI (either as a main document or via importedBy)
	ParseContext *findContextFor(const std::string &uri);

	// Generate semantic tokens for a document
	std::vector<int> generateSemanticTokens(const std::string &uri);
};

} // namespace lsp
