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
24. **Inference-product lifetimes.** Retain immutable snapshots of observed and
    newly produced inference summaries, bodies and classes. Recording only that
    a path existed allowed another build's cache eviction to remove it before a
    later read. A 12-line import-free program reproduces the failure when GDB
    evicts its summary at the read boundary; after the fix it compiles and prints
    the expected value. Snapshot tests cover eviction, replacement and ownership
    of newly produced bytes.

Focused coverage includes the import-free required fixtures and the incremental
body-demand, declaration-cache, validation-flow and compiler-stack checks under
`scripts/`. The merged native compiler passes all 386 required checks in 108.093
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
The Farm document fixture declares a 120-second compilation budget, shared by
the standalone and model runners. The next full run passed 519 integration
checks, including both previously failing ownership checks. The saved-state
model then exceeded its separate default 60-second guard. Isolated optimized
compiles completed correctly in 81.3 seconds with the current compiler and
61.6 seconds with the previously qualified compiler; both matched all 145
recorded save scenarios. The unchanged model now resides beside the Farm
document fixture and uses its existing budget. Its rerun exposed the separate
inference-product lifetime bug described above.

The latest cache fix passes the native build, snapshot tests, deterministic
eviction reproducer, all 386 required checks and all 520 integration checks.
The browser build also passes: the native-only cache files are excluded from
that target, so its validated Wasm binary is unchanged. Shipped-compiler
regressions, shader-input checks and the startup-order fixture pass. Packaged
website and shared-UI qualification also pass: 20 selected native UI checks,
shared source/control checks through both compiler frontends, and the general
site, River, character inspection and Farm flows across WebGPU and both WebGL
capability configurations. Browser checks cover syntax colors, hover, navigation,
quick fixes, undo, themes, downloads, gameplay, persistence and deadline recovery.
The tested candidate is served by the local preview.


## Website extraction: embedded-library deletion

The browser build tracked existing embedded files but did not retain directory
membership as an input. Deleting a file removed a dependency edge without making
Ninja relink the compiler, leaving the deleted library in its virtual filesystem.
A generated membership manifest now participates in link dependencies. A small
CMake/Ninja regression verifies deletion, edits and additions; real browser
negative-import checks verify Farm and River are absent from the standard library.
The application supplies that vocabulary as ordinary project sources instead.

During extraction, all 386 required checks passed functionally in 133.782 seconds;
this run exceeded the unchanged 120-second gate. The independent incremental
compilation group was the last group running. This timing result is not counted
as a passing gate.

## Farm performance: browser allocation lifetimes

The runtime allocator used a sorted array for free ranges and repeatedly deleted
and reinserted allocation keys in a JavaScript Map. Retained UI data interleaved
with temporary world snapshots made range insertion/search quadratic; Map churn
added lookup costs even when the same addresses were repeatedly reused.
An address-ordered AVL tree now retains first-fit semantics with logarithmic
search/coalescing. Lazily allocated typed metadata pages record requested lengths
without Map tombstones. Alignment, zeroing, realloc copying and invalid-pointer
errors retain their existing contracts.

A 15,000-operation independent interval model verifies splitting, first fit and
two-sided coalescing. Public-allocator checks cover zero-byte allocations, stale
and interior pointers, shrink/move and metadata-page boundaries. In one local
Farm benchmark, snapshot creation with 2,000 retained allocation holes fell from
96.37 ms to 6.45 ms median; the unfragmented case fell from 5.48 ms to 3.01 ms.
These are snapshot timings, not browser frame-rate measurements.

The required gate passed all 387 checks in 84.020 seconds. The integration run
passed all 486 checks with source files; its 26 other entries were empty local
directories left by the extraction, and reported missing main.dl. Those empty
untracked directories have been removed; the extracted fixtures live in the
website repository.
