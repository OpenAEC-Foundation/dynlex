#pragma once

namespace tbx {

/**
 * Pattern element types for building patterns
 */
enum class PatternElementType {
  Literal,           // A literal string like "print " or " + "
  Variable,          // A $ variable slot (eager expression)
  ExpressionCapture, // A {expression:name} lazy capture (greedy, caller's
                     // scope)
  WordCapture        // A {word:name} single identifier capture (non-greedy)
};

} // namespace tbx
