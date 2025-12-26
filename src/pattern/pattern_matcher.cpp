#include "pattern/pattern_matcher.hpp"
#include <unordered_set>

namespace tbx {

PatternMatcher::PatternMatcher(PatternRegistry& registry)
    : registry_(registry) {}

std::optional<MatchResult> PatternMatcher::matchStatement(
    const std::vector<Token>& tokens,
    size_t pos
) {
    if (pos >= tokens.size()) return std::nullopt;

    // Get candidate patterns based on first token
    std::string firstWord;
    if (tokens[pos].type == TokenType::IDENTIFIER) {
        firstWord = tokens[pos].lexeme;
    } else {
        // For keyword tokens, use their lexeme
        firstWord = tokens[pos].lexeme;
    }

    auto candidates = registry_.getCandidates(firstWord);

    MatchResult bestMatch;
    bool found = false;

    for (Pattern* pattern : candidates) {
        auto result = tryMatch(pattern, tokens, pos);
        if (result) {
            if (!found || result->specificity > bestMatch.specificity) {
                bestMatch = std::move(*result);
                found = true;
            }
        }
    }

    if (found) {
        return bestMatch;
    }
    return std::nullopt;
}

std::optional<MatchResult> PatternMatcher::matchExpression(
    const std::vector<Token>& tokens,
    size_t pos,
    int minPrecedence
) {
    // For now, just try to match any expression pattern
    return matchStatement(tokens, pos);
}

std::optional<MatchResult> PatternMatcher::tryMatch(
    Pattern* pattern,
    const std::vector<Token>& tokens,
    size_t pos
) {
    size_t currentPos = pos;
    std::unordered_map<std::string, ExprPtr> bindings;

    for (size_t elementIndex = 0; elementIndex < pattern->elements.size(); ++elementIndex) {
        const auto& elem = pattern->elements[elementIndex];

        if (currentPos >= tokens.size()) {
            // Check if remaining elements are all optional (literals or parameters)
            bool allOptional = true;
            for (size_t j = elementIndex; j < pattern->elements.size(); ++j) {
                const auto& remaining = pattern->elements[j];
                // Optional literals can be skipped
                if (remaining.is_optional) {
                    continue;
                }
                // Parameters without remaining input cannot be satisfied
                if (remaining.is_param) {
                    allOptional = false;
                    break;
                }
                // Required literals without remaining input
                allOptional = false;
                break;
            }
            if (!allOptional) return std::nullopt;
            break;
        }

        if (!elem.is_param) {
            // Literal word
            const Token& token = tokens[currentPos];

            // Special handling for "'s" which is tokenized as APOSTROPHE + IDENTIFIER "s"
            if (elem.value == "'s") {
                if (token.type == TokenType::APOSTROPHE &&
                    currentPos + 1 < tokens.size() &&
                    tokens[currentPos + 1].lexeme == "s") {
                    currentPos += 2;  // Consume both ' and s
                } else if (elem.is_optional) {
                    // Optional 's not present, skip
                } else {
                    return std::nullopt;
                }
            } else if (elem.is_optional) {
                // Optional literal - try to match, but skip if not present
                if (token.lexeme == elem.value) {
                    // Optional word is present, consume it
                    currentPos++;
                }
                // If not present, just continue to next element (skip the optional)
            } else {
                // Required literal - must match exactly
                if (token.lexeme != elem.value) {
                    return std::nullopt;
                }
                currentPos++;
            }
        } else {
            // Parameter - capture expression
            std::vector<PatternElement> remaining(
                pattern->elements.begin() + elementIndex + 1,
                pattern->elements.end()
            );

            auto paramResult = matchParam(elem, tokens, currentPos, remaining);
            if (!paramResult) {
                return std::nullopt;
            }

            bindings[elem.value] = std::move(paramResult->first);
            currentPos = paramResult->second;
        }
    }

    MatchResult result;
    result.pattern = pattern;
    result.bindings = std::move(bindings);
    result.tokensConsumed = currentPos - pos;
    result.specificity = pattern->specificity();

    return result;
}

std::optional<std::pair<ExprPtr, size_t>> PatternMatcher::matchParam(
    const PatternElement& param,
    const std::vector<Token>& tokens,
    size_t pos,
    const std::vector<PatternElement>& remainingElements
) {
    if (pos >= tokens.size()) return std::nullopt;

    // Find the next literal word in remaining elements (if any)
    // Skip optional literals to find the next required stop word
    // But also track optional literals as potential stop words
    std::string nextRequiredLiteral;
    std::vector<std::string> optionalLiterals;

    for (const auto& elem : remainingElements) {
        if (!elem.is_param) {
            if (elem.is_optional) {
                optionalLiterals.push_back(elem.value);
            } else {
                nextRequiredLiteral = elem.value;
                break;
            }
        }
    }

    // Build a set of all stop words (both required and optional)
    std::unordered_set<std::string> stopWords;
    bool hasPossessiveStop = false;  // Special handling for "'s" stop word
    if (!nextRequiredLiteral.empty()) {
        stopWords.insert(nextRequiredLiteral);
        if (nextRequiredLiteral == "'s") hasPossessiveStop = true;
    }
    for (const auto& opt : optionalLiterals) {
        stopWords.insert(opt);
        if (opt == "'s") hasPossessiveStop = true;
    }

    // Helper to check if position is at a stop word (handles "'s" specially)
    auto isAtStopWord = [&](size_t checkPos) -> bool {
        if (checkPos >= tokens.size()) return false;
        // Check for "'s" as APOSTROPHE + "s"
        if (hasPossessiveStop &&
            tokens[checkPos].type == TokenType::APOSTROPHE &&
            checkPos + 1 < tokens.size() &&
            tokens[checkPos + 1].lexeme == "s") {
            return true;
        }
        return stopWords.count(tokens[checkPos].lexeme) > 0;
    };

    // Parse an expression, stopping at any stop word or end of line
    size_t endPos = pos;

    // Try to parse a full expression with operator precedence
    auto result = parseBinaryExpr(tokens, pos, 0);
    if (result) {
        endPos = result->second;

        // If there are stop words, make sure we stopped before or at one
        if (!stopWords.empty()) {
            // Check if the expression ended before a stop word
            if (isAtStopWord(endPos)) {
                return result;
            }

            // If we went past, try scanning for stop words
            size_t scanPos = pos;
            while (scanPos < tokens.size() &&
                   !isAtStopWord(scanPos) &&
                   tokens[scanPos].type != TokenType::NEWLINE &&
                   tokens[scanPos].type != TokenType::END_OF_FILE) {
                scanPos++;
            }

            if (scanPos > pos && (scanPos >= tokens.size() || isAtStopWord(scanPos))) {
                // Re-parse just the tokens before the stop word
                return parseBinaryExpr(tokens, pos, 0);
            }
        }

        return result;
    }

    // Fallback: try parsing just a primary expression
    return parsePrimary(tokens, pos);
}

std::optional<std::pair<ExprPtr, size_t>> PatternMatcher::parsePrimary(
    const std::vector<Token>& tokens,
    size_t pos
) {
    if (pos >= tokens.size()) return std::nullopt;

    const Token& token = tokens[pos];

    switch (token.type) {
        case TokenType::INTEGER: {
            auto lit = std::make_unique<IntegerLiteral>();
            lit->value = std::get<int64_t>(token.value);
            lit->location = token.location;
            return std::make_pair(std::move(lit), pos + 1);
        }
        case TokenType::FLOAT: {
            auto lit = std::make_unique<FloatLiteral>();
            lit->value = std::get<double>(token.value);
            lit->location = token.location;
            return std::make_pair(std::move(lit), pos + 1);
        }
        case TokenType::STRING: {
            auto lit = std::make_unique<StringLiteral>();
            lit->value = std::get<std::string>(token.value);
            lit->location = token.location;
            return std::make_pair(std::move(lit), pos + 1);
        }
        case TokenType::IDENTIFIER:
        case TokenType::RESULT:
        case TokenType::MULTIPLY:
        case TokenType::EFFECT:
        case TokenType::GET:
        case TokenType::PATTERNS: {
            // Identifiers and keyword tokens that act as identifiers
            auto id = std::make_unique<Identifier>();
            id->name = token.lexeme;
            id->location = token.location;
            return std::make_pair(std::move(id), pos + 1);
        }
        case TokenType::LPAREN: {
            // Parenthesized expression
            auto inner = parseBinaryExpr(tokens, pos + 1, 0);
            if (inner && inner->second < tokens.size() &&
                tokens[inner->second].type == TokenType::RPAREN) {
                return std::make_pair(std::move(inner->first), inner->second + 1);
            }
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

std::optional<std::pair<ExprPtr, size_t>> PatternMatcher::parseBinaryExpr(
    const std::vector<Token>& tokens,
    size_t pos,
    int minPrecedence
) {
    auto leftResult = parsePrimary(tokens, pos);
    if (!leftResult) return std::nullopt;

    ExprPtr left = std::move(leftResult->first);
    size_t currentPos = leftResult->second;

    while (currentPos < tokens.size()) {
        const Token& opToken = tokens[currentPos];

        if (!isOperator(opToken.type)) {
            break;
        }

        int precedence = getOperatorPrecedence(opToken.type);
        if (precedence < minPrecedence) {
            break;
        }

        currentPos++; // Consume operator

        // Parse right side with higher precedence for left-associativity
        auto rightResult = parseBinaryExpr(tokens, currentPos, precedence + 1);
        if (!rightResult) {
            break;
        }

        auto binary = std::make_unique<BinaryExpr>();
        binary->left = std::move(left);
        binary->op = opToken.type;
        binary->right = std::move(rightResult->first);
        binary->location = opToken.location;

        left = std::move(binary);
        currentPos = rightResult->second;
    }

    return std::make_pair(std::move(left), currentPos);
}

int PatternMatcher::getOperatorPrecedence(TokenType type) {
    switch (type) {
        case TokenType::STAR:
        case TokenType::SLASH:
            return 3;
        case TokenType::PLUS:
        case TokenType::MINUS:
            return 2;
        case TokenType::LESS:
        case TokenType::GREATER:
            return 1;
        case TokenType::EQUALS:
        case TokenType::NOT_EQUALS:
            return 0;
        default:
            return -1;
    }
}

bool PatternMatcher::isOperator(TokenType type) {
    return getOperatorPrecedence(type) >= 0;
}

bool PatternMatcher::isStopWord(const std::string& word, const std::vector<PatternElement>& remaining) {
    for (const auto& elem : remaining) {
        if (!elem.is_param && elem.value == word) {
            return true;
        }
    }
    return false;
}

std::optional<std::pair<ExprPtr, size_t>> PatternMatcher::parseExpression(
    const std::vector<Token>& tokens,
    size_t pos,
    const std::string& stopWord
) {
    if (pos >= tokens.size()) return std::nullopt;

    // If there's a stop word, find where to stop parsing
    if (!stopWord.empty()) {
        size_t stopPos = pos;
        while (stopPos < tokens.size() &&
               tokens[stopPos].lexeme != stopWord &&
               tokens[stopPos].type != TokenType::NEWLINE &&
               tokens[stopPos].type != TokenType::END_OF_FILE) {
            stopPos++;
        }

        if (stopPos > pos) {
            return parseBinaryExpr(tokens, pos, 0);
        }
        return std::nullopt;
    }

    return parseBinaryExpr(tokens, pos, 0);
}

} // namespace tbx
