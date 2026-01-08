#pragma once

#include "lsp/lspRange.hpp"

#include <string>

namespace tbx {

// Represents a diagnostic (error/warning)
struct LspDiagnostic {
  LspRange range;
  int severity{1}; // 1=Error, 2=Warning, 3=Info, 4=Hint
  std::string message;
  std::string source{"3bx"};
};

} // namespace tbx
