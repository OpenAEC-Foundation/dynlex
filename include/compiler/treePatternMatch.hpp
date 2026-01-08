#pragma once

#include "compiler/matchedValue.hpp"

#include <cstddef>
#include <vector>

namespace tbx {

// Forward declaration
struct ResolvedPattern;

/**
 * Result of a successful pattern match
 */
struct TreePatternMatch {
  ResolvedPattern *pattern;            // The matched pattern
  std::vector<MatchedValue> arguments; // Arguments in variable order
  size_t consumedLength;               // How many characters were consumed
};

} // namespace tbx
