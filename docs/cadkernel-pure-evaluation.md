# Bounded compile-time evaluation

Pure execution of a recursive function with changing arguments exhausted the host
stack instead of returning a diagnostic. The active-call check only recognized
the same section with identical argument values. Its local unknown result, and
the separate 100,000-iteration loop fallback, could also let an enclosing function
continue and publish a constant calculated from incomplete execution.

## Shared execution state

Each root evaluation has a shared budget of 1,000,000 expression/section entries
and 256 simultaneous entries. Nested calls, expression conversions, flex bodies
and loop iterations use the same state. A scoped entry restores nesting depth on
return and during unwinding. Resource exhaustion unwinds to the root pure-call
evaluation, which reports the exhausted limit through the normal inference
diagnostic path.

The narrow repeated-argument guard and local loop fallback are removed. Failed
evaluation cannot resume its caller or cache its partial result. Completed
nested values still follow the existing trial journal. Active body and flex
scopes retain their existing scoped restoration. Compiler invariant failures
remain distinct from a program exceeding the evaluation budget.

The pattern-call inference path stops after a failed evaluation instead of
continuing to publish inferred expression metadata.

## Reproductions and regression checks

The tests use intrinsic wrappers without importing the standard library.

| Required fixture | Behavior before repair | Required behavior |
| --- | --- | --- |
| `pure_execution_recursion_limit` | Windows stack overflow, exit 0xC00000FD | Nesting-limit diagnostic |
| `pure_execution_mutual_recursion_limit` | Windows stack overflow, exit 0xC00000FD | Nesting-limit diagnostic |
| `pure_execution_call_cycle` | Compilation accepted a constant after a nonterminating call | Nesting-limit diagnostic |
| `pure_execution_step_limit` | Compilation accepted the partial counter after stopping its loop | Step-limit diagnostic |
| `pure_execution_finite_recursion` | Successful compilation | Array length resolves to seven; native exit zero |

All five cases were checked at O0 and O2 before and after the repair.
The four failures now exit 1, emit the exact expected diagnostics and produce
no executable. The valid case compiles and runs. The complete focused selection
of 33 existing and new pure/recursive fixtures passes all **66 mode combinations**,
including compile-time type sources, mutual recursion, flex return wrappers,
ownerless functions, rejected nonconvergence and inference-trial cases.

Independent review found no actionable defect. Nine additional probes passed
all 18 O0/O2 outcomes, checking a 60,000-iteration calculation, shared budgets
across distinct nested calls, completed cache hits, independent root budgets,
trial rollback and finite recursion both below and above the nesting limit.
The limits count evaluator entries, not function calls; sufficiently deep finite
computations also receive the documented limit diagnostic.

The complete compiler suite still needs to pass after integration. These checks
do not establish complete CAD-port coverage.
