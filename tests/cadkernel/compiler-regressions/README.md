# Compiler regression: recursive actions with implicit return

Compiler revision: `332dac1385edcbe6458386a5119b4cc62d010581`.

`recursive_void.dl` is a standalone eleven-line reproducer with no imports.
It terminates after three recursive steps and discards each argument. It has
no return value, so its inferred result must be `nothing`.

Reproduce from the repository root:

```sh
build/dynlex tests/cadkernel/compiler-regressions/recursive_void.dl -O0 -o build/recursive-void.out
```

Use `build/dynlex.exe` on Windows. The unmodified base compiler exits 1:

```text
Recursive type inference did not converge for function 'visit depth'
```

Adding an explicit `@intrinsic("return")` after the discard makes this
isolated control compile successfully. The production sampling algorithm
has not been changed to depend on this diagnostic control.

## Cause

The recursive call sees the caller's undeduced result and marks the
instantiation for reinference. The fixed-point loop in
`src/cpp/compiler/type_inference/function_inference.inl` detects that
the next pass makes no progress and reports nonconvergence.

Implicit `Void` assignment lives after that loop in
`function_inference_pattern_calls.inl`, guarded by
`!inst.inferring && !inst.needsReinfer`. The equivalent ownerless path
in `section_instantiation_inference.inl` has the same ordering.
Neither can establish the missing result before convergence is tested.

## Repair and validation

Establish the implicit result of a completed function-body inference pass
before the recursive convergence decision. Track whether that pass observed
an actual return intrinsic, including returns whose operand is still
unresolved; those must not be mistaken for functions without returns.
The evidence must follow instantiated pattern/intrinsic behavior and trial
rollback, rather than searching source words such as "return".

Cover direct and mutual recursive actions, natural-language return wrappers,
exposed/function-reference inference, and a nested value function whose
result must not become the caller's result. Existing nonconvergent
value-recursion diagnostics must continue to fail as specified. Run the
full required suite after rebuilding, then the CAD angular-sampling fixture.

The repair records an actual return intrinsic on its owning instantiation
before inferring the return operand. The trial journal restores this state
when a candidate grouping is rejected. Each full body pass resets this
observation. A successful, type-valid pass with no return establishes
`Void` before the convergence snapshot; an early failed pass or an
unresolved return cannot establish it. Ordinary calls and exposed/function
references use the same path, replacing both late default assignments.

Five new required fixtures cover direct and mutual actions, nested value
callees, return wrappers, exposed functions and function references.
Together with eight existing recursion fixtures and the adaptive-sampling
fixture, all 28 O0/O2 combinations pass. Invalid value recursion still
produces its exact expected diagnostic. Independent review found no defect and
checked four additional reproductions at both optimization levels. The first
complete suite run reported 537 passes and six failures: four arena fixtures
under active revision and two Windows test-harness portability failures.
Those failures require resolution and another complete run.

The native `lib/cadkernel/tessellation.dl` fixture now runs successfully,
including minimum subdivision, a curved sample and the 65,537-point depth
limit case. Its binary64 fixed-array contract now rejects wrong precision and
dimension, and its callback contract rejects non-3D points and tangents.
See the [sampling validation](../tessellation/README.md).

A separate [pure-evaluation correction](../../../docs/cadkernel-pure-evaluation.md)
handles host stack overflow and partial constants after an exhausted computation.
