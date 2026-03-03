#include "languageServer.h"
#include "lspProtocol.h"
#include "tcpTransport.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace lsp {

LanguageServer::LanguageServer(int port) : port(port) {}

LanguageServer::LanguageServer(std::unique_ptr<Transport> transport) : transport(std::move(transport)) {}

LanguageServer::~LanguageServer() { shutdown(); }

bool LanguageServer::enableTrace(const std::string &path) {
	if (path.empty()) {
		traceFile.reset();
		traceStream = &std::cerr;
		return true;
	}

	traceFile = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
	if (!traceFile->is_open()) {
		traceFile.reset();
		traceStream = nullptr;
		return false;
	}

	traceStream = traceFile.get();
	return true;
}

void LanguageServer::run() {
	running = true;

	if (transport) {
		// Direct transport mode (e.g., stdio)
		log("Language server running on provided transport");
		handleConnection();
	} else {
		// TCP server mode
		TcpServer server(port);
		if (!server.setup()) {
			logError("Failed to setup TCP server");
			return;
		}

		log("Language server listening on port " + std::to_string(port));

		while (running) {
			transport = server.acceptConnection();
			if (transport) {
				log("Client connected");
				handleConnection();
				transport.reset();
				log("Client disconnected");
			}
		}
	}

	running = false;
}

void LanguageServer::shutdown() {
	running = false;
	if (transport) {
		transport->close();
	}
}

void LanguageServer::handleConnection() {
	while (running && transport && transport->isConnected()) {
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

std::string LanguageServer::readMessage() {
	if (!transport || !transport->isConnected()) {
		return "";
	}

	// Read headers until empty line
	std::string headers;
	char c;
	int consecutiveNewlines = 0;

	while (running && transport->isConnected()) {
		ssize_t n = transport->read(&c, 1);
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
	while (totalRead < contentLength && running && transport->isConnected()) {
		ssize_t n = transport->read(&body[totalRead], contentLength - totalRead);
		if (n <= 0) {
			return "";
		}
		totalRead += n;
	}

	traceMessage("recv", body);
	return body;
}

void LanguageServer::sendMessage(const Json &message) {
	if (!transport || !transport->isConnected()) {
		return;
	}

	std::string body = message.dump();
	traceMessage("send", body);
	std::string header = "Content-Length: " + std::to_string(body.length()) + "\r\n\r\n";
	std::string fullMessage = header + body;

	size_t totalSent = 0;
	while (totalSent < fullMessage.size() && transport->isConnected()) {
		ssize_t n = transport->write(fullMessage.c_str() + totalSent, fullMessage.size() - totalSent);
		if (n <= 0) {
			logError("Failed to send message: " + std::string(strerror(errno)));
			return;
		}
		totalSent += n;
	}
}

void LanguageServer::handleMessage(const Json &message) {
	if (message.contains("id") && message.contains("method")) {
		handleRequest(message);
	} else if (message.contains("id")) {
		handleResponse(message);
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
		} else if (method == "textDocument/completion") {
			TextDocumentPositionParams p = params.get<TextDocumentPositionParams>();
			sendResponse(id, onCompletion(p));
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
		} else if (method == "textDocument/documentSymbol") {
			DocumentSymbolParams p = params.get<DocumentSymbolParams>();
			auto result = onDocumentSymbol(p);
			sendResponse(id, result);
		} else if (method == "textDocument/codeAction") {
			CodeActionParams p = params.get<CodeActionParams>();
			auto result = onCodeAction(p);
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
		} else if (method == "dynlex/activeCursorChanged") {
			ActiveCursorParams p = params.get<ActiveCursorParams>();
			onActiveCursorChanged(p);
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

void LanguageServer::sendRequest(const std::string &method, const Json &params) {
	Json request = {{"jsonrpc", "2.0"}, {"id", nextRequestId++}, {"method", method}, {"params", params}};
	sendMessage(request);
}

void LanguageServer::handleResponse(const Json &message) { (void)message; }

void LanguageServer::log(const std::string &message) { std::cerr << "[LSP] " << message << std::endl; }

void LanguageServer::logError(const std::string &message) { std::cerr << "[LSP ERROR] " << message << std::endl; }

void LanguageServer::traceMessage(std::string_view direction, const std::string &body) {
	if (!traceStream)
		return;

	using namespace std::chrono;
	auto now = system_clock::now();
	auto nowTime = system_clock::to_time_t(now);
	auto millis = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

	std::tm tm{};
	localtime_r(&nowTime, &tm);

	(*traceStream) << "[LSP TRACE " << direction << " " << std::put_time(&tm, "%F %T") << '.' << std::setw(3)
				   << std::setfill('0') << millis.count() << "] " << body << '\n';
	traceStream->flush();
}

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

CompletionList LanguageServer::onCompletion(const TextDocumentPositionParams & /*params*/) { return {}; }

SemanticTokens LanguageServer::onSemanticTokensFull(const SemanticTokensParams & /*params*/) { return SemanticTokens{}; }

std::vector<DocumentSymbol> LanguageServer::onDocumentSymbol(const DocumentSymbolParams & /*params*/) { return {}; }

std::vector<CodeAction> LanguageServer::onCodeAction(const CodeActionParams & /*params*/) { return {}; }

void LanguageServer::onActiveCursorChanged(const ActiveCursorParams & /*params*/) {}

} // namespace lsp
