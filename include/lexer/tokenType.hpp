#pragma once

#include <string_view>

namespace tbx {

/**
 * @brief 3BX Token Types
 *
 * Unlike traditional languages, 3BX does not have a large set of reserved
 * keywords. Most "words" are identified as IDENTIFIER and then resolved by the
 * PatternResolver based on the registered patterns (expression, effect,
 * section).
 */
enum class TokenType {
  // Literals & Identifiers
  INTEGER,
  FLOAT,
  STRING,
  IDENTIFIER, // Any word (could be a pattern literal or a variable)

  // Structural Elements
  COLON, // Indicates a section start
  NEWLINE,
  INDENT,
  DEDENT,

  // Special Characters used in specific contexts
  AT,   // Used for @intrinsic
  HASH, // Used for #comments (if not stripped)

  // Pattern Syntax (used when parsing pattern definitions)
  LBRACKET, // [
  RBRACKET, // ]
  LBRACE,   // {
  RBRACE,   // }
  PIPE,     // |

  // Miscellaneous
  SYMBOL, // Any other single character (operators like +, -, *, / etc.)
  END_OF_FILE,
  ERROR
};

// Convert token type to string for debugging
constexpr std::string_view tokenTypeToString(TokenType type) {
  switch (type) {
  case TokenType::INTEGER:
    return "INTEGER";
  case TokenType::FLOAT:
    return "FLOAT";
  case TokenType::STRING:
    return "STRING";
  case TokenType::IDENTIFIER:
    return "IDENTIFIER";
  case TokenType::COLON:
    return "COLON";
  case TokenType::NEWLINE:
    return "NEWLINE";
  case TokenType::INDENT:
    return "INDENT";
  case TokenType::DEDENT:
    return "DEDENT";
  case TokenType::AT:
    return "AT";
  case TokenType::HASH:
    return "HASH";
  case TokenType::LBRACKET:
    return "LBRACKET";
  case TokenType::RBRACKET:
    return "RBRACKET";
  case TokenType::LBRACE:
    return "LBRACE";
  case TokenType::RBRACE:
    return "RBRACE";
  case TokenType::PIPE:
    return "PIPE";
  case TokenType::SYMBOL:
    return "SYMBOL";
  case TokenType::END_OF_FILE:
    return "EOF";
  case TokenType::ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

} // namespace tbx
