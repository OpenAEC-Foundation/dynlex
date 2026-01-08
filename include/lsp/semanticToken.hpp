#pragma once

#include "lsp/semanticTokenTypes.hpp"

namespace tbx {

struct SemanticToken {
  int start;
  int length;
  SemanticTokenType type;

  bool operator<(const SemanticToken &other) const {
    if (start != other.start)
      return start < other.start;
    return length > other.length; // Longer tokens first for nesting
  }
};

} // namespace tbx
