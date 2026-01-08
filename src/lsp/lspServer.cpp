#include "lsp/lspServer.hpp"
#include "compiler/importResolver.hpp"
#include "compiler/patternResolver.hpp"
#include "compiler/sectionAnalyzer.hpp"
#include "compiler/typeInference.hpp"
#include "lexer/lexer.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace tbx {

// ============================================================================
// LspServer Core Implementation
// ============================================================================

LspServer::LspServer() {
  // Enable debug by default for now to help diagnose go-to-definition issues
  debugMode = true;
  std::cerr << "[3BX-LSP] LspServer constructor called" << std::endl;
}

LspServer::~LspServer() = default;

void LspServer::run() {
  // Log immediately to stderr so we can confirm the server started
  std::cerr << "[3BX-LSP] Server starting... (debug="
            << (debugMode ? "true" : "false") << ")" << std::endl;
  std::cerr.flush();

  log("3BX Language Server starting...");

  while (!shutdown) {
    try {
      std::string message = readMessage();
      if (message.empty()) {
        break;
      }

      std::string response = processMessage(message);
      if (!response.empty()) {
        writeMessage(response);
      }
    } catch (const std::exception &exception) {
      std::cerr << "[3BX-LSP] Loop Error: " << exception.what() << std::endl;
      log("Error: " + std::string(exception.what()));
    }
  }

  log("3BX Language Server shutting down.");
}

std::string LspServer::readMessage() {
  // Read headers until empty line
  int contentLength = -1;

  while (true) {
    std::string line;
    if (!std::getline(std::cin, line)) {
      return ""; // EOF
    }

    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      break; // End of headers
    }

    // Parse Content-Length header
    if (line.find("Content-Length:") == 0) {
      contentLength = std::stoi(line.substr(15));
    }
  }

  if (contentLength <= 0) {
    log("Invalid Content-Length");
    return "";
  }

  // Read content
  std::string content(contentLength, '\0');
  std::cin.read(&content[0], contentLength);

  if (std::cin.gcount() != contentLength) {
    log("Failed to read full message content");
    return "";
  }

  return content;
}

void LspServer::writeMessage(const std::string &content) {
  std::cout << "Content-Length: " << content.size() << "\r\n\r\n" << content;
  std::cout.flush();
}

std::string LspServer::processMessage(const std::string &message) {
  json request = json::parse(message);

  std::string method = request["method"].get<std::string>();
  json params = request.value("params", json::object());

  if (request.contains("id")) {
    json id = request["id"];
    // This is a request
    json result = handleRequest(method, params, id);

    json response = json::object();
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = result;
    return response.dump();
  } else {
    // This is a notification
    handleNotification(method, params);
    return "";
  }
}

// ============================================================================
// Response Helpers
// ============================================================================

void LspServer::sendResponse(const json &id, const json &result) {
  json response = json::object();
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["result"] = result;
  writeMessage(response.dump());
}

void LspServer::sendError(const json &id, int code,
                          const std::string &message) {
  json error = json::object();
  error["code"] = code;
  error["message"] = message;
  json response = json::object();
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["error"] = error;
  writeMessage(response.dump());
}

void LspServer::sendNotification(const std::string &method,
                                 const json &params) {
  json notification = json::object();
  notification["jsonrpc"] = "2.0";
  notification["method"] = method;
  notification["params"] = params;
  writeMessage(notification.dump());
}

// ============================================================================
// Logging
// ============================================================================

void LspServer::log(const std::string &message) {
  if (debugMode)
    std::cerr << "[3BX-LSP] " << message << std::endl;
  logToFile(message);
}

void LspServer::logToFile(const std::string &message) {
  std::cerr << "[FILE_LOG] " << message << std::endl;
  static std::ofstream logFile(
      "/home/johnheikens/Documents/Github/3BX/lsp_debug.log", std::ios::app);
  if (logFile.is_open()) {
    logFile << "[LOG] " << message << std::endl;
    logFile.flush();
  }
}

// ============================================================================
// URI/Path Utilities
// ============================================================================

std::string LspServer::uriToPath(const std::string &uri) {
  if (uri.find("file://") == 0) {
    std::string path = uri.substr(7);
    std::string decoded;
    for (size_t charIndex = 0; charIndex < path.size(); charIndex++) {
      if (path[charIndex] == '%' && charIndex + 2 < path.size()) {
        int val = std::stoi(path.substr(charIndex + 1, 2), nullptr, 16);
        decoded += (char)val;
        charIndex += 2;
      } else
        decoded += path[charIndex];
    }
    return decoded;
  }
  return uri;
}

std::string LspServer::pathToUri(const std::string &path) {
  return "file://" + path;
}

} // namespace tbx
