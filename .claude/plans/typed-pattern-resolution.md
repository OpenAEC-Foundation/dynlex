# Typed Pattern Resolution — Iterative Refinement

## Problem

Pattern matching and type inference are mutually dependent:
- **Pattern matching** identifies variables vs keywords by overlaying code onto known patterns
- **Type inference** follows the call tree to determine expression types
- But **typed arguments** (`{type:name}`) can change which pattern matches, which changes what's a variable vs keyword

## Concrete Example

```dl
expression {string:s} to upper:       # "to" and "upper" are keywords
expression {i32:lo} to {i32:hi}:      # "to" is keyword, "hi" is variable
```

```dl
set x to 5
set upper to 10
set result to x to upper
```

Same tokens `x to upper` parse differently based on `x`'s type:

| `x` type | `upper` is | matched pattern |
|-----------|-----------|-----------------|
| string    | keyword   | `{string:s} to upper` |
| i32       | variable (=10) | `{i32:lo} to {i32:hi}` |

## Iterative Refinement Algorithm

1. **Round 1 — pattern match (no types):** Match all patterns using specificity (more keywords = more specific). `upper` classified as keyword.
2. **Round 1 — type inference:** Discover `x: i32`. Pattern A needs `string`. Mismatch.
3. **Round 2 — rematch:** Drop pattern A. Try pattern B. Reclassify `upper` from keyword → variable. Re-resolve in scope.
4. **Round 2 — type inference:** `x: i32`, `upper: i32`. Pattern B valid. Stable.

## Key Invariant

When a candidate is eliminated during rematch, the replacement candidate may reclassify tokens (keyword↔variable). Rematch must re-resolve variable bindings and scope lookups for affected tokens, not just re-rank patterns.

## Cascade Risk

A rematch can change:
- How many tokens an expression consumes (different pattern arity)
- What neighboring tokens belong to (affects outer expression parsing)
- Types returned by the expression (affects downstream type inference)

Each of these can trigger further rematches in dependent expressions.

## Current State

The existing specificity rematch system already handles keyword↔variable reclassification when a pattern is eliminated. The question is whether it converges for all cases, and whether the iteration order is correct.
