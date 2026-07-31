#pragma once
#include "lspProtocol.h"
#include "textDocument.h"
#include "transport.h"
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace lsp {

// Base class for a Language Server Protocol server
// Handles transport, message framing, and request dispatch
class LanguageServer {
  public:
	// TCP mode: listens on port, accepts connections
	explicit LanguageServer(int port = 5007);

	// Transport mode: uses provided transport directly (e.g., StdioTransport)
	explicit LanguageServer(std::unique_ptr<Transport> transport);

	virtual ~LanguageServer();

	// Start the server (blocks until shutdown)
	bool run();

	// Stop the server
	void shutdown();

	// Enable raw JSON-RPC tracing to stderr or a file path.
	bool enableTrace(const std::string &path = "");

	// Process one decoded JSON-RPC message. Embedders use this to share the
	// normal protocol dispatcher without running a blocking transport loop.
	void processMessage(const Json &message);

  protected:
	// Override these in derived classes for language-specific behavior

	// Called when client sends initialize request
	virtual InitializeResult onInitialize(const InitializeParams &params);

	// Called when client opens a document
	virtual void onDidOpen(const DidOpenTextDocumentParams &params);

	// Called when client changes a document
	virtual void onDidChange(const DidChangeTextDocumentParams &params);

	// Called when client closes a document
	virtual void onDidClose(const DidCloseTextDocumentParams &params);

	// Called when client saves a document
	virtual void onDidSave(const DidSaveTextDocumentParams &params);

	// Called for DynLex custom cursor tracking notifications.
	virtual void onActiveCursorChanged(const ActiveCursorParams &params);

	// Called for go-to-definition request
	virtual std::optional<Location> onDefinition(const TextDocumentPositionParams &params);

	// Called for hover requests
	virtual std::optional<Hover> onHover(const TextDocumentPositionParams &params);

	// Called for completion requests
	virtual CompletionList onCompletion(const TextDocumentPositionParams &params);

	// Called for semantic tokens request
	virtual SemanticTokens onSemanticTokensFull(const SemanticTokensParams &params);

	// Called for document symbol request
	virtual std::vector<DocumentSymbol> onDocumentSymbol(const DocumentSymbolParams &params);

	// Called for code action request
	virtual std::vector<CodeAction> onCodeAction(const CodeActionParams &params);

	// Called for DynLex debug request returning tagged semantic token output.
	virtual std::string onRenderSemanticTokens(const TextDocumentIdentifier &params);

	// Called for DynLex request returning instantiation choices for a document.
	virtual Json onInstantiationsInDocument(const TextDocumentIdentifier &params);

	// Called for DynLex request returning resolved source call expressions.
	virtual Json onCallExpressions(const TextDocumentIdentifier &params);

	// Called for DynLex request returning an already-loaded document.
	virtual std::optional<std::string> onReadDocument(const TextDocumentIdentifier &params);

	// Called for DynLex notification selecting an instantiation choice.
	virtual void onSelectInstantiation(const Json &params);

	// Send a notification to the client (e.g., publishDiagnostics)
	void sendNotification(const std::string &method, const Json &params);
	void sendRequest(const std::string &method, const Json &params);

	// Document storage
	std::unordered_map<std::string, std::unique_ptr<TextDocument>> documents;

  private:
#ifndef DYNLEX_WEB
	int port = 0;
#endif
	std::unique_ptr<Transport> transport;
	bool running = false;
	int nextRequestId = 1;
	std::ostream *traceStream = nullptr;
	std::unique_ptr<std::ofstream> traceFile;

	// Handle a single connection (reads messages until disconnect)
	void handleConnection();

	// Message framing
	std::string readMessage();
	void sendMessage(const Json &message);

	// Request dispatch
	void handleMessage(const Json &message);
	void handleRequest(const Json &message);
	void handleResponse(const Json &message);
	void handleNotification(const Json &message);

	// Send a response
	void sendResponse(const Json &id, const Json &result);
	void sendError(const Json &id, int code, const std::string &message);

	// Logging (to stderr, so it doesn't interfere with stdio transport)
	void log(const std::string &message);
	void logError(const std::string &message);
	void traceMessage(std::string_view direction, const std::string &body);
};

} // namespace lsp
