#pragma once

#include "compiler/codeLine.hpp"
#include "compiler/diagnostic.hpp"
#include "compiler/patternType.hpp"
#include "compiler/resolvedValue.hpp"
#include "compiler/section.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tbx {

/**
 * SectionAnalyzer - Step 2 of the 3BX compiler pipeline
 *
 * Analyzes merged source code and creates a section tree based on indentation.
 *
 * Key principles:
 * - NO hardcoded keywords
 * - Indentation determines structure
 * - Lines ending with ":" have child sections
 * - Lines starting with "section ", "effect ", or "expression " are pattern
 * definitions
 */
class SectionAnalyzer {
public:
  /**
   * Analyze source code and create section tree
   * @param source The merged source code
   * @param sourceMap Optional map mapping merged line numbers to original file
   * locations (mergedLine -> {filePath, originalLine})
   * @return Root section containing the entire program
   */
  struct SourceLocation {
    std::string filePath;
    int lineNumber;
  };

  std::unique_ptr<Section>
  analyze(const std::string &source,
          const std::map<int, SourceLocation> &sourceMap = {});

  /**
   * Get any errors that occurred during analysis
   */
  const std::vector<Diagnostic> &diagnostics() const { return diagnosticsData; }

private:
  /**
   * Split source into lines and calculate indent levels
   */
  struct SourceLine {
    std::string text;     // Original text with whitespace trimmed
    int indentLevel;      // Number of leading spaces/tabs (normalized)
    int lineNumber;       // 1-based line number (in merged file)
    std::string filePath; // Original file path (if available)
    int originalLine;     // Original line number (if available)
    int startColumn;      // Start column
    int endColumn;        // End column
    bool isEmpty;         // Is this line empty or comment-only?
  };

  std::vector<SourceLine>
  splitLines(const std::string &source,
             const std::map<int, SourceLocation> &sourceMap);

  /**
   * Calculate indent level from leading whitespace
   * Tabs are treated as equivalent to 4 spaces
   */
  int calculateIndent(const std::string &line, int &startCol);

  /**
   * Trim leading and trailing whitespace
   */
  std::string trim(const std::string &str);

  /**
   * Check if a line is a comment (starts with #)
   */
  bool isComment(const std::string &line);

  /**
   * Check if a line is a pattern definition
   * Returns true if line starts with "section ", "effect ", or "expression "
   */
  bool isPatternDefinition(const std::string &line);

  /**
   * Check if a line ends with a colon (indicating child section follows)
   */
  bool endsWithColon(const std::string &line);

  /**
   * Build section tree recursively
   */
  void buildSection(Section &section, const std::vector<SourceLine> &lines,
                    size_t &index, int parentIndent);

  std::vector<Diagnostic> diagnosticsData;
};

} // namespace tbx
