#include "lexer/lexer.hpp"
#include <cctype>
#include <unordered_map>

namespace tbx {

static const std::unordered_map<std::string, TokenType> keywords = {
    {"if", TokenType::IF},
    {"then", TokenType::THEN},
    {"else", TokenType::ELSE},
    {"loop", TokenType::LOOP},
    {"while", TokenType::WHILE},
    {"function", TokenType::FUNCTION},
    {"return", TokenType::RETURN},
    {"set", TokenType::SET},
    {"to", TokenType::TO},
    {"is", TokenType::IS},
    // Pattern system keywords
    {"pattern", TokenType::PATTERN},
    {"syntax", TokenType::SYNTAX},
    {"when", TokenType::WHEN},
    {"parsed", TokenType::PARSED},
    {"triggered", TokenType::TRIGGERED},
    {"priority", TokenType::PRIORITY},
    {"import", TokenType::IMPORT},
    {"use", TokenType::USE},
    {"from", TokenType::FROM},
    {"the", TokenType::THE},
    // Class system keywords
    {"class", TokenType::CLASS},
    {"expression", TokenType::EXPRESSION},
    {"members", TokenType::MEMBERS},
    {"created", TokenType::CREATED},
    {"new", TokenType::NEW},
    {"of", TokenType::OF},
    {"a", TokenType::A},
    {"an", TokenType::AN},
    {"with", TokenType::WITH},
    {"by", TokenType::BY},
    {"each", TokenType::EACH},
    {"member", TokenType::MEMBER},
    {"print", TokenType::PRINT},
    {"effect", TokenType::EFFECT},
    {"get", TokenType::GET},
    {"patterns", TokenType::PATTERNS},
    {"result", TokenType::RESULT},
    {"multiply", TokenType::MULTIPLY},
};

Lexer::Lexer(const std::string& source, const std::string& filename)
    : source_(source), filename_(filename) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!at_end()) {
        tokens.push_back(next_token());
        if (tokens.back().type == TokenType::END_OF_FILE) {
            break;
        }
    }
    return tokens;
}

Token Lexer::next_token() {
    skip_whitespace();

    if (at_end()) {
        return make_token(TokenType::END_OF_FILE, "");
    }

    char c = advance();

    // Numbers
    if (std::isdigit(c)) {
        pos_--;
        column_--;
        return scan_number();
    }

    // Strings
    if (c == '"') {
        return scan_string();
    }

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        pos_--;
        column_--;
        return scan_identifier();
    }

    // Single character tokens
    switch (c) {
        case '+': return make_token(TokenType::PLUS, "+");
        case '-': return make_token(TokenType::MINUS, "-");
        case '*': return make_token(TokenType::STAR, "*");
        case '/': return make_token(TokenType::SLASH, "/");
        case ':': return make_token(TokenType::COLON, ":");
        case '<': return make_token(TokenType::LESS, "<");
        case '>': return make_token(TokenType::GREATER, ">");
        case '@': return make_token(TokenType::AT, "@");
        case '(': return make_token(TokenType::LPAREN, "(");
        case ')': return make_token(TokenType::RPAREN, ")");
        case '[': return make_token(TokenType::LBRACKET, "[");
        case ']': return make_token(TokenType::RBRACKET, "]");
        case ',': return make_token(TokenType::COMMA, ",");
        case '\'': return make_token(TokenType::APOSTROPHE, "'");
        case '\n':
            line_++;
            column_ = 1;
            return make_token(TokenType::NEWLINE, "\\n");
        case '=':
            if (current() == '=') {
                advance();
                return make_token(TokenType::EQUALS, "==");
            }
            return make_token(TokenType::ERROR, "=");
        case '!':
            if (current() == '=') {
                advance();
                return make_token(TokenType::NOT_EQUALS, "!=");
            }
            return make_token(TokenType::ERROR, "!");
    }

    return make_token(TokenType::ERROR, std::string(1, c));
}

Token Lexer::peek() {
    size_t saved_pos = pos_;
    size_t saved_line = line_;
    size_t saved_column = column_;

    Token token = next_token();

    pos_ = saved_pos;
    line_ = saved_line;
    column_ = saved_column;

    return token;
}

Token Lexer::peekAhead(size_t n) {
    size_t saved_pos = pos_;
    size_t saved_line = line_;
    size_t saved_column = column_;

    Token token;
    for (size_t i = 0; i <= n; i++) {
        token = next_token();
    }

    pos_ = saved_pos;
    line_ = saved_line;
    column_ = saved_column;

    return token;
}

char Lexer::current() const {
    if (at_end()) return '\0';
    return source_[pos_];
}

char Lexer::advance() {
    column_++;
    return source_[pos_++];
}

bool Lexer::at_end() const {
    return pos_ >= source_.size();
}

void Lexer::skip_whitespace() {
    while (!at_end()) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
        } else if (c == '#') {
            // Skip comments
            while (!at_end() && current() != '\n') {
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::make_token(TokenType type, const std::string& lexeme) {
    Token token;
    token.type = type;
    token.lexeme = lexeme;
    token.location = {line_, column_ - lexeme.size(), filename_};
    return token;
}

Token Lexer::scan_string() {
    std::string value;
    while (!at_end() && current() != '"') {
        if (current() == '\n') {
            line_++;
            column_ = 1;
        }
        if (current() == '\\' && pos_ + 1 < source_.size()) {
            advance();
            switch (current()) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                default: value += current();
            }
        } else {
            value += current();
        }
        advance();
    }

    if (at_end()) {
        return make_token(TokenType::ERROR, "Unterminated string");
    }

    advance(); // closing "

    Token token = make_token(TokenType::STRING, "\"" + value + "\"");
    token.value = value;
    return token;
}

Token Lexer::scan_number() {
    std::string num;
    bool is_float = false;

    while (!at_end() && std::isdigit(current())) {
        num += advance();
    }

    if (current() == '.' && pos_ + 1 < source_.size() && std::isdigit(source_[pos_ + 1])) {
        is_float = true;
        num += advance(); // .
        while (!at_end() && std::isdigit(current())) {
            num += advance();
        }
    }

    Token token = make_token(is_float ? TokenType::FLOAT : TokenType::INTEGER, num);
    if (is_float) {
        token.value = std::stod(num);
    } else {
        token.value = std::stoll(num);
    }
    return token;
}

Token Lexer::scan_identifier() {
    std::string id;
    while (!at_end() && (std::isalnum(current()) || current() == '_')) {
        id += advance();
    }

    // Check for keywords
    auto it = keywords.find(id);
    if (it != keywords.end()) {
        return make_token(it->second, id);
    }

    return make_token(TokenType::IDENTIFIER, id);
}

} // namespace tbx
