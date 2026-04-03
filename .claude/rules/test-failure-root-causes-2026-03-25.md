## Verified Root Causes From `./scripts/test.sh` on 2026-03-25

This file tracks the currently active root causes after the fixes already landed for
flex real-inference, section-header outermost grouping, the matcher memory blow-up,
and `construct` codegen type reconstruction.

### Active compiler bugs

- `src/compiler/type_inference/operand_reordering.inl` now records ambiguity warnings at the currently selected root expression (`expr`) instead of the minimal ambiguous sub-expression. That shifts diagnostics from inner spans like `2 + 3 + 4` to enclosing statements like `print 2 + 3 + 4 as line`.
- `src/compiler/type_inference/operand_reordering.inl` emits regrouping warnings for internal stdlib sources as if they were user-code diagnostics. These `lib/string.dl` warnings leak into unrelated required tests.
- `src/compiler/parseContext.cpp` + `src/compiler/pattern/pattern_tree/matchProgress.cpp` still spend too much time in `ParseContext::match` queue growth with deep `MatchProgress` copy chains on long references, causing `tests/required/lib` to timeout.
- `src/compiler/patternResolution.cpp` still does not recursively expand captured runtime arguments that were materialized during pattern expansion, so `Pending` nodes can leak into later stages.
- `src/compiler/type_inference/type_resolution.inl` still does not resolve single-token `Pending` expressions through active bindings during compile-time type resolution. This breaks flex type parameters such as `elementtype`.

### Current failure map

- `ambiguous_grouping`, `nested_inner_grouping`, `precedence`, `recursion`, `specificity`, and `string_form_integer` are failing on ambiguity warning-scope drift (enclosing statement warning text/range instead of inner ambiguous expression scope).
- `array_literal`, `class`, `group_precedence`, and `pattern` fail because internal `lib/string.dl` ambiguity warnings leak into tests that do not expect diagnostics.
- `lib` fails with compile timeout from matcher-state/copy amplification in `ParseContext::match`.

### Fixed root causes from this triage pass

- Flex replacement bodies used to be inferred through the rollback-only probe path even during real inference in `src/compiler/type_inference/function_inference.inl`.
- Section headers used to be flattened into generic operand regrouping in `src/compiler/type_inference/operand_reordering.inl`.
- Function-flex codegen used to re-expand raw replacement source instead of cloning the grouped flex body selected during type inference, so wrappers like `add 2 to total` could still reach codegen as `+ [void, i32]` even after inference had found the right grouping.
- Failed operand-grouping trials used to latch the first rejected diagnostic into the shared inference context, so wrappers like `add 0.5 to a` reported the earlier `nothing + float` failure instead of the later variable-type-change error.
- Failed operand-grouping trials now keep a scored failure record in `src/compiler/type_inference/operand_reordering.inl` that owns the exact `Diagnostic`. The current ranking is intentionally simple: `void`/`nothing`-based failures score lower than all other failures, so trial-order no longer decides between those two classes.
- Failure selection in operand regrouping now uses explicit per-report scores instead of message-text matching. The regrouping trial payload stores a cause diagnostic plus score, and the final emitted error is rebuilt at the outer expression range so nested failures still report the right source span.
- `src/compiler/type_inference/operand_reordering.inl` now snapshots and restores `Expression::isExplicitGroup` together with argument vectors while exploring opaque regroupings. That removed the deterministic leak where a failed opaque branch left `isExplicitGroup=true` on siblings and prevented enclosed-first enumeration from ever emitting valid nested postfix candidates like `print ((the length of (the text of (123 at character 0))) as line)`.
- `src/compiler/patternResolution.cpp` deadlock recovery no longer promotes words from multi-word body references. Cyclic implicit-parameter promotion is now restricted to unresolved single-token references in the same section chain, which prevents recursive suffix literals like `nested`/`inverted` from being mutated into parameters and collapsing distinct recursive signatures.
- `src/compiler/codegen/codegenTypes.cpp` used to assume flex-expanded `@intrinsic("construct", ...)` clones still carried `expr->type`.
- A temporary relaxation in `src/compiler/pattern/pattern_tree/matchProgress.cpp` caused runaway matcher-state growth and large RSS spikes.
