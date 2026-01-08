#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace tbx {

// Forward declaration
struct Section;

/**
 * Represents a resolved variable value
 * Can be a literal (number, string) or a reference to another pattern
 */
using ResolvedValue =
    std::variant<int64_t,                 // Integer literal
                 double,                  // Float literal
                 std::string,             // String literal or identifier
                 std::shared_ptr<Section> // Nested section/expression
                 >;

} // namespace tbx
