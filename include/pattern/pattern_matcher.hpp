#pragma once

#include "pattern/pattern.hpp"
#include "pattern/pattern_registry.hpp"
#include "lexer/token.hpp"
#include "ast/ast.hpp"
#include <vector>
#include <optional>
#include <unordered_map>

namespace tbx {

// Result of a successful pattern match
struct MatchResult {
    Pattern* pattern;                                    // Matched pattern
    std::unordered_map<std::string, ExprPtr> bindings;   // Parameter bindings
    size_t tokensConsumed;                               // How many tokens were used
    int specificity;                                     // Match quality score

    // Track if any variables were created by this match
    std::vector<std::string> createdVariables;
};

class PatternMatcher {
public:
    explicit PatternMatcher(PatternRegistry& registry);

    // Try to match a statement pattern at current position
    std::optional<MatchResult> matchStatement(
        const std::vector<Token>& tokens,
        size_t pos
    );

    // Try to match an expression pattern at current position
    std::optional<MatchResult> matchExpression(
        const std::vector<Token>& tokens,
        size_t pos,
        int minPrecedence = 0
    );

    // Match a single pattern against tokens
    std::optional<MatchResult> tryMatch(
        Pattern* pattern,
        const std::vector<Token>& tokens,
        size_t pos
    );

    // Parse an expression from tokens (for parameter captures)
    std::optional<std::pair<ExprPtr, size_t>> parseExpression(
        const std::vector<Token>& tokens,
        size_t pos,
        const std::string& stopWord = ""
    );

private:
    PatternRegistry& registry_;

    // Match a parameter (captures tokens based on parameter position)
    std::optional<std::pair<ExprPtr, size_t>> matchParam(
        const PatternElement& param,
        const std::vector<Token>& tokens,
        size_t pos,
        const std::vector<PatternElement>& remainingElements
    );

    // Parse a primary expression (literal, identifier, or parenthesized)
    std::optional<std::pair<ExprPtr, size_t>> parsePrimary(
        const std::vector<Token>& tokens,
        size_t pos
    );

    // Parse a binary expression with operator precedence
    std::optional<std::pair<ExprPtr, size_t>> parseBinaryExpr(
        const std::vector<Token>& tokens,
        size_t pos,
        int minPrecedence
    );

    // Get operator precedence (higher = binds tighter)
    int getOperatorPrecedence(TokenType type);

    // Check if token type is an operator
    bool isOperator(TokenType type);

    // Check if word would stop parameter capture
    bool isStopWord(const std::string& word, const std::vector<PatternElement>& remaining);
};

} // namespace tbx
