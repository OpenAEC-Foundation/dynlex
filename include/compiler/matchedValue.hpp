#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace tbx {

// Forward declaration
struct ExpressionMatch;

/**
 * Represents a matched value in a pattern
 * Can be a literal (number, string) or a nested expression match
 */
using MatchedValue =
    std::variant<int64_t,     // Integer literal
                 double,      // Float literal
                 std::string, // String literal or identifier
                 std::shared_ptr<struct ExpressionMatch> // Nested expression
                 >;

} // namespace tbx
