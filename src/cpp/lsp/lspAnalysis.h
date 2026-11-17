#pragma once
#include "lspProtocol.h"
#include "parseContext.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lsp {

using ParseContexts = std::vector<std::shared_ptr<ParseContext>>;
using ParseContextMap = std::unordered_map<std::string, ParseContexts>;
using ImportGraph = std::unordered_map<std::string, std::unordered_set<std::string>>;

class TextDocument;
struct LspAnalysisResult {
	std::shared_ptr<ParseContext> context;
	Json sources, configurations;
	bool profileCurrent = true;
	bool reused = false;
};

LspAnalysisResult analyzeLspDocument(
	const std::string &uri, const ParseContext::Options &options,
	std::unordered_map<std::string, std::unique_ptr<TextDocument>> &documents, const LspAnalysisResult *previous
);
void addAnalysisTrackingForMain(ImportGraph &graph, const std::string &mainUri, const LspAnalysisResult &result);

std::vector<ParseContext::Options> parseAnalysisProfiles(const std::optional<Json> &initializationOptions);
void eraseImportTrackingForMain(ImportGraph &graph, const std::string &mainUri);
void addImportTrackingForMain(ImportGraph &graph, const std::string &mainUri, const ParseContext &context);
std::vector<ParseContext *>
findContextsForUri(const std::string &uri, const ParseContextMap &contexts, const ImportGraph &graph);

} // namespace lsp
