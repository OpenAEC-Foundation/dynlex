#pragma once

#include "compiler/diagnostic.hpp"
#include "lsp/lspDiagnostic.hpp"
#include "lsp/lspLocation.hpp"
#include "lsp/lspPosition.hpp"
#include "lsp/lspRange.hpp"
#include "lsp/patternDefLocation.hpp"
#include "lsp/semanticTokenTypes.hpp"
#include "lsp/semanticTokensBuilder.hpp"
#include "lsp/textDocument.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbx {

using json = nlohmann::json;

// Forward declarations
class Lexer;

// LSP Server implementation
class LspServer {
public:
  LspServer();
  ~LspServer();

  // Run the server (main loop reading from stdin, writing to stdout)
  void run();

  // Process a single message (for testing)
  std::string processMessage(const std::string &message);

  // Enable debug mode (writes logs to stderr)
  void setDebug(bool debug) { debugMode = debug; }

private:
  bool debugMode{};
  bool initialized{};
  bool shutdown{};
  std::unordered_map<std::string, TextDocument> documents;

  // Pattern definitions indexed by document URI
  std::unordered_map<std::string, std::vector<PatternDefLocation>>
      patternDefinitions;

  // Message handling
  json handleRequest(const std::string &method, const json &params,
                     const json &id);
  void handleNotification(const std::string &method, const json &params);

  // LSP method handlers
  json handleInitialize(const json &params);
  void handleInitialized(const json &params);
  void handleShutdown();
  void handleExit();

  // Document sync
  void handleDidOpen(const json &params);
  void handleDidChange(const json &params);
  void handleDidClose(const json &params);

  // Language features
  json handleCompletion(const json &params);
  json handleHover(const json &params);
  json handleDefinition(const json &params);

  // Diagnostics
  void publishDiagnostics(const std::string &uri, const std::string &content);
  std::vector<LspDiagnostic> getDiagnostics(const std::string &content,
                                            const std::string &filename);

  // Semantic tokens
  json handleSemanticTokensFull(const json &params);
  std::vector<int32_t> computeSemanticTokens(const std::string &uri);

  // IO helpers
  std::string readMessage();
  void writeMessage(const std::string &content);
  void sendResponse(const json &id, const json &result);
  void sendError(const json &id, int code, const std::string &message);
  void sendNotification(const std::string &method, const json &params);

  // Logging
  void log(const std::string &message);
  void logToFile(const std::string &message);

  // Utility
  std::string uriToPath(const std::string &uri);
  std::string pathToUri(const std::string &path);

  // Pattern definition tracking
  void extractPatternDefinitions(const std::string &uri,
                                 const std::string &content);
  void processImports(const std::string &uri, const std::string &content);
  std::string resolveImportPath(const std::string &importPath,
                                const std::string &sourceDir);
  PatternDefLocation *findPatternAtPosition(const std::string &uri, int line,
                                            int character);
};

} // namespace tbx
