#pragma once

#include "pattern/pattern.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>

namespace tbx {

// A node in the pattern matching trie
// Each node represents one position in a pattern
struct PatternTreeNode {
    // Children indexed by literal word
    std::unordered_map<std::string, std::unique_ptr<PatternTreeNode>> literalChildren;

    // Child for parameter matches (captures any expression)
    std::unique_ptr<PatternTreeNode> paramChild;

    // If this node is the end of a pattern, store which pattern(s) end here
    // Multiple patterns may end at the same node with different priorities
    std::vector<Pattern*> endingPatterns;

    // Parameter name if this node was reached via a parameter path
    std::string paramName;

    // Optional elements support:
    // When a literal child is optional, the key is stored here
    // This enables matching both with and without the optional word
    std::unordered_set<std::string> optionalLiterals;

    // Track which patterns pass through this node (for optional handling)
    // Maps pattern ID to next required node after optional sequence
    std::unordered_map<std::string, PatternTreeNode*> skipEdges;

    PatternTreeNode() = default;
};

// Pattern tree for efficient pattern lookup
// Organizes patterns into a trie structure for O(n) matching where n is pattern length
class PatternTree {
public:
    PatternTree();

    // Insert a pattern into the tree
    void insert(Pattern* pattern);

    // Remove all patterns from the tree
    void clear();

    // Get all patterns that could potentially match the given first word
    // Returns patterns sorted by specificity (most specific first)
    std::vector<Pattern*> getCandidates(const std::string& firstWord);

    // Get the root node for traversal-based matching
    PatternTreeNode* getRoot() { return root_.get(); }

private:
    std::unique_ptr<PatternTreeNode> root_;

    // All patterns that start with a parameter (need to be checked for all inputs)
    std::vector<Pattern*> paramFirstPatterns_;

    // Index: first literal word -> patterns starting with that word
    std::unordered_map<std::string, std::vector<Pattern*>> wordIndex_;
};

} // namespace tbx
