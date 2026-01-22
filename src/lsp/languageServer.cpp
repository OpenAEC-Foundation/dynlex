#include "languageServer.h"
#include "lspProtocol.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace lsp {

LanguageServer::LanguageServer(int port) : port(port) {}

LanguageServer::~LanguageServer() { shutdown(); }

void LanguageServer::run() {
	if (!setupSocket()) {
		logError("Failed to setup socket");
		return;
	}

	running = true;
	log("Language server listening on port " + std::to_string(port));

	while (running) {
		if (acceptConnection()) {
			handleConnection();
			closeConnection();
		}
	}

	if (serverSocket >= 0) {
		close(serverSocket);
		serverSocket = -1;
	}
}

void LanguageServer::shutdown() {
	running = false;
	closeConnection();
	if (serverSocket >= 0) {
		close(serverSocket);
		serverSocket = -1;
	}
}

bool LanguageServer::setupSocket() {
	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket < 0) {
		logError("Failed to create socket: " + std::string(strerror(errno)));
		return false;
	}

	// Allow socket reuse
	int opt = 1;
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		logError("Failed to set socket options: " + std::string(strerror(errno)));
		return false;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(serverSocket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
		logError("Failed to bind socket: " + std::string(strerror(errno)));
		return false;
	}

	if (listen(serverSocket, 1) < 0) {
		logError("Failed to listen on socket: " + std::string(strerror(errno)));
		return false;
	}

	return true;
}

bool LanguageServer::acceptConnection() {
	struct sockaddr_in clientAddr;
	socklen_t clientLen = sizeof(clientAddr);

	clientSocket = accept(serverSocket, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
	if (clientSocket < 0) {
		if (running) {
			logError("Failed to accept connection: " + std::string(strerror(errno)));
		}
		return false;
	}

	log("Client connected");
	return true;
}

void LanguageServer::handleConnection() {
	while (running && clientSocket >= 0) {
		std::string message = readMessage();
		if (message.empty()) {
			break;
		}

		try {
			Json j = Json::parse(message);
			handleMessage(j);
		} catch (const Json::parse_error &e) {
			logError("JSON parse error: " + std::string(e.what()));
		}
	}
}

void LanguageServer::closeConnection() {
	if (clientSocket >= 0) {
		close(clientSocket);
		clientSocket = -1;
		log("Client disconnected");
	}
}

std::string LanguageServer::readMessage() {
	// Read headers until empty line
	std::string headers;
	char c;
	int consecutiveNewlines = 0;

	while (running && clientSocket >= 0) {
		ssize_t n = recv(clientSocket, &c, 1, 0);
		if (n <= 0) {
			return "";
		}

		headers += c;

		if (c == '\n') {
			consecutiveNewlines++;
			if (consecutiveNewlines >= 2 || (headers.size() >= 4 && headers.substr(headers.size() - 4) == "\r\n\r\n")) {
				break;
			}
		} else if (c != '\r') {
			consecutiveNewlines = 0;
		}
	}

	// Parse Content-Length header
	size_t contentLength = 0;
	std::string contentLengthKey = "Content-Length:";
	size_t pos = headers.find(contentLengthKey);
	if (pos != std::string::npos) {
		pos += contentLengthKey.length();
		while (pos < headers.size() && headers[pos] == ' ')
			pos++;
		size_t endPos = headers.find_first_of("\r\n", pos);
		std::string lengthStr = headers.substr(pos, endPos - pos);
		contentLength = std::stoull(lengthStr);
	}

	if (contentLength == 0) {
		logError("No Content-Length header found");
		return "";
	}

	// Read body
	std::string body(contentLength, '\0');
	size_t totalRead = 0;
	while (totalRead < contentLength && running && clientSocket >= 0) {
		ssize_t n = recv(clientSocket, &body[totalRead], contentLength - totalRead, 0);
		if (n <= 0) {
			return "";
		}
		totalRead += n;
	}

	return body;
}

void LanguageServer::sendMessage(const Json &message) {
	std::string body = message.dump();
	std::string header = "Content-Length: " + std::to_string(body.length()) + "\r\n\r\n";
	std::string fullMessage = header + body;

	size_t totalSent = 0;
	while (totalSent < fullMessage.size() && clientSocket >= 0) {
		ssize_t n = send(clientSocket, fullMessage.c_str() + totalSent, fullMessage.size() - totalSent, 0);
		if (n <= 0) {
			logError("Failed to send message: " + std::string(strerror(errno)));
			return;
		}
		totalSent += n;
	}
}

void LanguageServer::handleMessage(const Json &message) {
	if (message.contains("id")) {
		handleRequest(message);
	} else {
		handleNotification(message);
	}
}

void LanguageServer::handleRequest(const Json &message) {
	std::string method = message.at("method").get<std::string>();
	Json id = message.at("id");
	Json params = message.value("params", Json::object());

	log("Request: " + method);

	try {
		if (method == "initialize") {
			InitializeParams p = params.get<InitializeParams>();
			InitializeResult result = onInitialize(p);
			sendResponse(id, result);
		} else if (method == "shutdown") {
			sendResponse(id, nullptr);
		} else if (method == "textDocument/definition") {
			TextDocumentPositionParams p = params.get<TextDocumentPositionParams>();
			auto result = onDefinition(p);
			if (result) {
				sendResponse(id, *result);
			} else {
				sendResponse(id, nullptr);
			}
		} else if (method == "textDocument/semanticTokens/full") {
			SemanticTokensParams p = params.get<SemanticTokensParams>();
			SemanticTokens result = onSemanticTokensFull(p);
			sendResponse(id, result);
		} else {
			sendError(id, -32601, "Method not found: " + method);
		}
	} catch (const std::exception &e) {
		logError("Error handling request " + method + ": " + e.what());
		sendError(id, -32603, "Internal error: " + std::string(e.what()));
	}
}

void LanguageServer::handleNotification(const Json &message) {
	std::string method = message.at("method").get<std::string>();
	Json params = message.value("params", Json::object());

	log("Notification: " + method);

	try {
		if (method == "initialized") {
			// Client finished initialization
		} else if (method == "exit") {
			running = false;
		} else if (method == "textDocument/didOpen") {
			DidOpenTextDocumentParams p = params.get<DidOpenTextDocumentParams>();
			onDidOpen(p);
		} else if (method == "textDocument/didChange") {
			DidChangeTextDocumentParams p = params.get<DidChangeTextDocumentParams>();
			onDidChange(p);
		} else if (method == "textDocument/didClose") {
			DidCloseTextDocumentParams p = params.get<DidCloseTextDocumentParams>();
			onDidClose(p);
		} else if (method == "textDocument/didSave") {
			DidSaveTextDocumentParams p = params.get<DidSaveTextDocumentParams>();
			onDidSave(p);
		}
	} catch (const std::exception &e) {
		logError("Error handling notification " + method + ": " + e.what());
	}
}

void LanguageServer::sendResponse(const Json &id, const Json &result) {
	Json response = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
	sendMessage(response);
}

void LanguageServer::sendError(const Json &id, int code, const std::string &message) {
	Json response = {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
	sendMessage(response);
}

void LanguageServer::sendNotification(const std::string &method, const Json &params) {
	Json notification = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
	sendMessage(notification);
}

void LanguageServer::log(const std::string &message) { std::cerr << "[LSP] " << message << std::endl; }

void LanguageServer::logError(const std::string &message) { std::cerr << "[LSP ERROR] " << message << std::endl; }

// Default implementations of virtual methods

InitializeResult LanguageServer::onInitialize(const InitializeParams & /*params*/) {
	InitializeResult result;
	return result;
}

void LanguageServer::onDidOpen(const DidOpenTextDocumentParams &params) {
	const auto &doc = params.textDocument;
	documents[doc.uri] = std::make_unique<TextDocument>(doc.uri, doc.text, doc.version);
}

void LanguageServer::onDidChange(const DidChangeTextDocumentParams &params) {
	auto it = documents.find(params.textDocument.uri);
	if (it != documents.end()) {
		for (const auto &change : params.contentChanges) {
			it->second->applyChange(change, params.textDocument.version);
		}
	}
}

void LanguageServer::onDidClose(const DidCloseTextDocumentParams &params) { documents.erase(params.textDocument.uri); }

void LanguageServer::onDidSave(const DidSaveTextDocumentParams & /*params*/) {
	// Default: do nothing
}

std::optional<Location> LanguageServer::onDefinition(const TextDocumentPositionParams & /*params*/) { return std::nullopt; }

SemanticTokens LanguageServer::onSemanticTokensFull(const SemanticTokensParams & /*params*/) { return SemanticTokens{}; }

} // namespace lsp
