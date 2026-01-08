#pragma once

#include "lsp/lspRange.hpp"

#include <string>

namespace tbx {

// Represents a location in source code for go-to-definition
struct LspLocation {
  std::string uri;
  LspRange range;
};

} // namespace tbx
