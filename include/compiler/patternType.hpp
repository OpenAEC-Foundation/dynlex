#pragma once

namespace tbx {

/**
 * Pattern type enumeration
 * Corresponds to the prefixes: "effect ", "expression ", "section "
 * Note: "condition" is treated as "expression" (booleans are just expressions)
 */
enum class PatternType {
  Effect,     // effect print msg:
  Expression, // expression left + right: (includes conditions)
  Section     // section loop:
};

} // namespace tbx
