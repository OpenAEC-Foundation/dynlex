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

std::vector<ParseContext::Options> parseAnalysisProfiles(const std::optional<Json> &initializationOptions);
void eraseImportTrackingForMain(ImportGraph &graph, const std::string &mainUri);
void addImportTrackingForMain(ImportGraph &graph, const std::string &mainUri, const ParseContext &context);
std::vector<ParseContext *>
findContextsForUri(const std::string &uri, const ParseContextMap &contexts, const ImportGraph &graph);

} // namespace lsp
