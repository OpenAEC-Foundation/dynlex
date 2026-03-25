---
paths:
  - "src/compiler/pattern/**"
  - "src/compiler/patternResolution.cpp"
---

# Pattern Matching & Resolution

## Operator Precedence
- Defined via `before:`/`after:` sections in pattern definitions (e.g. `*` has `before: $ + $, $ - $`)
- Topological sort (Kahn's algorithm) with **wave-based BFS level assignment** — all nodes in same BFS wave get same precedence value
- Enforced during matching via `maxPrecedence` (left arg constraint) and `minRightPrecedence` (right arg constraint)
- Check: `def->precedence > maxPrecedence || def->precedence >= minRightPrecedence` → reject
- Same-precedence operators enforce left-to-right associativity (e.g. `a / b * c` → `(a/b)*c`)

## Pattern Resolution Loop
- No-progress detection for recursive patterns: if stuck with unresolved sections, force-resolve them (remaining VariableLike elements become parameters)
- `definitionToReferences` must track sub-match definitions too (not just top-level)

## Pattern Specificity Rematching
- More-specific definition added → invalidate references matched to less-specific definition → re-match
- `findLessSpecificDefinitions`: dual-path walk — main path follows exact elements, less-specific path follows argument alternatives
- Argument nodes on less-specific path can absorb multiple elements (sub-expression spans)
- Invalidation flow: unresolve → revert Variable→VariableLike → mark unresolved → re-add to unResolvedSections

## Match Progress (matchProgress.cpp)
- DFS with stack (vector, process back). First complete match wins.
- Priority order in `step()`: lowest=completion/stepUp, then arguments, then word capture, highest=literal match
- Case B: submatch fills right arg → set `minRightPrecedence` on parent continuation
- Case C: completed match becomes left arg → set `maxPrecedence` on new operator
- Case D: new submatch → reset precedence constraints (fresh expression parse)

## Bugs Fixed (pattern matching)
- **sourceArgumentIndex**: was incrementing `this` instead of `substituteStep`. Fix: `substituteStep.sourceArgumentIndex++`
- **Submatch patternPos**: not propagated to parent after submatch completion. Fix: `stepUp.patternPos = patternPos`
- **Keyframe shift**: `replaceLocal()` used `+` instead of `-`. Over-shifted keyframes by `2 * replacement.length()`
- **Precedence levels**: Sequential numbering gave `*`/`/` different levels. Fix: wave-based BFS assignment.
- **Function submatch recursion guard must stay strict**: relaxing `MatchProgress::canStartSubmatch()` to walk ancestor state explodes the DFS search space during ordinary stdlib matching, causing runaway compile-time memory growth. The guard must keep blocking fresh function-root submatches from another function-root state; recursive pattern-resolution fixes belong in the deadlock/classification logic, not the matcher.
