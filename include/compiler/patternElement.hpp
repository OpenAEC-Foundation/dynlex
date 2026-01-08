#pragma once

#include "compiler/patternElementType.hpp"

#include <string>

namespace tbx {

/**
 * A single element in a parsed pattern
 */
struct PatternElement {
  PatternElementType type;
  std::string text;        // For Literal: the text
  std::string captureName; // For captures: the variable name (e.g., "member" in
                           // {word:member})
};

} // namespace tbx
