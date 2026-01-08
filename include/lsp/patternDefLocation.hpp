#pragma once

#include "lsp/lspLocation.hpp"
#include "lsp/lspRange.hpp"

#include <string>
#include <vector>

namespace tbx {

// Represents a pattern definition with its location
struct PatternDefLocation {
  std::string syntax; // The pattern syntax string (e.g., "set var to val")
  std::vector<std::string> words; // Words in the pattern (for matching)
  LspLocation location;           // Where the pattern is defined
  LspRange usageRange;    // Where the pattern is USED (added for resolved
                          // Go-to-Definition)
  bool isPrivate = false; // Whether the pattern is private
};

} // namespace tbx
