#pragma once

#include "compiler/matchedValue.hpp"

#include <string>
#include <vector>

namespace tbx {

// Forward declaration
struct ResolvedPattern;

/**
 * Result of matching an expression (for expression substitution)
 */
struct ExpressionMatch {
  ResolvedPattern *pattern;            // The matched expression pattern
  std::vector<MatchedValue> arguments; // Arguments in order
  std::string matchedText;             // The original text that was matched
};

} // namespace tbx
