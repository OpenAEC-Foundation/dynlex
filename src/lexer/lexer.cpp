#include "lexer/lexer.hpp"
#include <cctype>

namespace tbx {

Lexer::Lexer(const std::string &src, const std::string &file)
    : source(src), filename(file) {}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  while (!atEnd()) {
    tokens.push_back(nextToken());
    if (tokens.back().type == TokenType::END_OF_FILE) {
      break;
    }
  }
  return tokens;
}

Token Lexer::nextToken() {
  skipWhitespace();

  if (atEnd()) {
    return makeToken(TokenType::END_OF_FILE, "");
  }

  char currentChar = advance();

  // Numbers
  if (std::isdigit(currentChar)) {
    pos--;
    column--;
    return scanNumber();
  }

  // Strings
  if (currentChar == '"') {
    return scanString();
  }

  // Identifiers (all words are identifiers - no keyword distinction)
  if (std::isalpha(currentChar) || currentChar == '_') {
    pos--;
    column--;
    return scanIdentifier();
  }

  // Single and multi-character tokens
  switch (currentChar) {
  case ':':
    return makeToken(TokenType::COLON, ":");
  case '@':
    return makeToken(TokenType::AT, "@");
  case '#':
    return makeToken(TokenType::HASH, "#");
  case '[':
    return makeToken(TokenType::LBRACKET, "[");
  case ']':
    return makeToken(TokenType::RBRACKET, "]");
  case '{':
    return makeToken(TokenType::LBRACE, "{");
  case '}':
    return makeToken(TokenType::RBRACE, "}");
  case '|':
    return makeToken(TokenType::PIPE, "|");
  case '\n':
    line++;
    column = 1;
    return makeToken(TokenType::NEWLINE, "\\n");
  }

  // Any other character becomes a SYMBOL token
  return makeToken(TokenType::SYMBOL, std::string(1, currentChar));
}

Token Lexer::peek() {
  size_t savedPos = pos;
  size_t savedLine = line;
  size_t savedColumn = column;

  Token token = nextToken();

  pos = savedPos;
  line = savedLine;
  column = savedColumn;

  return token;
}

Token Lexer::peekAhead(size_t count) {
  size_t savedPos = pos;
  size_t savedLine = line;
  size_t savedColumn = column;

  Token token;
  for (size_t tokenIndex = 0; tokenIndex <= count; tokenIndex++) {
    token = nextToken();
  }

  pos = savedPos;
  line = savedLine;
  column = savedColumn;

  return token;
}

char Lexer::current() const {
  if (atEnd())
    return '\0';
  return source[pos];
}

char Lexer::advance() {
  column++;
  return source[pos++];
}

bool Lexer::atEnd() const { return pos >= source.size(); }

void Lexer::skipWhitespace() {
  while (!atEnd()) {
    char currentChar = current();
    if (currentChar == ' ' || currentChar == '\t' || currentChar == '\r') {
      advance();
    } else if (currentChar == '#') {
      // Note: nextToken handles # if we want it as a token,
      // but skipWhitespace is usually called first.
      // If we want HASH tokens, we should remove this skip.
      // For now, let's keep skipping comments during normal lexing
      // unless we're in a mode that needs them (like LSP).
      while (!atEnd() && current() != '\n') {
        advance();
      }
    } else {
      break;
    }
  }
}

Token Lexer::makeToken(TokenType type, const std::string &lexeme) {
  Token token;
  token.type = type;
  token.lexeme = lexeme;
  token.location = {line, column - lexeme.size(), filename};
  return token;
}

Token Lexer::scanString() {
  std::string stringValue;
  while (!atEnd() && current() != '"') {
    if (current() == '\n') {
      line++;
      column = 1;
    }
    if (current() == '\\' && pos + 1 < source.size()) {
      advance();
      switch (current()) {
      case 'n':
        stringValue += '\n';
        break;
      case 't':
        stringValue += '\t';
        break;
      case '"':
        stringValue += '"';
        break;
      case '\\':
        stringValue += '\\';
        break;
      default:
        stringValue += current();
      }
    } else {
      stringValue += current();
    }
    advance();
  }

  if (atEnd()) {
    return makeToken(TokenType::ERROR, "Unterminated string");
  }

  advance(); // closing "

  Token token = makeToken(TokenType::STRING, "\"" + stringValue + "\"");
  token.value = stringValue;
  return token;
}

Token Lexer::scanNumber() {
  std::string num;
  bool isFloat = false;

  while (!atEnd() && std::isdigit(current())) {
    num += advance();
  }

  if (current() == '.' && pos + 1 < source.size() &&
      std::isdigit(source[pos + 1])) {
    isFloat = true;
    num += advance(); // .
    while (!atEnd() && std::isdigit(current())) {
      num += advance();
    }
  }

  Token token = makeToken(isFloat ? TokenType::FLOAT : TokenType::INTEGER, num);
  if (isFloat) {
    token.value = std::stod(num);
  } else {
    token.value = std::stoll(num);
  }
  return token;
}

Token Lexer::scanIdentifier() {
  std::string id;
  while (!atEnd() && (std::isalnum(current()) || current() == '_')) {
    id += advance();
  }

  // All words are identifiers - no keyword distinction
  // Pattern matching determines if a word is a literal or parameter
  return makeToken(TokenType::IDENTIFIER, id);
}

} // namespace tbx
