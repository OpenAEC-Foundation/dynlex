#pragma once
#include "languageServer.h"
#include "parseContext.h"
#include "semanticTokens.h"
#include <memory>
#include <unordered_map>

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

  private:
	// ParseContext per document URI
	std::unordered_map<std::string, std::unique_ptr<ParseContext>> parseContexts;

	// Recompile a document and publish diagnostics
	void recompileDocument(const std::string &uri);

	// Convert DynLex Range to LSP Range
	Range convertRange(const ::Range &range) const;

	// Convert DynLex Diagnostic to LSP Diagnostic
	Diagnostic convertDiagnostic(const ::Diagnostic &diag) const;

	// Publish diagnostics for a document
	void publishDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics);

	// Find the element at a given position
	struct PositionInfo {
		VariableReference *variableRef = nullptr;
		PatternReference *patternRef = nullptr;
		Section *section = nullptr;
	};
	PositionInfo findElementAtPosition(const std::string &uri, const Position &pos);

	// Generate semantic tokens for a document
	std::vector<int> generateSemanticTokens(const std::string &uri);
};

} // namespace lsp
