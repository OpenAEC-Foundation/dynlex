# Next Agent Handoff (2026-04-03)

## User Directives (Non-Negotiable)
- Work on the existing tree only.
- No subtree cloning workarounds to dodge corruption/state issues.
- No internal validation/fallback hacks to mask stage inconsistencies.
- Use `flex` terminology; do not introduce `macro` wording.
- If blocked, report blocker/root cause directly before broad edits.

## What Was Confirmed
- Initial failure in `lib/std.dl` select path:
  - `Incompatible operand types 'type' and 'unresolved(?)'`
- Root cause for that specific failure:
  - function-flex expansion was only retained in committed inference (`!trial`).
  - trial inference could not follow the call expansion in compile-time binding resolution.

## Current State After Focused Patch
The tree currently contains a focused trial-expansion propagation patch (not the rejected broad evaluator):
- [`src/cpp/compiler/type_inference/function_inference.inl`](src/cpp/compiler/type_inference/function_inference.inl):1903-1906
- [`src/cpp/compiler/type_inference/type_resolution.inl`](src/cpp/compiler/type_inference/type_resolution.inl):1369-1370,1500-1516,1591-1621,1623-1649
- [`src/cpp/compiler/type_inference/section_inference.inl`](src/cpp/compiler/type_inference/section_inference.inl):409-411
- [`src/cpp/compiler/type_inference/operand_reordering.inl`](src/cpp/compiler/type_inference/operand_reordering.inl):429-430

This removed the old `select` type-mismatch symptom, but did **not** fix the deeper blocker.

## Remaining Blocker (Current Failing Error)
Now failing on construct type-reference compile-time requirement:

```text
Error: Intrinsic 'construct' argument 1 must be compile-time known
```

### Repro 1 (original chain)
```bash
./build/dynlex /tmp/repro_matrix_select.dl -o /tmp/repro_matrix_select.out
```

`/tmp/repro_matrix_select.dl`:
```dl
import lib/matrix.dl

set transform to a new 4 x 4 matrix from [
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
]
```

### Repro 2 (select bypassed; still fails)
```bash
./build/dynlex /tmp/repro_cpu_matrix_construct.dl -o /tmp/repro_cpu_matrix_construct.out
```

`/tmp/repro_cpu_matrix_construct.dl`:
```dl
import lib/matrix.dl

set transform to @intrinsic("construct", a cpu 4 x 4 matrix, [
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
])
```

### Repro 3 (stdlib-independent minimal class case)
```bash
./build/dynlex /tmp/repro_class_n_mul_m.dl -o /tmp/repro_class_n_mul_m.out
```

`/tmp/repro_class_n_mul_m.dl`:
```dl
import lib/std.dl

class:
    patterns:
        box n x m
    members:
        values as an array of n * m items

set b to @intrinsic("construct", box 4 x 4, [
    1,2,3,4,
    5,6,7,8,
    9,10,11,12,
    13,14,15,16
])
```

This proves the remaining issue is not specifically `select`.

## What Was Already Checked
- `target is` compile-time argument is seen as string in trial path (breakpoint at `function_inference.inl:1508` hit; failure branch at `:1505` not hit).
- `findPatternParameterDefinition(...)` returns non-null for class parameters (`n`, `m`) and bindings are collected.
- Failure still occurs at construct compile-time type-reference check:
  - `function_inference.inl:1632` (`resolveCompileTimeTypeReference(...)`).

## Most Likely Root-Cause Area (Still Open)
Class type-reference resolution for expressions like `n * m` (field declarations) is not yielding compile-time known values in this path, even when call-site literals exist.

High-probability files/lines:
- [`src/cpp/compiler/type_inference/function_inference.inl`](src/cpp/compiler/type_inference/function_inference.inl):1623-1636
- [`src/cpp/compiler/type_inference/type_resolution.inl`](src/cpp/compiler/type_inference/type_resolution.inl):899-934 (instantiateBoundClassType)
- [`src/cpp/compiler/type_inference/type_resolution.inl`](src/cpp/compiler/type_inference/type_resolution.inl):1623-1649 (resolveStoredCompileTimeValue)
- [`src/cpp/compiler/compileTimeValue.h`](src/cpp/compiler/compileTimeValue.h):40-63 (generic known-state loop)

## Important Constraint For Continuation
A broad recursive compile-time evaluator was attempted and rejected by user as over-scoped. Do **not** reintroduce that direction. Keep next fix minimal and local to the proven failing path.

## Suggested Next Steps (Minimal)
1. Instrument with `gdb` at `function_inference.inl:1632` and inside `instantiateBoundClassType(...)` to inspect exactly where `n * m` loses compile-time integer resolution.
2. Patch only the narrow resolution path needed for class field type expressions under construct/type-reference inference.
3. Re-run all three repros above plus:
   - `./scripts/build.sh`
   - `./build/dynlex tests/required/flex_binding_frames/main.dl -o /tmp/flex_binding_frames.out && /tmp/flex_binding_frames.out`

## Notes For The Next Agent
- The user explicitly requested directness and no gold-plating.
- Report blockers early; do not ship speculative rewrites.
