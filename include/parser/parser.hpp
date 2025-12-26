#pragma once

#include "lexer/lexer.hpp"
#include "ast/ast.hpp"
#include "pattern/pattern_registry.hpp"
#include "pattern/pattern_matcher.hpp"
#include <memory>
#include <vector>
#include <set>

namespace tbx {

class Parser {
public:
    explicit Parser(Lexer& lexer);

    // Parse entire program
    std::unique_ptr<Program> parse();

    // Get the pattern registry (patterns defined in parsed code)
    PatternRegistry& getRegistry() { return registry_; }

    // Set shared registry for multi-file parsing
    void setSharedRegistry(PatternRegistry* registry);

private:
    Lexer& lexer_;
    Token current_;
    Token previous_;
    PatternRegistry registry_;
    PatternRegistry* sharedRegistry_ = nullptr;
    std::unique_ptr<PatternMatcher> matcher_;

    // All tokens for pattern matching
    std::vector<Token> tokens_;
    size_t tokenPos_ = 0;

    // Current indentation tracking
    int currentIndent_ = 0;

    void advance();
    bool check(TokenType type);
    bool match(TokenType type);
    Token consume(TokenType type, const std::string& message);

    // Check if at specific token type
    bool checkAt(size_t pos, TokenType type);

    // Skip newlines and track indentation
    int skipNewlinesAndGetIndent();

    // Get the active registry (shared or local)
    PatternRegistry& activeRegistry();

    // Grammar rules
    StmtPtr statement();
    StmtPtr setStatement();
    StmtPtr ifStatement();
    StmtPtr whileStatement();
    StmtPtr functionDeclaration();
    StmtPtr patternDefinition();
    StmtPtr classDefinition();
    StmtPtr expressionDefinition();
    StmtPtr effectDefinition();
    StmtPtr importStatement();
    StmtPtr importFunctionDeclaration();
    StmtPtr useStatement();

    // Try to match a statement against registered patterns
    StmtPtr matchPatternStatement();

    // Parse pattern syntax line (e.g., "set var to val")
    std::vector<PatternElement> parsePatternSyntax();

    // Parse pattern syntax until colon (for inline definitions)
    std::vector<PatternElement> parsePatternSyntaxUntilColon();

    // Parse a comma-separated list of identifiers
    std::vector<std::string> parseIdentifierList();

    // Parse indented block
    std::vector<StmtPtr> parseBlock();

    // Parse block with explicit indent level
    std::vector<StmtPtr> parseBlockWithIndent(int baseIndent);

    ExprPtr expression();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr primary();
    ExprPtr intrinsicCall();
};

} // namespace tbx
