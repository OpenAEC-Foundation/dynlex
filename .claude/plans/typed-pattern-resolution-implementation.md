# Typed Pattern Resolution — Implementation Plan

## Current Architecture (as-is)

The pipeline is strictly sequential with no feedback:

```
Pattern Resolution  →  expandMatch  →  Type Inference  →  Codegen
(patternResolution.cpp)  (patternResolution.cpp)  (typeInference.cpp)
```

Key details:
- `ParseContext::match()` returns the **first** complete match (LIFO queue, literals have highest priority). No alternatives stored.
- `selectOverload()` picks among definitions at the **same tree endpoint** based on type constraints. It cannot try a different tree path.
- When ALL overloads fail type constraints at an endpoint, `selectOverload` returns null and falls back to `defs[0]` — a silent miscompile.
- The `unresolveReference()` / `invalidateStaleMatches()` mechanism already handles full re-resolution (variable bindings, VL reclassification, ancestor re-resolution), but is only triggered during pattern resolution when a more-specific definition is added. Never triggered by type inference.

## The Gap

After type inference discovers that a PatternCall's matched tree path is wrong (no overload satisfies type constraints), **there is no mechanism to rematch using a different tree path.** The expression tree is frozen.

## Approach: Post-Inference Validation + Re-Resolution Loop

Wrap the pipeline in an outer loop:

```
repeat (max N iterations):
    1. Pattern Resolution  (resolvePatterns)
    2. Expand Expressions   (expandMatch / expandExpression)
    3. Type Inference        (inferTypes)
    4. Validate Matches      (NEW)
       → find PatternCalls where selectOverload returns null
       → unresolve those references
       → re-add to resolution queue
    if no mismatches found: break
```

## Step-by-Step Changes

### 1. Detect type-mismatched PatternCalls (NEW function in typeInference.cpp)

After `inferTypes()`, walk all expressions. For each `PatternCall`:
- Run `selectOverload()` with the now-known argument types
- If it returns null (all overloads fail type constraints), record the PatternReference for re-resolution
- Return list of mismatched references

```cpp
// typeInference.cpp (new)
std::vector<PatternReference*> findTypeMismatchedReferences(ParseContext &context);
```

Walk `context.mainSection` recursively, visiting each CodeLine's expression tree. For PatternCall expressions, check if selectOverload returns null with resolved arg types. Map back from Expression → PatternReference via the PatternMatch's source range.

### 2. Unresolve mismatched references (patternResolution.cpp)

Reuse the existing `unresolveReference()` mechanism. For each mismatched reference:
- Call `unresolveReference()` to remove variable references, revert VL→Variable promotions
- Add reference back to the body/global reference list
- Add affected ancestor sections back to unresolved sections list

This is exactly what `invalidateStaleMatches` does, but triggered by type information instead of a new definition.

### 3. Re-run pattern resolution with type hints (patternResolution.cpp)

On re-resolution iterations, type information is available. Two options:

**Option A (simple):** Don't change the matcher. The same "most-specific" match will be found again. Instead, after re-matching, immediately check types and reject if constraints fail. Continue exploring alternatives in the match queue.

This requires changing `ParseContext::match()` to accept an optional type-check callback. When a match completes, invoke the callback with the matched endpoint's overloads and argument positions. If no overload's type constraints are satisfiable, reject this match and continue the BFS queue.

**Option B (type-aware trie):** At argument nodes in the trie, check if the source element (when it's a known variable) has a type that could satisfy any overload's constraint at that node. Skip the argument branch if impossible.

Option A is cleaner — the matcher stays generic and the type check is an optional filter.

```cpp
// parseContext.h
using MatchFilter = std::function<bool(const PatternMatch&, const PatternTreeNode*)>;
PatternMatch *match(PatternReference *reference, MatchFilter filter = nullptr);

// parseContext.cpp - modified
PatternMatch *ParseContext::match(PatternReference *reference, MatchFilter filter) {
    MatchProgress progress = MatchProgress(this, reference);
    std::vector<MatchProgress> queue = {progress};
    while (queue.size()) {
        MatchProgress &current = queue.back();
        std::vector<MatchProgress> nextSteps = current.step();
        if (current.isComplete()) {
            if (!filter || filter(current.match, current.currentNode))
                return new PatternMatch(current.match);
            // else: match rejected by type filter, continue exploring
        }
        queue.pop_back();
        queue.insert(queue.end(), nextSteps.begin(), nextSteps.end());
    }
    return nullptr;
}
```

The filter would check: for each overload at the matched endpoint, do the argument types (looked up from variables' inferred types) satisfy the type constraints? If at least one overload passes, accept the match.

### 4. Re-expand and re-infer

After re-resolution:
- Re-run `expandMatch` / `expandExpression` for the re-resolved references (creates new Expression nodes)
- Re-run `inferTypes()` (the fixed-point loop naturally handles changed expressions)

### 5. Outer loop convergence

The outer loop terminates when:
- No type mismatches found (stable), OR
- Max iterations reached (report as error — ambiguous code)

Convergence argument: each iteration either eliminates a match candidate (finite set) or finds a stable state. With finite patterns and finite code, the loop terminates.

## Files to Modify

| File | Change |
|------|--------|
| `compiler.cpp` | Wrap resolve→expand→infer in outer loop |
| `typeInference.cpp` | Add `findTypeMismatchedReferences()` |
| `patternResolution.cpp` | Extract unresolve logic for external use; add re-resolution entry point |
| `parseContext.h/cpp` | Add optional MatchFilter to `match()` |
| `compiler.h` | Expose new function declarations |

## Risk Assessment

- **Expression tree ownership:** Expressions are arena-allocated, so old expressions from a previous iteration are leaked but not double-freed. Acceptable per project conventions.
- **Variable identity:** Variables persist across re-resolution (only VariableReferences are added/removed). Variable types from the previous inference pass survive into the next iteration.
- **Macro body shared state:** Macro bodies are shared and reset before each inference. Re-resolution doesn't break this.
- **Precedence re-matching:** Already happens once during pattern resolution (phase 3). If the outer loop triggers, precedence may need re-evaluation. For now, skip — precedence is structural (operator order), not type-dependent.

## Not Needed Now

- No changes to codegen — it already uses `selectOverload` which handles the final type-based selection
- No changes to the pattern tree structure — different tree paths already exist
- No changes to variable scoping — `unresolveReference` already handles scope cleanup
