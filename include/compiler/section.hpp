#pragma once

#include "compiler/codeLine.hpp"
#include "compiler/resolvedValue.hpp"

#include <map>
#include <string>
#include <vector>

namespace tbx {

/**
 * Section - A block of code at a particular indentation level
 *
 * Sections contain code lines, and each code line can have a child section.
 * This creates a tree structure based on indentation.
 */
struct Section {
  std::vector<CodeLine> lines;
  bool isResolved = false;
  std::map<std::string, ResolvedValue> resolvedVariables;
  Section *parent = nullptr;
  int indentLevel = 0;

  // Default constructor
  Section() = default;

  // Move constructor and assignment
  Section(Section &&other) noexcept = default;
  Section &operator=(Section &&other) noexcept = default;

  // No copy (due to unique_ptr in CodeLine)
  Section(const Section &) = delete;
  Section &operator=(const Section &) = delete;

  /**
   * Add a code line to this section
   */
  void addLine(CodeLine line);

  /**
   * Check if all lines in this section are resolved
   */
  bool allLinesResolved() const;

  /**
   * Print the section tree for debugging
   */
  void print(int depth = 0) const;
};

} // namespace tbx
