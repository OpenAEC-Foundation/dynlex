#include "compiler/importResolver.hpp"
#include "compiler/patternResolver.hpp"
#include "compiler/sectionAnalyzer.hpp"
#include "compiler/typeInference.hpp"
#include "lexer/lexer.hpp"
#include "lsp/lspServer.hpp"
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
// Request Handlers
// ============================================================================

json LspServer::handleRequest(const std::string &method, const json &params,
                              const json &id) {
  (void)id;
  if (method == "initialize") {
    return handleInitialize(params);
  }
  if (method == "shutdown") {
    handleShutdown();
    return json::object();
  }
  if (method == "textDocument/completion") {
    return handleCompletion(params);
  }
  if (method == "textDocument/hover") {
    return handleHover(params);
  }
  if (method == "textDocument/definition") {
    return handleDefinition(params);
  }
  if (method == "textDocument/semanticTokens/full") {
    return handleSemanticTokensFull(params);
  }

  // Unknown method
  log("Unknown request method: " + method);
  return json::object();
}

void LspServer::handleNotification(const std::string &method,
                                   const json &params) {
  if (method == "initialized") {
    handleInitialized(params);
  } else if (method == "exit") {
    handleExit();
  } else if (method == "textDocument/didOpen") {
    handleDidOpen(params);
  } else if (method == "textDocument/didChange") {
    handleDidChange(params);
  } else if (method == "textDocument/didClose") {
    handleDidClose(params);
  } else {
    log("Unknown notification method: " + method);
  }
}

json LspServer::handleInitialize(const json &params) {
  (void)params;
  initialized = true;
  debugMode = true; // FORCE DEBUG ON FOR NOW

  // Build capabilities
  json capabilities = json::object();

  // Text document sync
  json textDocSync = json::object();
  textDocSync["openClose"] = true;
  textDocSync["change"] = 1; // 1 = Full sync
  capabilities["textDocumentSync"] = textDocSync;

  // Completion support
  json completionProvider = json::object();
  completionProvider["resolveProvider"] = false;
  json triggerChars = json::array();
  triggerChars.push_back(" ");
  triggerChars.push_back("@");
  completionProvider["triggerCharacters"] = triggerChars;
  capabilities["completionProvider"] = completionProvider;

  // Hover support
  capabilities["hoverProvider"] = true;

  // Definition support (go-to-definition)
  capabilities["definitionProvider"] = true;

  // Semantic tokens support
  json semanticTokensProvider = json::object();
  json legend = json::object();
  legend["tokenTypes"] = getSemanticTokenTypes();
  legend["tokenModifiers"] = json::array();
  semanticTokensProvider["legend"] = legend;
  semanticTokensProvider["full"] = true;
  capabilities["semanticTokensProvider"] = semanticTokensProvider;

  // Build response
  json result = json::object();
  result["capabilities"] = capabilities;

  json serverInfo = json::object();
  serverInfo["name"] = "3BX Language Server";
  serverInfo["version"] = "0.1.0";
  result["serverInfo"] = serverInfo;

  return result;
}

void LspServer::handleInitialized(const json &params) {
  (void)params;
  log("Client initialized");
}

void LspServer::handleShutdown() {
  shutdown = true;
  log("Shutdown requested");
}

void LspServer::handleExit() { std::exit(shutdown ? 0 : 1); }

void LspServer::handleDidOpen(const json &params) {
  const json &textDoc = params["textDocument"];
  std::string uri = textDoc["uri"].get<std::string>();
  std::string text = textDoc["text"].get<std::string>();
  int version = textDoc["version"].get<int>();

  TextDocument doc;
  doc.uri = uri;
  doc.content = text;
  doc.version = version;
  documents[uri] = doc;

  log("Document opened: " + uri);
  extractPatternDefinitions(uri, text);

  // Also process imports to get patterns from imported files
  processImports(uri, text);

  // Trigger full analysis to update shared state for language features
  getDiagnostics(text, uriToPath(uri));

  publishDiagnostics(uri, text);
}

void LspServer::handleDidChange(const json &params) {
  const json &textDoc = params["textDocument"];
  std::string uri = textDoc["uri"].get<std::string>();
  int version = textDoc["version"].get<int>();

  // For full sync, take the complete new content
  const json &changes = params["contentChanges"];
  if (changes.is_array() && !changes.empty()) {
    std::string text = changes[0]["text"].get<std::string>();

    documents[uri].content = text;
    documents[uri].version = version;

    log("Document changed: " + uri);
    extractPatternDefinitions(uri, text);

    // Process imports as well
    processImports(uri, text);

    publishDiagnostics(uri, text);

    // Ensure go-to-definition is updated for the new version
    getDiagnostics(text, uriToPath(uri));
  }
}

void LspServer::handleDidClose(const json &params) {
  const json &textDoc = params["textDocument"];
  std::string uri = textDoc["uri"].get<std::string>();

  documents.erase(uri);
  patternDefinitions.erase(uri);
  log("Document closed: " + uri);

  // Clear diagnostics
  json diagParams = json::object();
  diagParams["uri"] = uri;
  diagParams["diagnostics"] = json::array();
  sendNotification("textDocument/publishDiagnostics", diagParams);
}

// ============================================================================
// Pattern Extraction
// ============================================================================

void LspServer::extractPatternDefinitions(const std::string &uri,
                                          const std::string &content) {
  patternDefinitions[uri].clear();

  log("extractPatternDefinitions for " + uri);

  std::vector<std::string> lines;
  std::istringstream stream(content);
  std::string lineStr;
  while (std::getline(stream, lineStr))
    lines.push_back(lineStr);

  for (size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
    const std::string &line = lines[lineIndex];

    std::string trimmedLine = line;
    size_t firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos)
      trimmedLine = line.substr(firstNonSpace);
    else
      continue;

    bool isPatternDef = false;
    bool isPrivate = false;
    std::string syntaxPart;

    static const std::vector<std::string> patternKeywords = {
        "effect ",  "expression ", "condition ",
        "section ", "pattern:",    "private "};

    for (const auto &kw : patternKeywords) {
      if (trimmedLine.find(kw) == 0) {
        isPatternDef = true;
        syntaxPart = trimmedLine.substr(kw.size());

        if (kw == "private ") {
          isPrivate = true;
          static const std::vector<std::string> subKeywords = {
              "effect ", "expression ", "condition ", "section "};
          bool foundSub = false;
          for (const auto &subKw : subKeywords) {
            if (syntaxPart.find(subKw) == 0) {
              syntaxPart = syntaxPart.substr(subKw.size());
              foundSub = true;
              break;
            }
          }
          if (!foundSub) {
            isPatternDef = false;
            break;
          }
        }

        if (!syntaxPart.empty() && syntaxPart.back() == ':')
          syntaxPart.pop_back();
        break;
      }
    }

    if (isPatternDef && !syntaxPart.empty()) {
      while (!syntaxPart.empty() && std::isspace(syntaxPart.back()))
        syntaxPart.pop_back();
      while (!syntaxPart.empty() && std::isspace(syntaxPart.front()))
        syntaxPart = syntaxPart.substr(1);

      if (syntaxPart.empty())
        continue;

      PatternDefLocation patDef;
      patDef.syntax = syntaxPart;
      patDef.isPrivate = isPrivate;
      patDef.location.uri = uri;
      patDef.location.range.start.line = (int)lineIndex;
      patDef.location.range.start.character = (int)firstNonSpace;
      patDef.location.range.end.line = (int)lineIndex;
      patDef.location.range.end.character = (int)line.size();

      std::string word;
      std::vector<std::string> allWords;
      for (char character : syntaxPart) {
        if (std::isalnum(character) || character == '_')
          word += character;
        else if (!word.empty()) {
          allWords.push_back(word);
          word.clear();
        }
      }
      if (!word.empty())
        allWords.push_back(word);
      for (const auto &w : allWords)
        patDef.words.push_back(w);
      patternDefinitions[uri].push_back(patDef);
    }
  }
}

void LspServer::processImports(const std::string &uri,
                               const std::string &content) {
  std::string filePath = uriToPath(uri);
  std::string sourceDir;
  size_t lastSlash = filePath.rfind('/');
  if (lastSlash != std::string::npos)
    sourceDir = filePath.substr(0, lastSlash);
  else
    sourceDir = ".";

  std::vector<std::string> lines;
  std::istringstream stream(content);
  std::string lineStr;
  while (std::getline(stream, lineStr))
    lines.push_back(lineStr);

  std::set<std::string> processedImports;
  for (const auto &line : lines) {
    std::string trimmedLine = line;
    size_t firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos)
      trimmedLine = line.substr(firstNonSpace);
    else
      continue;

    if (trimmedLine.find("import ") == 0) {
      std::string importPath = trimmedLine.substr(7);
      while (!importPath.empty() && std::isspace(importPath.back()))
        importPath.pop_back();
      if (importPath.empty())
        continue;

      std::string resolvedPath = resolveImportPath(importPath, sourceDir);
      if (resolvedPath.empty())
        continue;
      if (processedImports.count(resolvedPath))
        continue;
      processedImports.insert(resolvedPath);

      std::ifstream file(resolvedPath);
      if (!file)
        continue;
      std::stringstream buffer;
      buffer << file.rdbuf();
      std::string importedContent = buffer.str();
      std::string importedUri = pathToUri(resolvedPath);
      extractPatternDefinitions(importedUri, importedContent);
    }
  }
}

std::string LspServer::resolveImportPath(const std::string &importPath,
                                         const std::string &sourceDir) {
  std::string fullPath = sourceDir + "/" + importPath;
  if (std::ifstream(fullPath))
    return fullPath;
  fullPath = sourceDir + "/lib/" + importPath;
  if (std::ifstream(fullPath))
    return fullPath;
  std::string searchDir = sourceDir;
  for (int level = 0; level < 5; level++) {
    fullPath = searchDir + "/lib/" + importPath;
    if (std::ifstream(fullPath))
      return fullPath;
    size_t lastSlash = searchDir.rfind('/');
    if (lastSlash == std::string::npos || lastSlash == 0)
      break;
    searchDir = searchDir.substr(0, lastSlash);
  }
  return "";
}

} // namespace tbx
