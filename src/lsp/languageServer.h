#pragma once
#include "lspProtocol.h"
#include "textDocument.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace lsp {

// Base class for a Language Server Protocol server
// Handles TCP socket, message framing, and request dispatch
class LanguageServer {
  public:
	explicit LanguageServer(int port = 5007);
	virtual ~LanguageServer();

	// Start the server (blocks until shutdown)
	void run();

	// Stop the server
	void shutdown();

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

	// Called for go-to-definition request
	virtual std::optional<Location> onDefinition(const TextDocumentPositionParams &params);

	// Called for semantic tokens request
	virtual SemanticTokens onSemanticTokensFull(const SemanticTokensParams &params);

	// Send a notification to the client (e.g., publishDiagnostics)
	void sendNotification(const std::string &method, const Json &params);

	// Document storage
	std::unordered_map<std::string, std::unique_ptr<TextDocument>> documents;

  private:
	int port;
	int serverSocket = -1;
	int clientSocket = -1;
	bool running = false;

	// TCP handling
	bool setupSocket();
	bool acceptConnection();
	void handleConnection();
	void closeConnection();

	// Message framing
	std::string readMessage();
	void sendMessage(const Json &message);

	// Request dispatch
	void handleMessage(const Json &message);
	void handleRequest(const Json &message);
	void handleNotification(const Json &message);

	// Send a response
	void sendResponse(const Json &id, const Json &result);
	void sendError(const Json &id, int code, const std::string &message);

	// Logging
	void log(const std::string &message);
	void logError(const std::string &message);
};

} // namespace lsp
