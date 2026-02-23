# Type-Aware Operand Reordering

## Design

### Pattern matching is type-agnostic

Pattern resolution runs without any type information. Keywords always win over variables (most-specific tree path by keyword count). The tree path is fixed after pattern resolution — no keyword↔variable reclassification happens later.

### Overload selection during type inference

Multiple definitions at the same tree endpoint (same keyword structure, different type constraints) are stored as overloads. During type inference, `selectOverload` picks the best matching definition based on argument types. If no overload matches with fully-deduced argument types, it's a type error.

### Operand reordering during type inference

Type inference processes code in execution order (top to bottom). For each line, when multiple patterns share argument edges, the nesting is ambiguous. The algorithm tries all 2^N groupings (where N = shared edges), preferring left-side as submatch (left-to-right evaluation).

**Shared argument edge:** two patterns share an argument edge when one's output could be another's input at an argument position. For example, in `set z to x + y`, the patterns `set $ to $` and `$ + $` share an edge — `x` could be the right arg of `set` or the left arg of `+`.

**Validation per grouping:**
- Evaluate submatch return types bottom-up (macros: first expression's type; functions: instantiate and infer body)
- Reject grouping if any submatch returns Void in value context
- Reject grouping if argument types fail type constraints
- First valid grouping wins; multiple valid = ambiguous error; none valid = type error

**Limit:** Error if more than 6 unresolved shared edges (precedence declarations resolve some edges).

### Operand reordering is instance-independent

The operand ordering for a given source location is the same across all instantiations of the enclosing function. If one instantiation reveals a type mismatch that requires reordering, all instantiations use the reordered grouping.

### What reordering can't solve

When the correct match requires a keyword↔variable reclassification (different tree path), reordering doesn't help. This is a type error with a diagnostic suggesting the variable may be shadowed by a keyword.

## Implementation

### Replace the type inference engine

The current fixed-point `runInference` loop is replaced by execution-order processing. For each code line, enumerate groupings, validate each, and commit the first valid one. Modify the expression tree directly when reordering.

### Remove dead code

- `matchTypesValid()` and `inferMatchReturnType()` in `parseContext.cpp`
- Type inference + reset code in the pattern resolution loop (`patternResolution.cpp`)
- `expressionTypesValid()` in `typeInference.cpp`
- The fixed-point `runInference` loop

### Reordering mechanics

The expression tree stores subexpressions as children. Reordering swaps the parent-child relationship between two PatternCall expressions at a shared edge. The inner expression's range is set to the shared operand's range so `sortArgumentsByPosition` maps parameters correctly.

### Return value enforcement

In DynLex, return values cannot be ignored. If a top-level expression returns non-Void, it's an error. Users wrap in `discard <value>` which calls `@intrinsic("discard", value)`.

### New intrinsic: `discard`

`@intrinsic("discard", value)` — evaluates `value` for side effects and discards the result. Return type: Void.
