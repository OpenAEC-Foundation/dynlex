#pragma once
#include "parseContext.h"
#include "semanticTokenBuilder.h"
#include <string>
#include <vector>

namespace lsp {

class TextDocument;

std::vector<std::vector<SemanticToken>>
collectSemanticTokens(ParseContext &context, const std::string &uri, int lineCount, bool suppressOnFileErrors = true);

std::vector<SemanticToken>
collectLiveLineSemanticTokens(const ParseContext *context, const TextDocument &document, const std::string &uri, int lineIndex);

std::vector<int> encodeSemanticTokens(const std::vector<std::vector<SemanticToken>> &tokensByLine);
std::string renderTaggedSemanticTokensFromData(std::string_view text, const std::vector<int> &data);

std::string renderTaggedSemanticTokens(ParseContext &context, const std::string &path, bool suppressOnFileErrors = true);

} // namespace lsp
