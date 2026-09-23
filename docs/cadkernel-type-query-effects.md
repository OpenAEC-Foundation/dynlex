# Signature domains and unevaluated type queries

The topology layer requires concrete member layouts for managed planar curves
and typed adjacency lists. It obtains these layouts through `type of` factory
expressions. Those expressions are inferred, but must not allocate, retain,
release, mutate variables or perform I/O while answering the type query.

## Provisional overload domains

Resolving one signature can require ordinary function inference before every
other signature is ready. An unrelated typed overload must not turn a known
integer operation into a signature cycle. Provisional resolution therefore
checks all available argument domains. A concrete mismatch excludes that
candidate; a potentially matching incomplete candidate still defers the call.

Fixed constraint probes use the normal inference engine and recursion guards.
They cache only necessary structural domains, with class-layout indices removed
recursively. These domains can exclude a candidate but never select it. Probes
inside a class-layout transaction restore its request cache, appended layouts
and failure state before returning. Ordinary committed inference remains the
only authority for completed signatures and overload selection.

Import-free controls cover declaration order, disjoint later parameters,
numeric and pointer conversions, generated properties, and a genuinely matching
signature cycle that must still fail.

## Execution effects

The previous bottom-up argument walk propagated effects before knowing whether
a replacement would execute the argument. As a result, a constraint such as
`the type of witness` could be rejected because `witness` called an external
function, although code generation already omitted that call.

Instantiation execution effects are now separate from representation and
inference progress. A selected replacement restores preliminary argument
effects before interpreting its body. Binding reuse requires unchanged and
identical execution state; otherwise inference replays the binding's effects.
Type queries restore their operand's execution effects, and pure evaluation
uses the stored type-query result without executing the operand.

`type_query_effects` checks direct and wrapped queries, typed parameters, class
members and mutation of a local variable. Its result is `7777` at both O0 and
O2, without the witness's `X` output. `type_query_evaluated_effect` and the
existing `impure_type_constraint` keep actual I/O in a type-producing body
invalid. The pointer-conversion control produces `79`; dereferencing inside
its unevaluated type witness does not make the constraint impure.

## Borrowed binding transactions

Replaying a binding preserves its committed operand grouping, including every
child node. Otherwise a nested loop may regroup an expression outside the
snapshot owned by its current trial. The random-generator controls exercise
this boundary alongside ordinary grouping and replacement tests.

The same binding is caller-owned: it is outside the current trial's expression
snapshot. Rejected inference had rolled back the constant-value map while
leaving a new replacement expansion selected on that binding. In the import-free
`unevaluated_extent_loop_binding` reproducer, the array length remained `2` on
the caller expression, while its selected `type extent` expansion had no value.
Code generation then correctly rejected the missing runtime loop bound.

The existing trial journal now records the first write to each borrowed
expression and its reachable argument/expansion nodes. Nested trial promotion
retains the outer first-write state. Rollback restores those expressions before
discarding provisional instantiations that can own their bodies. This restores
selection metadata and constant values together; code generation does not
recompute or substitute a missing value.

## Verification

Compiler SHA-256
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`
passes all 42 focused O0/O2 checks, including the import-free loop reproducer
(`XX7`), all 38 recursive-inference controls, and Dynedra Lab's model, state,
hidden-window graphics and interactive builds at both optimization levels.

On that compiler, the native B-rep topology fixtures pass at O0/O2. All 210
reference graphs and 19,372 trace lines agree with the pinned Rust source,
including all ten flaw variants and twelve exact messages. O0/O2 values agree
exactly. Evidence is in `build/brep-native-differential.log` and
`build/cadkernel-brep-topology-checks/run-jkiiu9qe/results.json`.
The full required suite subsequently passed **633 checks, zero failures and
zero skips**, in 953.203 seconds on the same compiler. Evidence:
`build/required-area2-budget.log`. This includes the stdio repair, bounded
integration-test budgets, split B-rep fixtures, transformations and area/centroid
fixtures. The two source-join wrappers were registered after the run started
and passed all four O0/O2 checks separately (`build/source-join3-required.log`).
The 633 total does not include those four later checks.
