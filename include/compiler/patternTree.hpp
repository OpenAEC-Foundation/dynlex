#pragma once

#include "compiler/expressionMatch.hpp"
#include "compiler/matchedValue.hpp"
#include "compiler/patternElement.hpp"
#include "compiler/patternElementType.hpp"
#include "compiler/patternTreeNode.hpp"
#include "compiler/treePatternMatch.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tbx {

// Forward declarations
struct ResolvedPattern;

/**
 * PatternTree - Trie-like structure for efficient pattern matching
 *
 * Supports:
 * - Merged literal sequences for compact storage
 * - Expression substitution via recursive matching
 * - Alternatives [a|b] with branch-and-merge (handled during pattern insertion)
 * - Lazy captures {word} for deferred evaluation
 */
class PatternTree {
public:
  PatternTree();

  /**
   * Add a pattern to the tree
   * @param pattern The resolved pattern to add
   */
  void addPattern(ResolvedPattern *pattern);

  /**
   * Match input text against all patterns in the tree
   * @param input The input text to match
   * @param startPos Starting position in the input
   * @return Best match (most specific), or nullopt if no match
   */
  std::optional<TreePatternMatch> match(const std::string &input,
                                        size_t startPos = 0);

  /**
   * Match only expression patterns (for expression substitution)
   * @param input The input text to match
   * @param startPos Starting position in the input
   * @return Best expression match, or nullopt if no match
   */
  std::optional<TreePatternMatch> matchExpression(const std::string &input,
                                                  size_t startPos = 0);

  /**
   * Clear all patterns from the tree
   */
  void clear();

  /**
   * Get the root node (for debugging)
   */
  const PatternTreeNode &root() const { return rootNode; }

private:
  /**
   * Parse a pattern into elements (merged literals + variable slots)
   */
  std::vector<PatternElement>
  parsePatternElements(const ResolvedPattern *pattern);

  /**
   * Parse a pattern string directly into elements
   */
  std::vector<PatternElement>
  parsePatternElementsFromString(const std::string &text);

  /**
   * Parse alternatives in a pattern text
   * Returns expanded patterns for [a|b] syntax
   */
  std::vector<std::string> expandAlternatives(const std::string &patternText);

  /**
   * Add a single expanded pattern path to the tree
   */
  void addPatternPath(const std::vector<PatternElement> &elements,
                      ResolvedPattern *pattern);

  /**
   * Internal matching with backtracking
   * @param node Current node in the tree
   * @param input Input text
   * @param pos Current position in input
   * @param arguments Collected arguments so far
   * @param matches Output: all successful matches found
   */
  void matchRecursive(PatternTreeNode *node, const std::string &input,
                      size_t pos, std::vector<MatchedValue> &arguments,
                      std::vector<TreePatternMatch> &matches);

  /**
   * Try to match an expression at the current position
   * Used when encountering an expression_child node
   */
  std::optional<ExpressionMatch> tryMatchExpressionAt(const std::string &input,
                                                      size_t pos);

  /**
   * Find possible expression end positions
   * Returns positions in reverse order (longest first) for greedy matching
   */
  std::vector<size_t> findExpressionBoundaries(const std::string &input,
                                               size_t start);

  /**
   * Check if a character is an expression boundary
   */
  bool isExpressionBoundary(char character) const;

  /**
   * Try to parse a literal value (number or string)
   */
  std::optional<MatchedValue> tryParseLiteral(const std::string &text);

  PatternTreeNode rootNode;

  // Separate storage for expression patterns (for recursive matching)
  std::vector<ResolvedPattern *> expressionPatterns;
};

} // namespace tbx
