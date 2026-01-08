#pragma once

#include "lsp/lspPosition.hpp"

namespace tbx {

// Represents a range in a document
struct LspRange {
  LspPosition start;
  LspPosition end;
};

} // namespace tbx
