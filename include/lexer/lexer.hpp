#pragma once

#include "lexer/token.hpp"
#include <string>
#include <vector>

namespace tbx {

class Lexer {
public:
  explicit Lexer(const std::string &source,
                 const std::string &filename = "<stdin>");

  // Tokenize the entire source
  std::vector<Token> tokenize();

  // Get next token
  Token nextToken();

  // Peek at next token without consuming
  Token peek();

  // Peek N tokens ahead (0 = next token, 1 = token after that, etc.)
  Token peekAhead(size_t count);

private:
  std::string source;
  std::string filename;
  size_t pos = 0;
  size_t line = 1;
  size_t column = 1;

  char current() const;
  char advance();
  bool atEnd() const;
  void skipWhitespace();

  Token makeToken(TokenType type, const std::string &lexeme);
  Token scanString();
  Token scanNumber();
  Token scanIdentifier();
};

} // namespace tbx
