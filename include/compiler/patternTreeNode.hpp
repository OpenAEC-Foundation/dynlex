#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbx {

// Forward declaration
struct ResolvedPattern;

/**
 * PatternTreeNode - A node in the pattern matching trie
 *
 * Uses unordered_map for O(1) child lookup by literal string.
 * Separate pointers for different capture types.
 */
struct PatternTreeNode {
  // Child nodes keyed by literal strings (merged sequences)
  std::unordered_map<std::string, std::shared_ptr<PatternTreeNode>> children;

  // Child node for expression variable slots ($) - eager evaluation
  std::shared_ptr<PatternTreeNode> expressionChild;

  // Child node for {expression:name} - lazy capture (greedy, caller's scope)
  std::shared_ptr<PatternTreeNode> expressionCaptureChild;

  // Child node for {word:name} - single identifier capture (non-greedy)
  std::shared_ptr<PatternTreeNode> wordCaptureChild;

  // Patterns that end at this node
  std::vector<ResolvedPattern *> patternsEndedHere;

  PatternTreeNode() = default;
};

} // namespace tbx
