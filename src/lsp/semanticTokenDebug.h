#pragma once
#include "parseContext.h"
#include "semanticTokenBuilder.h"
#include <string>
#include <vector>

namespace lsp {

std::vector<std::vector<SemanticToken>>
collectSemanticTokens(ParseContext &context, const std::string &uri, int lineCount, bool suppressOnFileErrors = true);

std::vector<int> encodeSemanticTokens(const std::vector<std::vector<SemanticToken>> &tokensByLine);

std::string renderTaggedSemanticTokens(ParseContext &context, const std::string &path, bool suppressOnFileErrors = true);

} // namespace lsp
