#pragma once

#include "compiler/patternMatch.hpp"
#include "compiler/patternTree.hpp"
#include "compiler/resolvedPattern.hpp"
#include "compiler/sectionAnalyzer.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tbx {

PatternType patternTypeFromPrefix(const std::string &prefix);
std::string patternTypeToString(PatternType type);
std::vector<std::string> expandAlternatives(const std::string &patternText);

/**
 * SectionPatternResolver - Step 3 of the 3BX compiler pipeline
 *
 * Matches code lines against pattern definitions.
 */
class SectionPatternResolver {
public:
  SectionPatternResolver();

  /**
   * Resolve all pattern references in the section tree
   * @return true if all patterns were successfully resolved
   */
  bool resolve(Section *root);

  /**
   * Get any diagnostics (errors/warnings) found during resolution
   */
  const std::vector<Diagnostic> &diagnostics() const { return diagnosticsData; }

  /**
   * Get all pattern definitions found in the codebase
   */
  const std::vector<std::unique_ptr<ResolvedPattern>> &
  patternDefinitions() const {
    return patternDefinitionsData;
  }

  /**
   * Get all successful pattern matches
   */
  const std::vector<std::unique_ptr<PatternMatch>> &patternMatches() const {
    return patternMatchesData;
  }

  /**
   * Print the results of pattern resolution for debugging
   */
  void printResults() const;

  /**
   * Tree-based matching (Step 3 optimized)
   */
  void buildPatternTrees();
  PatternMatch *matchWithTree(CodeLine *line);

  /**
   * Get the pattern match for a specific code line
   */
  PatternMatch *getPatternMatch(CodeLine *line) const {
    auto it = lineToMatchData.find(line);
    return (it != lineToMatchData.end()) ? it->second : nullptr;
  }

  /**
   * Get the expression tree for expression pattern matching
   */
  PatternTree &getExpressionTree() { return expressionTree; }

private:
  /**
   * Phase 1: Collect all pattern definitions and code lines
   */
  void collectCodeLines(Section *section, std::vector<CodeLine *> &lines);
  void collectSections(Section *section, std::vector<Section *> &sections);
  std::vector<std::unique_ptr<ResolvedPattern>>
  extractPatternDefinitions(CodeLine *line);
  std::unique_ptr<ResolvedPattern> extractPatternDefinition(CodeLine *line);

  /**
   * Phase 2: Variable identification and pattern string creation
   */
  std::vector<std::string> parsePatternWords(const std::string &text);
  std::vector<std::string>
  identifyVariables(const std::vector<std::string> &words);
  std::vector<std::string>
  identifyVariablesFromBody(const std::vector<std::string> &patternWords,
                            Section *body);
  std::string createPatternString(const std::vector<std::string> &words,
                                  const std::vector<std::string> &variables);

  /**
   * Resolution algorithm phases
   */
  void resolveSingleWordPatterns();
  bool resolvePatternReferences();
  bool resolveSections();
  bool propagateVariablesFromCalls();

  /**
   * Helper to detect if a line is a special directive or intrinsic
   */
  bool isIntrinsicCall(const std::string &text) const;
  bool isPatternBodyDirective(const std::string &text) const;
  bool isSingleWordWithSection(const CodeLine *line) const;
  bool isInsidePatternsSection(CodeLine *line) const;

  /**
   * Tree-based matching helper structures and methods
   */
  struct ParsedLiteral {
    enum class Type { String, Number, Intrinsic, Group };
    Type type;
    std::string text;
    size_t startPos;
    size_t endPos;
    std::vector<std::string> intrinsicArgs;
  };

  std::vector<ParsedLiteral> detectLiterals(const std::string &input);
  size_t parseIntrinsicCall(const std::string &input, size_t startPos,
                            std::string &name, std::vector<std::string> &args);
  std::unique_ptr<PatternMatch>
  treeMatchToPatternMatch(const TreePatternMatch &treeMatch,
                          const ResolvedPattern *pattern);

  std::vector<std::unique_ptr<ResolvedPattern>> patternDefinitionsData;
  std::vector<std::unique_ptr<PatternMatch>> patternMatchesData;
  std::vector<Diagnostic> diagnosticsData;

  // Working state during resolution
  std::vector<CodeLine *> allLinesData;
  std::vector<Section *> allSectionsData;
  std::map<CodeLine *, ResolvedPattern *> lineToPatternData;
  std::map<CodeLine *, PatternMatch *> lineToMatchData;

  // Pattern Trees
  PatternTree effectTree;
  PatternTree sectionTree;
  PatternTree expressionTree;
};

} // namespace tbx
