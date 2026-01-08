#pragma once

#include "lexer/sourceLocation.hpp"
#include "lexer/tokenType.hpp"

#include <string>
#include <variant>

namespace tbx {

struct Token {
  TokenType type{};
  std::string lexeme;
  SourceLocation location;

  // Literal value if applicable
  std::variant<std::monostate, int64_t, double, std::string> value;
};

} // namespace tbx
