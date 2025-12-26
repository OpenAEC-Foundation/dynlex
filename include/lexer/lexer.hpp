#pragma once

#include "lexer/token.hpp"
#include <string>
#include <vector>

namespace tbx {

class Lexer {
public:
    explicit Lexer(const std::string& source, const std::string& filename = "<stdin>");

    // Tokenize the entire source
    std::vector<Token> tokenize();

    // Get next token
    Token next_token();

    // Peek at next token without consuming
    Token peek();

    // Peek N tokens ahead (0 = next token, 1 = token after that, etc.)
    Token peekAhead(size_t n);

private:
    std::string source_;
    std::string filename_;
    size_t pos_ = 0;
    size_t line_ = 1;
    size_t column_ = 1;

    char current() const;
    char advance();
    bool at_end() const;
    void skip_whitespace();

    Token make_token(TokenType type, const std::string& lexeme);
    Token scan_string();
    Token scan_number();
    Token scan_identifier();
};

} // namespace tbx
