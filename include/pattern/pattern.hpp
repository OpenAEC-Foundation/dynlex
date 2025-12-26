#pragma once

#include "ast/ast.hpp"
#include <string>
#include <vector>
#include <memory>

namespace tbx {

// Compiled pattern for efficient matching
struct Pattern {
    std::string id;                          // Unique identifier
    std::vector<PatternElement> elements;    // The syntax template
    PatternDef* definition = nullptr;        // Full AST definition

    // Precomputed for matching
    size_t literal_count = 0;                // Number of literal words
    std::string first_word;                  // First word for indexing (empty if starts with param)

    // Compile a pattern definition into this structure
    void compile();

    // Check if this pattern could potentially match tokens starting at position
    bool could_match(const std::vector<Token>& tokens, size_t pos) const;

    // Get specificity score (higher = more specific match)
    int specificity() const;
};

using PatternPtr = std::unique_ptr<Pattern>;

} // namespace tbx
