# Type-Aware Operand Reordering

## Design

### Pattern matching is type-agnostic

Pattern resolution runs without any type information. Keywords always win over variables (most-specific tree path by keyword count). The tree path is fixed after pattern resolution — no keyword↔variable reclassification happens later.

### Overload selection during type inference

Multiple definitions at the same tree endpoint (same keyword structure, different type constraints) are stored as overloads. During type inference, `selectOverload` picks the best matching definition based on argument types. If no overload matches with fully-deduced argument types, it's a type error.

### Operand reordering during type inference

When type inference discovers that a subexpression grouping produces invalid types (e.g., a type constraint fails), it tries alternative groupings of operands along shared argument edges.

**Shared argument edge:** two patterns share an argument edge when one's output could be another's input at an argument position. For example, in `x plus y length`, the patterns `$ plus $` and `$ length` share an edge — `y` could be the right arg of `plus` or the left arg of `length`.

Reordering changes the nesting of subexpressions:
- `(x plus y) length` ↔ `x plus (y length)`

Reordering changes:
- The nesting of subexpressions (which arguments go to which pattern)
- Which overload is selected at each endpoint (different argument types → different overload)
- Return types of subexpressions (different overload → different return type)

Reordering does NOT change:
- Which tokens are keywords vs variables (tree path is fixed)
- Which tree endpoints are reached (same endpoints, just different nesting)

### Operand reordering is instance-independent

The operand ordering for a given source location is the same across all instantiations of the enclosing function. If one instantiation reveals a type mismatch that requires reordering, all instantiations use the reordered grouping.

### What reordering can't solve

When the correct match requires a keyword↔variable reclassification (different tree path), reordering doesn't help. This is a type error with a diagnostic suggesting the variable may be shadowed by a keyword.

Example: `x from start` where `{list:l} from start` (keywords win) is chosen over `{i32:x} from {i32:y}` (where `start` would be a variable). If `x: i32`, this is a type error — not solvable by reordering.

## Implementation Sketch

### During type inference (fixed-point loop)

When inferring a PatternCall expression:
1. Infer argument types (bottom-up as usual)
2. Run `selectOverload` with argument types
3. If no overload matches and argument types are deduced → try alternative operand groupings
4. For each alternative grouping: re-infer argument types, re-check selectOverload
5. If a valid grouping is found → restructure the expression tree, continue inference
6. If no grouping works → type error diagnostic

### Reordering mechanics

The expression tree stores subexpressions as children. Reordering swaps the parent-child relationship between two expressions that share an argument edge. For example:

```
Before:  PatternCall(plus, [x, PatternCall(length, [y])])
         meaning: x plus (y length)

After:   PatternCall(length, [PatternCall(plus, [x, y])])
         meaning: (x plus y) length
```

After reordering, the overloads at each endpoint may change:
- `plus` now gets [i32, i32] instead of [i32, string] → different overload selected
- `length` now gets [i32] instead of [string] → different overload selected (or fails)

### Consistency check

After reordering, verify that ALL instantiations of the enclosing function still type-check with the new grouping. If any instantiation needs a different ordering than another, emit an error: "ambiguous operand grouping: `x plus y length` groups differently for different argument types." The programmer must disambiguate explicitly.
