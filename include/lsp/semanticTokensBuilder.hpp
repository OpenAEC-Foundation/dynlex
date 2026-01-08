#pragma once

#include "lsp/semanticToken.hpp"
#include "lsp/semanticTokenTypes.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace tbx {

class SemanticTokensBuilder {
public:
  void addToken(int start, int length, SemanticTokenType type) {
    if (length <= 0)
      return;
    tokens.push_back({start, length, type});
  }

  // Clean up overlapping tokens, keeping highest priority
  void resolve() {
    if (tokens.empty())
      return;

    std::sort(tokens.begin(), tokens.end());

    std::vector<SemanticToken> resolved;
    int lastEnd = -1;

    for (const auto &token : tokens) {
      if (token.start >= lastEnd) {
        resolved.push_back(token);
        lastEnd = token.start + token.length;
      } else {
        // Potential overlapping token, for now just skip it
        // In a real implementation we'd handle nesting
      }
    }
    tokens = std::move(resolved);
  }

  static std::string tokenTypeToString(SemanticTokenType type) {
    switch (type) {
    case SemanticTokenType::Comment:
      return "comment";
    case SemanticTokenType::String:
      return "string";
    case SemanticTokenType::Number:
      return "number";
    case SemanticTokenType::Variable:
      return "variable";
    case SemanticTokenType::Function:
      return "function";
    case SemanticTokenType::Keyword:
      return "keyword";
    case SemanticTokenType::Operator:
      return "operator";
    case SemanticTokenType::Pattern:
      return "pattern";
    case SemanticTokenType::Effect:
      return "effect";
    case SemanticTokenType::Expression:
      return "expression";
    case SemanticTokenType::Section:
      return "section";
    default:
      return "other";
    }
  }

  void printTokens(std::ostream &os, const std::string &lineText,
                   const std::string &prefix = "") {
    auto sortedTokens = getTokens();
    os << prefix << "[";
    for (size_t tokenIndex = 0; tokenIndex < sortedTokens.size();
         ++tokenIndex) {
      const auto &token = sortedTokens[tokenIndex];
      os << "\"" << lineText.substr(token.start, token.length) << "\" ("
         << tokenTypeToString(token.type) << ")";
      if (tokenIndex < sortedTokens.size() - 1)
        os << ", ";
    }
    os << "]";
  }

  const std::vector<SemanticToken> &getTokens() {
    // Final sort before returning
    std::sort(
        tokens.begin(), tokens.end(),
        [](const SemanticToken &leftToken, const SemanticToken &rightToken) {
          if (leftToken.start != rightToken.start)
            return leftToken.start < rightToken.start;
          return leftToken.length > rightToken.length;
        });

    // Remove exact duplicates
    tokens.erase(std::unique(tokens.begin(), tokens.end(),
                             [](const SemanticToken &leftToken,
                                const SemanticToken &rightToken) {
                               return leftToken.start == rightToken.start &&
                                      leftToken.length == rightToken.length &&
                                      leftToken.type == rightToken.type;
                             }),
                 tokens.end());

    return tokens;
  }

private:
  std::vector<SemanticToken> tokens;
};

} // namespace tbx
