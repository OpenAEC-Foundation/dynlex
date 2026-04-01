# Next Agent Handoff (2026-04-01)

## User Instructions (Must Follow)
- Compiler behavior must be deterministic and correct; no workarounds.
- Do not treat compile-time values as a separate weaker path with different semantics.
- Pointer identity comparison must remain available for raw pointers.
- `cstring` content equality is needed, but it must not hijack all `i8* = i8*` comparisons.
- Keep fixes minimal, explicit, and performance-safe.
- Another agent should continue from this state.

## Repository-Wide Rules Reiterated by User
- This is a compiler project: only root-cause fixes, no temporary patches.
- Fail hard on internal inconsistencies.
- Keep behavior pattern/intrinsic driven, no hardcoding language behavior.
- Keep code DRY and performant.
- Document important fixes in shared rules/docs.

## What Was Done In This Session
- Ran `./scripts/test.sh`.
- Observed widespread failures, with many crashes: `FATAL: compiler bug: Runtime call argument produced no code`.
- Root-caused the crash path to non-macro call argument lowering in `src/cpp/compiler/codegen/codegen.cpp` when runtime code is requested for an argument whose expression emits no runtime IR.
- Added a `cstring` equality overload in `lib/string.dl`:
  - `function {cstring:left} = {cstring:right}`
  - byte-wise null-terminated comparison with pointer fast-path + null checks.
- Added test `tests/required/cstring_equality/` and it passes in isolation.
- Added a note to `.claude/rules/compiler.md` about keeping pointer intrinsic equality address-based and implementing cstring content equality in stdlib overloads.

## Critical Current Problem
- The new `{cstring} = {cstring}` overload reroutes generic `i8* = i8*` calls to cstring content compare.
- This hijacks pointer identity semantics for byte pointers unless callers use `@intrinsic("equal", left, right)` directly.

## Build-Info / Compile-Time Context
- `@intrinsic("build info", key)` is currently compile-time-only in codegen (`generateIntrinsicCode` returns no runtime value for it).
- Compile-time string equality exists in compile-time evaluation (`CompileTimeValue` string compare).
- Crash still reproduces on:
  - `discard 0 if (the build info "platform") = "wasm", else 1`
- Root-cause direction already identified:
  - compile-time-known argument values are seeded,
  - but some call-argument paths still attempt runtime lowering when compile-time-only propagation/requirement marking does not fully align.

## Required Next Decisions / Fix Work
- Decide how to provide cstring content equality without hijacking all pointer equality:
  - Option A: remove operator overload and provide explicit pattern name for cstring content compare.
  - Option B: introduce dedicated intrinsic/pattern for cstring content compare with explicit use sites.
  - Option C: add distinct type-level separation so raw byte pointers and cstrings are not conflated at `=` resolution.
- Keep raw pointer identity comparison intact for `i8*` where requested.
- Fix compile-time/runtime value propagation mismatch so compile-time-known expressions (including build-info-derived expressions) never get forced through runtime codegen paths that produce no IR.

## Files Touched In This Session
- `lib/string.dl`
- `tests/required/cstring_equality/main.dl`
- `tests/required/cstring_equality/expected.txt`
- `.claude/rules/compiler.md`

