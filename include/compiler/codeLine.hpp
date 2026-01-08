#pragma once

#include "compiler/patternType.hpp"

#include <memory>
#include <string>

namespace tbx {

// Forward declaration
struct Section;

/**
 * CodeLine - Represents a single line of code within a section
 *
 * Each code line can be:
 * - A pattern definition (starts with "section ", "effect ", or "expression ")
 * - A pattern reference (any other code)
 *
 * If the line ends with ":", it has a child section.
 */
struct CodeLine {
  std::string text; // The raw line text (trimmed)
  bool isPatternDefinition =
      false;              // Starts with "section ", "effect ", or "expression "
  bool isPrivate = false; // Only accessible in the defining file
  PatternType type =
      PatternType::Effect; // The type of pattern (if isPatternDefinition)
  bool isResolved = false; // Has this line been resolved?
  std::unique_ptr<Section> childSection; // If line ends with ":"
  int lineNumber = 0;                    // Original line number in source
  std::string filePath;                  // Original source file path
  int startColumn = 0;                   // 0-based start column of actual code
  int endColumn = 0;                     // 0-based end column of actual code

  // Default constructor
  CodeLine() = default;

  // Constructor with text
  explicit CodeLine(const std::string &lineText, int lineNum = 0,
                    std::string file = "", int startCol = 0, int endCol = 0);

  // Move constructor and assignment
  CodeLine(CodeLine &&other) noexcept = default;
  CodeLine &operator=(CodeLine &&other) noexcept = default;

  // Clone for patterns: extraction
  std::unique_ptr<CodeLine> clone() const;

  // No copy (due to unique_ptr)
  CodeLine(const CodeLine &) = delete;
  CodeLine &operator=(const CodeLine &) = delete;

  /**
   * Check if this line has a child section (ends with ":")
   */
  bool hasChildSection() const { return childSection != nullptr; }

  /**
   * Get the pattern text (without the trailing ":" if present)
   */
  std::string getPatternText() const;
};

} // namespace tbx
