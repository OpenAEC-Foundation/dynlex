# Type Inference with Operand Reordering

## Overview

Type inference processes code in **execution order** (top to bottom, one statement at a time). For each statement, it evaluates all possible operand groupings and picks the valid one.

## Algorithm

For each code line (in execution order):

1. **Identify the matched patterns** on this line. The pattern resolution phase already determined which tokens are keywords vs variables (the tree path is fixed). But when multiple patterns share argument edges, the nesting (which is a submatch of which) is ambiguous.

2. **Count shared edges.** A shared edge exists where two adjacent patterns share a boundary operand. The number of possible groupings = 2^(shared edges). Error if more than 6 unresolved shared edges (precedence declarations resolve some edges ahead of time).

3. **Try groupings with left-side as submatch first** (so `x - y - z` produces `(x - y) - z` — submatches are evaluated first, giving left-to-right evaluation).

4. **For each grouping, validate bottom-up:**
   - **Submatches first:** Evaluate each submatch's return type.
     - **Macros:** Return type = return type of the macro's first expression (macros are code replacement; a macro returning a value always has exactly one expression that returns it; a void macro can have multiple void statements).
     - **Functions:** Instantiate with the argument types passed. Infer the function body recursively (same algorithm). The instantiation's return type is the result.
   - **Check submatch return types:** A submatch in value context cannot return Void. If it does, this grouping is invalid — try the next one.
   - **Check argument types against constraints:** If the parent pattern has type constraints on its parameters, verify the submatch's return type satisfies them. If not, this grouping is invalid.
   - **If all checks pass:** This grouping is valid. Commit to it and move to the next line.

5. **If no grouping is valid:** Emit a type error diagnostic.

6. **Top-level return type check:** In DynLex, return values cannot be ignored. If the line's top-level expression returns a non-Void type, it's an error. The fix is to wrap it in a `discard` expression pattern which calls `@intrinsic("discard", value)`.

## Example: `simple` test

```
set x to 42        → macro, returns Void. No submatches, no reordering.
set y to 10        → macro, returns Void. No submatches, no reordering.
set z to x + y     → Two patterns: set $ to $, $ + $. One shared edge.
  Try 1: left as submatch of right → +(set(z, x), y)
    Submatch set(z, x): macro, first expression is @intrinsic("store", z, x) → Void.
    Void can't be returned from a submatch → INVALID.
  Try 2: right as submatch of left → set(z, +(x, y))
    Submatch +(x, y): macro, first expression is @intrinsic("add", x, y) → f64.
    set $ to $ accepts (var, f64) → VALID. Commit.
print z             → One pattern, no reordering possible.
  Instantiate print(f64). Inside the function body:
    @intrinsic("call", "libc", "printf", "i32", "%ld", msg as a 64 bit integer)
    Cast return type: i64. printf return type (from intrinsic): i32.
  print(f64) instantiation return type: i32.
  Top-level: print z returns i32 (non-Void) → error: return value not used (wrap in discard).
```

## Key Rules

- **Execution order:** Process lines top-to-bottom, not in fixed-point iteration.
- **Left-to-right default:** When trying groupings, prefer left-side as submatch (left-to-right evaluation order).
- **Void rejection:** A submatch in value context cannot return Void. This is the primary mechanism for rejecting wrong groupings.
- **Macro return type:** Return type of the macro's first expression.
- **Function instantiation:** Each unique argument type combination creates a new instantiation. The function body is inferred recursively using the same algorithm.
- **One valid grouping:** If exactly one grouping is valid, use it. If multiple are valid, it's ambiguous (error). If none are valid, it's a type error.
- **No type inference in pattern resolution:** Pattern resolution is purely structural. `matchTypesValid` is removed. Type-based match rejection happens in this algorithm instead.
- **Expression tree modification:** Reordering modifies the expression tree directly (swapping parent-child relationships between PatternCall nodes at shared edges).

## Code to remove

- `matchTypesValid()` in `parseContext.cpp` — replaced by Void rejection in this algorithm
- `inferMatchReturnType()` in `parseContext.cpp` — only used by `matchTypesValid`
- Type inference call (`runInference`) in the pattern resolution loop (`patternResolution.cpp`) — along with all the reset code (resetTypes, resetInstantiations, resetVarTypes)
- `expressionTypesValid()` in `typeInference.cpp` — validation moves into the new algorithm
- The current fixed-point `runInference` loop — replaced by execution-order processing
