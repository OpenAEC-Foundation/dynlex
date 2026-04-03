# Next Agent Handoff (2026-04-02)

## Scope Completed
- Removed the old post-inference compile-time evaluator path.
- Renamed the remaining inference-time helpers so they no longer claim to "evaluate" already-inferred expressions.
- Switched compile-time readers toward resolve-only access through stored expression values.
- Cleaned docs/code references so there are no remaining `evaluateCompileTime*` symbols or matching wording in `src/cpp`, `.claude/rules`, or `docs`.

## Current Status
- `./scripts/build.sh` passes.
- The earlier `@intrinsic("type", ..., bits)` failures were fixed.
- Full suite is improved but not clean. The two unresolved failures I focused on are:
  - `tests/required/lib/main.dl`
  - `tests/required/captured_binding_lifetime/main.dl`

## Repros

### 1. `tests/required/lib/main.dl`

Command:

```bash
./build/dynlex tests/required/lib/main.dl -o /tmp/lib.out
```

Current failure:

```text
lib/vector.dl:20:9-59: Error: construct requires compile-time type reference
  note: while inferring intrinsic 'construct' in '@intrinsic("construct", a vector type, vx, vy, vz)' with arguments [i8*, type, a 64 bit float, a 64 bit float, a 64 bit float] -> undeduced lib/vector.dl:20:9-59
```

Relevant DynLex code:

```dl
function [a|] vector type:
    replacement:
        a gpu vector if the build target is gpu, else a cpu vector

function a new vector with x vx, y vy and z vz:
    replacement:
        @intrinsic("construct", a vector type, vx, vy, vz)
```

### 2. `tests/required/captured_binding_lifetime/main.dl`

Command:

```bash
./build/dynlex tests/required/captured_binding_lifetime/main.dl -o /tmp/captured_binding_lifetime.out
```

Current failure:

```text
lib/std.dl:95:9-65: Error: Incompatible operand types 'type' and 'unresolved(?)'
  note: while inferring intrinsic 'select' in '@intrinsic("select", condition, true_value, false_value)' with arguments [i8*, a boolean, type, unresolved(?)] -> undeduced lib/std.dl:95:9-65
  note: while inferring call 'a gpu n x m matrix if the build target is gpu, else a cpu n x m matrix' with arguments [type, a boolean, unresolved(?)] -> undeduced lib/matrix.dl:15:3-73
```

Relevant DynLex code:

```dl
function a n x m matrix:
    replacement:
        a gpu n x m matrix if the build target is gpu, else a cpu n x m matrix

function a new n x m matrix from value:
    replacement:
        @intrinsic("construct", a n x m matrix, value)
```

## Root-Cause Trail
- The broad cleanup is done. There are no leftover `evaluateCompileTimeValue*` / `evaluateCompileTimeInteger` call sites.
- The remaining issue is narrower:
  - a resolved expression reaches a later reader with a deduced `type`
  - but without the stored compile-time value that should accompany that inferred state
- This is why later resolve-only readers still fail:
  - `resolveCompileTimeTypeReference(...)` first prefers a stored compile-time `DataType`
  - `@intrinsic("select", ...)` branch collapsing depends on a stored compile-time boolean
  - both failures look like "typed expression, missing compile-time value"

## Strongest Suspect
- `src/cpp/compiler/type_inference/const_evaluation.inl`
- Specifically the binding-capture clone path:
  - `cloneFrozenBindingSubtree(...)`
  - used from `captureFlexBindingReferencesImpl(...)`

Current behavior of `cloneFrozenBindingSubtree(...)`:
- copies `type`
- copies `selectedPatternDefinition`
- copies `inferredFlexExpansion`
- does **not** copy the stored compile-time value for the source expression

That creates an invalid intermediate state:
- clone looks inferred enough to short-circuit later logic
- but its compile-time value map entry is gone

This matches the observed failure shape better than the earlier workaround attempt to forcibly re-infer flex bindings.

## Likely Fix
- Preserve stored compile-time values when freezing already-resolved binding subtrees.
- The obvious place is `cloneFrozenBindingSubtree(...)`.
- It currently has no `ParseContext` parameter, so it cannot copy the source node's stored constant.
- The likely clean change is:
  - thread `ParseContext *` through the frozen-clone helper
  - copy the source expression's stored compile-time value onto the clone
  - keep the path resolve-only; do not reintroduce evaluator logic

## Secondary Cleanup Opportunity
- After the clone-path fix, re-check whether the added "force infer flexBinding if type/value missing" logic in `function_inference.inl` is still needed.
- If preserving compile-time values at the clone boundary restores the invariant, that workaround can likely be reduced or removed.

## Files Most Relevant
- `src/cpp/compiler/type_inference/const_evaluation.inl`
- `src/cpp/compiler/type_inference/type_resolution.inl`
- `src/cpp/compiler/type_inference/function_inference.inl`
- `src/cpp/compiler/compileTimeValue.cpp`
- `src/cpp/compiler/compileTimeValue.h`
- `src/cpp/compiler/parseContext.cpp`

## Useful Verification Commands

```bash
./scripts/build.sh
./build/dynlex tests/required/lib/main.dl -o /tmp/lib.out
./build/dynlex tests/required/captured_binding_lifetime/main.dl -o /tmp/captured_binding_lifetime.out
./scripts/test.sh
```

Search that should stay clean:

```bash
rg -n "evaluateCompileTimeValueWithKnownState|evaluateCompileTimeValueImpl|evaluateCompileTimeValue\\b|evaluateCompileTimeInteger\\b|compile-time evaluat|compile time evaluat|evaluate .*compile|evaluate.*CompileTime|EvaluateCompileTimeFn|EvaluateCompileTimeValueFn" src/cpp .claude/rules docs
```

Expected result:
- no matches

## Workspace Notes
- There are unrelated dirty user changes in several `tests/required/*/expected_diagnostics.txt` files. Do not revert them.
- There are also generated `tests/required/*.out.o` artifacts from local test runs. Handle carefully if cleaning the tree.
