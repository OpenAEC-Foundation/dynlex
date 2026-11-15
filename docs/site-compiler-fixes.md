# Compiler fixes found during the website migration

The website and shared UI exercised these compiler problems. These changes fix
the compiler behavior rather than special-casing the website's source.

1. **Initialized aggregate reads.** Fold immutable aggregate fields through their
   initialized globals, and publish static-reference globals without invented
   zero initializers.
2. **Associative grouping.** Reject spans with no eligible expression root before
   enumerating their groupings, avoiding exponential invalid work.
3. **Argument inference replay.** Reuse inferred arguments only when binding,
   assignment, execution and result facts match; replay their complete state
   transitions, including replacement bodies and argument references.
4. **Declared operands.** Give declared data names operand priority over nested
   calls, including single-word names.
5. **Declaration captures.** Honor literal suffixes when capturing data names,
   refresh header-derived names to a fixed point, and remove provisional names.
6. **Explicit named operands.** Prefer an explicit operand at an expression root
   when a call's literal prefix collides with it, while preserving enclosing
   literal continuations.
7. **Unsupported numeric types.** Reject unsupported integer, numeric-constraint
   and floating-point widths during type construction instead of aborting later
   in code generation.
8. **Managed values at early returns.** Emit return cleanup using the complete
   owning scope, including values introduced later in the source but retained
   from an earlier loop iteration.
9. **Loop inference state.** Join first and subsequent loop-entry states when
   annotating runtime branches, so later inference cannot overwrite first-pass
   behavior.
10. **Shader interfaces.** Use shared explicit interpolant locations and typed
    interfaces; support signed values, flat interpolation and front-facing input
    consistently across the generated vertex and fragment stages.
11. **Declarative result schemas.** Infer independent declarations against their
    own schema instances instead of mutating a shared template.
12. **Incremental payload lifetime.** Retain immutable cache-file snapshots while
    compiling, so concurrent cache trimming cannot remove an active build's
    inputs.
13. **Named-data LLVM ownership.** Preserve the ownership and snapshot semantics
    of declarative data when emitting globals and captured values.
14. **Main data in replacements.** Use the declaration's storage scope when a
    replacement reads main-scope data, rather than treating the reader's clone
    as the initializer's owner.
15. **Code-generation scope restoration.** Initialize the saved static-reference
    depth before restoring it when leaving a generated function.
16. **Recursive operand grouping.** Keep unresolved recursive expression
    groupings provisional until the return types needed to choose them exist.
17. **Recursive callable references.** Retain an active callable's identity and
    record its unresolved result dependency instead of failing without a
    diagnostic and later aborting pure execution.
18. **Deferred declaration discovery.** Inspect immutable source outlines while
    discovering declaration names, preserving unused cached bodies until a
    semantic consumer demands them.
19. **Folded global dependencies.** Track the execution that initializes named
    global data before folding its value. Keep source-only reads distinct from
    entry-execution reads throughout pure evaluation.
20. **Recursive inference stack usage.** Store immutable execution-effect
    snapshots outside recursive traversal frames, preserving replay semantics
    within the existing stack budget.
21. **Parameter first accesses.** Analyze executable paths before warning that a
    parameter was overwritten before being read. A conditional write does not
    discard its input on the path that skips that write.
22. **Reopened resolver phases.** Retry blocked local-variable lookups whenever
    definition resolution completes, including when cached classification has
    reopened that phase.
23. **C++23 diagnostic ownership.** Use the supported shared-pointer ownership
    count for diagnostic copy-on-write. The removed `unique()` member compiled
    with the native standard library but prevented the Emscripten build.

Focused coverage includes the import-free required fixtures and the incremental
body-demand, declaration-cache, validation-flow and compiler-stack checks under
`scripts/`. The merged native compiler passes all 386 required checks in 107.387
seconds, within the unchanged two-minute budget. The C++23 ownership correction
also passes a direct diagnostic copy-on-write check with both standard libraries.

The required runner overlaps its independent fixture, incremental-compilation and
short auxiliary groups. Previously, the 104-second cache check ran before another
16 seconds of auxiliary checks; all 386 functional checks passed, but their full
run took 120.122 seconds. The group regression checks concurrent execution,
isolated counters, lossless output and failure collection, and worker crash
propagation. The checks and their deadlines are unchanged. Integration retains
its existing single-group memory budget.

Integration passed 520 checks; two checks hit the same Farm document compilation's
60-second process limit. An isolated cold cached compile completed correctly in
61.8 seconds, and the previously qualified compiler completed the same source in
65.5 seconds. The uncached control completed correctly in 22.2 seconds. Both
cached compilers encoded 5,129 bodies into 383 unique stored bodies. These are
measurements of existing cache work, not a newly established compiler defect.
The Farm document fixture now declares a 120-second compilation budget, shared
by the standalone and model runners. Those reruns, the merged browser compiler
build, its smoke checks and the final packaged website checks remain pending.
