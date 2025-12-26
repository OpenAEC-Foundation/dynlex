#include "lexer/token.hpp"

namespace tbx {

std::string token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::INTEGER:     return "INTEGER";
        case TokenType::FLOAT:       return "FLOAT";
        case TokenType::STRING:      return "STRING";
        case TokenType::IDENTIFIER:  return "IDENTIFIER";
        case TokenType::IF:          return "IF";
        case TokenType::THEN:        return "THEN";
        case TokenType::ELSE:        return "ELSE";
        case TokenType::LOOP:        return "LOOP";
        case TokenType::WHILE:       return "WHILE";
        case TokenType::FUNCTION:    return "FUNCTION";
        case TokenType::RETURN:      return "RETURN";
        case TokenType::SET:         return "SET";
        case TokenType::TO:          return "TO";
        case TokenType::IS:          return "IS";
        // Pattern system keywords
        case TokenType::PATTERN:     return "PATTERN";
        case TokenType::SYNTAX:      return "SYNTAX";
        case TokenType::WHEN:        return "WHEN";
        case TokenType::PARSED:      return "PARSED";
        case TokenType::TRIGGERED:   return "TRIGGERED";
        case TokenType::PRIORITY:    return "PRIORITY";
        case TokenType::IMPORT:      return "IMPORT";
        case TokenType::USE:         return "USE";
        case TokenType::FROM:        return "FROM";
        case TokenType::THE:         return "THE";
        // Class system keywords
        case TokenType::CLASS:       return "CLASS";
        case TokenType::EXPRESSION:  return "EXPRESSION";
        case TokenType::MEMBERS:     return "MEMBERS";
        case TokenType::CREATED:     return "CREATED";
        case TokenType::NEW:         return "NEW";
        case TokenType::OF:          return "OF";
        case TokenType::A:           return "A";
        case TokenType::AN:          return "AN";
        case TokenType::WITH:        return "WITH";
        case TokenType::BY:          return "BY";
        case TokenType::EACH:        return "EACH";
        case TokenType::MEMBER:      return "MEMBER";
        case TokenType::PRINT:       return "PRINT";
        // Operators
        case TokenType::PLUS:        return "PLUS";
        case TokenType::MINUS:       return "MINUS";
        case TokenType::STAR:        return "STAR";
        case TokenType::SLASH:       return "SLASH";
        case TokenType::EQUALS:      return "EQUALS";
        case TokenType::NOT_EQUALS:  return "NOT_EQUALS";
        case TokenType::LESS:        return "LESS";
        case TokenType::GREATER:     return "GREATER";
        case TokenType::COLON:       return "COLON";
        case TokenType::NEWLINE:     return "NEWLINE";
        case TokenType::INDENT:      return "INDENT";
        case TokenType::DEDENT:      return "DEDENT";
        case TokenType::COMMA:       return "COMMA";
        case TokenType::LPAREN:      return "LPAREN";
        case TokenType::RPAREN:      return "RPAREN";
        case TokenType::AT:          return "AT";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::ERROR:       return "ERROR";
        default:                     return "UNKNOWN";
    }
}

} // namespace tbx
