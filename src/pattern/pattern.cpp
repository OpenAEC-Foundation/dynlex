#include "pattern/pattern.hpp"

namespace tbx {

void Pattern::compile() {
    literal_count = 0;
    first_word.clear();

    for (size_t i = 0; i < elements.size(); ++i) {
        if (!elements[i].is_param) {
            // Count all literals (both optional and required)
            literal_count++;

            // First word should be the first non-optional literal for indexing
            // This ensures "set result to 5" can find pattern "set [the] result to value"
            if (first_word.empty() && !elements[i].is_optional) {
                first_word = elements[i].value;
            }
        }
    }
}

bool Pattern::could_match(const std::vector<Token>& tokens, size_t pos) const {
    if (pos >= tokens.size()) return false;

    // If pattern starts with literal, check if first token matches
    if (!first_word.empty()) {
        if (tokens[pos].type != TokenType::IDENTIFIER &&
            tokens[pos].lexeme != first_word) {
            return false;
        }
    }

    return true;
}

int Pattern::specificity() const {
    // Higher specificity = more required literal words = more specific match
    // Optional literals add less specificity than required ones
    int requiredLiterals = 0;
    int optionalLiterals = 0;

    for (const auto& elem : elements) {
        if (!elem.is_param) {
            if (elem.is_optional) {
                optionalLiterals++;
            } else {
                requiredLiterals++;
            }
        }
    }

    // Required literals are worth more than optional ones
    // This ensures more specific patterns match first
    return requiredLiterals * 10 + optionalLiterals * 5 + (int)elements.size();
}

} // namespace tbx
