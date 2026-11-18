#pragma once
#include "lspProtocol.h"
#include "semanticTokens.h"
#include <string>
#include <string_view>
#include <vector>

struct SyntaxConfig;

namespace lsp {

class TextDocument;

bool isConfigDocumentUri(std::string_view uri);
std::vector<Diagnostic> collectConfigDiagnostics(const TextDocument &document);
std::vector<int> encodeConfigSemanticTokens(const TextDocument &document);
CompletionList collectConfigCompletions(const TextDocument &document, int line, int character, const SyntaxConfig &builtin);

} // namespace lsp
