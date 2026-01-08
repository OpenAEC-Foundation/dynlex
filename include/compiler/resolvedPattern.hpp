#pragma once

#include "compiler/sectionAnalyzer.hpp"
#include <string>
#include <vector>

namespace tbx {

/**
 * Represents a resolved pattern definition
 */
struct ResolvedPattern {
  std::string pattern;      // The pattern string with $ for variable slots
  std::string originalText; // The original text of the pattern
  std::vector<std::string> variables; // Names of the variables in the pattern
  CodeLine *sourceLine = nullptr;     // The line that defines this pattern
  Section *body = nullptr;            // The body of the pattern (child section)
  PatternType type = PatternType::Effect;
  bool isPrivate = false;

  // Check if the pattern is a single word with no variables
  bool isSingleWord() const;

  // Calculate specificity (number of literal words)
  int specificity() const;

  // Print for debugging
  void print(int indent = 0) const;
};

} // namespace tbx
