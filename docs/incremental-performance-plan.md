# Incremental compiler performance plan

Owner: this compiler task. Established 13 November 2026.

## Objective and constraints

For edits to any `.dl` source or configuration, propagate changed inputs through
the relevant compiler units. Preserve global pattern availability. Retain positive
and negative lookups, implicit-parameter changes, types, compile-time values,
diagnostics and generated artifacts. Stop propagation when an observable result
is equivalent. Wording-only configuration changes must not regenerate executable
code. Keep the required suite uncached and within 120 seconds after the C++ build.

Optimize the complete workload: compiler startup, input checks, frontend work,
semantic analysis, persistence, LLVM, object generation and linking. Moving work
between stages is not an improvement by itself. Track cache-disabled builds,
empty-cache default builds and warm edits separately. Do not flush the operating
system's page cache on this shared PC.

This is an opportunity catalogue, not authorization to implement every idea.
Benchmarks decide which candidates survive. Necessary correctness fixes remain
required even if they expose previously hidden costs; identify those costs and
do not present them as performance wins.

## Iteration contract

1. Select one named goal and write its hypothesis, affected cases, acceptance
   criteria, baseline identity and start time in the ledger before changing code.
2. Use a 45-minute wall-clock iteration, including builds and verification.
   Complete one measured goal: accept an optimization or reject its hypothesis.
3. If blocked, record the blocker and preserve the unfinished work on an isolated
   branch. Select an independent goal instead of extending the same investigation
   indefinitely. A missing tool, unavailable fixture or unmeasurable result is a
   reason to park early.
4. Allow one additional 45-minute iteration on a goal only after recording concrete
   progress: a reduced reproducer, a verified cause, or a measured reduction with
   an implemented candidate. After that extension, accept, reject or park it.
5. Add the minimal correctness or work-count regression test before the fix.
   Tests exercise language behavior and compiler products, not a duplicate of the
   implementation. Required fixtures use no application-library imports.
6. Compare immutable baseline and candidate executables on identical application
   sources, options and edits. Alternate execution order. Report CPU and elapsed
   time separately, plus stage work, cache size and peak memory where measured.
7. Keep an optimization only when its intended end-to-end benefit is repeatable
   beyond measured noise, correctness matches a clean build, and unrelated cases
   show no unexplained repeatable regression. Set a goal-specific minimum gain
   before implementation. More code requires a larger or broader demonstrated gain.
8. Rejected experiments are removed from the proposed patch, with their evidence
   retained. Earlier authorized work is not silently reverted. Parked experiments
   are not merged or described as complete.
9. Run the full required suite for a compiler change. Run integration checks when
   the affected behavior is exercised there. Record the exact tested revision.
   Do not weaken the 120-second limit, enable caching for required fixtures, or
   change expectations to accommodate a compiler regression.
10. End the iteration with the decision, before/after measurements, correctness
    results, unresolved limits and the next selected goal. Update this document.

Use one heavy compiler build, benchmark or suite at a time on this shared PC.
C++ builds use one job until memory measurements justify a different choice.
Do not mutate compiler sources while a build, suite or comparison reads them.
Record host contention and memory pressure; elapsed time alone does not establish
a compiler regression. Failed or incomplete builds are not valid timing samples.

## Measurement and acceptance

The primary application must be real and normally take roughly 1–10 seconds to
compile. Freeze its complete source/configuration inputs and record their hashes.
The retained goal-start LEXI8 Breakout application provides historical continuity;
current dynlex-ui applications provide a separate modern-library check. Never
compare different application versions as if only the compiler changed.

The fixed comparison matrix is:

| Case | Required observation |
| --- | --- |
| Unchanged invocation | No semantic work, LLVM processing or linking. |
| Comment in an imported file | Correct source locations; no semantic recompilation. |
| Diagnostic wording; config creation/removal/shadowing | Current diagnostics; executable untouched. |
| Small active function body edit | Only affected specializations and downstream outputs reconsidered. |
| Unused function edit | Healthy active program results remain reusable. |
| Top-level statement edit | Correct globals, diagnostics and executable behavior. |
| Added/removed/generalized/specialized pattern | Relevant positive and failed lookups reconsidered. |
| Last parameter use removed | Classification change reaches all affected references and later stages. |
| Class layout, callback or global type/value edit | Relevant type, value, lifecycle and native dependencies update. |
| Repair error; edit elsewhere while an error remains | Healthy work survives; clean-build diagnostic demand order matches. |
| Cache-disabled build | Track cold CPU, elapsed time and memory independently. |
| Default build with an empty cache | Report capturing reusable products and preparing native units separately from warm edits. |

Use at least seven paired samples for ordinary cases and fifteen for fast paths.
Three pairs can screen an expensive case, but uncertain results require more data,
not an acceptance claim. Report medians, dispersion and paired ratios; do not
infer improvement from a single favorable sample. A benchmark harness must not
launch the graphical application, disturb the user's display, or include the C++
compiler build in DynLex compilation time.

Clean-build parity includes diagnostics and executable output. For noninteractive
applications, compare runtime output. For graphical programs, compare normalized
LLVM function/global/declaration maps or use existing rendered-output integration
checks. Cache counters must explain the work avoided. Synthetic cases are reserved
for focused correctness and scaling evidence, not the main speedup claim.

## Stage inventory and candidate goals

Status describes the implementation at the start of this plan. Each row is an
independent optimization hypothesis or a deliberately bounded part of a larger
stage. Inspect the current bottleneck before selecting a row.

| ID | Area and current limitation | Candidate outcome and decision evidence |
| --- | --- | --- |
| P00 | Benchmark and validation baseline; latest required run timed out | Close inherited work, obtain an identified passing baseline, and establish reproducible paired comparisons. Do not infer a CPU hog from incomplete child CPU accounting. |
| P01 | Cache maintenance scans and sorts all artifacts on every build | First evaluate one metadata query per artifact instead of two. Require at least 10% median improvement in fast paths, supported by paired samples and syscall/work counts. Preserve eviction order and corruption/I/O errors. Evaluate unnecessary sorting separately if still material. |
| P02 | Source import scanning, character validation and transformations run again for unchanged files | Cache immutable file-level source products with content, syntax and import-context inputs. Reuse unchanged products without rescanning their lines. Benchmark the complete build, including loading the products. |
| P03 | Section objects are rebuilt across the imported program | Persist reusable section fragments and stable source identities; materialize only required units. Preserve nested imports, indentation, shorthand expansion, locality and source mapping. Group with its consumer if serialization alone adds overhead. |
| P04 | Import/configuration dependency discovery is repeated broadly | Reuse the import graph and configuration discovery results while checking actual source changes, missing candidates and shadowing. Keep custom syntax and file identity correct; do not use timestamps as the correctness proof. |
| P05 | Pattern trees are reconstructed, including unchanged declarations | Retain indexed declaration products and apply additions/removals/interface changes. Keep global visibility and overload behavior. Measure tree construction and cold-build costs separately. |
| P06 | Classification and lexical-environment setup still visit many unchanged units | Use persistent scope/name queries and stop at unchanged exported signatures. Cover parameter promotion/retraction, nested definitions and negative bindings. Measure setup, candidate checks and references visited. |
| P07 | Frontend diagnostics disable body reuse for an entire source file | Retain diagnostics at their actual producing unit and demand boundary. Replay warnings, notes and fixes once, in clean-build order, without forcing unrelated bodies in the file through resolution. |
| P08 | Matcher fingerprints, candidate search, specificity and precedence remain expensive | Index only relevant changed paths; share immutable lookup inputs; avoid equivalent rematches. Keep only changes with real edited-build or cold-build gains. No spelling-specific shortcuts. |
| P09 | Pattern-conflict checking rescans definitions | Cache conflict queries and their declaration/type-domain inputs. Reconsider only affected candidate pairs, including newly introduced overlaps. |
| P10 | Inference restores complete bodies but eligibility and some validation are broad | Make specialization, conversion, class, lifecycle, global and value checks demand-driven. Stop propagation at equivalent interfaces/results. Reduce repeated type/storage/source identity construction. |
| P11 | A changed function or entry computation is generally processed as a whole | Evaluate statement/block/CFG checkpoints for large hot inference units and top-level execution. Implement only if profiles show that finer units repay dependency and restoration overhead. |
| P12 | Final variable and return checks include whole-program traversals | Retain validation products with their precise type, control-flow and source dependencies. Preserve early-error behavior and diagnostic publication. |
| P13 | Early frontend failures lose some completed stage products | Retain partial successful/failed/pending units at each real stage boundary. Test intervening errors, repaired providers and error ordering. Avoid quadratic readiness/frontier metadata. |
| P14 | Semantic projections and indexes still load/copy/encode broad data | Persist changed units, use compact stable identities and load records on demand. Measure decode, copy, hashing, serialization, bytes and allocations. Avoid a new general storage framework without demonstrated need. |
| P15 | Cached LLVM modules are parsed and serialized as a whole | Evaluate lazy or per-unit IR artifacts and persisted dependency summaries. Preserve symbol identity, mutable globals, callbacks, debug metadata and module-level requirements. |
| P16 | Optimization/partition preparation can process unchanged symbols and repeat setup on an empty cache | Reuse native closures and optimization inputs without unnecessary cloning/indexing. Measure first-default-build overhead and evaluate shared optimizer setup or batching. Preserve cross-unit inlining, link-once definitions and reachability. |
| P17 | Necessary executable changes still trigger a conventional full link | First eliminate redundant input preparation. Evaluate incremental-link support only if linking is a material measured cost and supported platforms can preserve correctness. |
| P18 | Debug and execution-trace options disable major reuse paths | Separate semantic facts and machine-code inputs from source-location/trace metadata. Recompute metadata that actually changed while retaining eligible semantic work. |
| P19 | LSP creates fresh full compilation contexts | Connect the shared incremental frontend to in-memory document versions, imports and target profiles. Avoid a second semantic implementation. Measure edit-to-diagnostic latency and stale-result cancellation. |
| P20 | LLVM/object/SPIR-V/WebAssembly and other output modes bypass the native incremental pipeline | Share target-independent frontend products, then add target-specific artifact keys where benchmarks justify them. Do not force native linker assumptions into other modes. |
| P21 | Cold builds and small builds pay avoidable bookkeeping/allocation costs | Profile startup, source observations, normalization, hashing, arena allocation and cache recording. Avoid generating persistent-only data when there is no consumer. Keep cold and incremental gates separate. |
| P22 | Cache access/retention and eviction can grow with unrelated programs | After P01 measurements, evaluate reference-aware retention or bounded accounting/indexing. Require cross-process, eviction, interrupted-write and platform tests. Do not add a storage engine speculatively. |
| P23 | Possible immutable compact/mapped products or an in-memory compilation service | Evaluate only if materialization/process startup remains dominant after unit reuse. Include memory, invalidation, lifecycle and operational costs in the end-to-end comparison. No new user configuration for ordinary incremental builds. |
| P24 | Possible parallelism after unnecessary serial work is removed | Measure wall time, summed CPU and peak memory. Keep bounded scheduling only if the tradeoff is demonstrated; do not saturate this shared PC or conceal higher CPU cost with lower elapsed time. |

P02–P07 establish missing frontend reuse. They have priority over speculative
low-level optimization once P00 gives reliable measurements. P01 is a bounded
candidate for the already observed fast-path regression; it does not substitute
for completing those frontend milestones. Later candidates are selected by the
remaining measured costs, not by table order alone.

## Completion gates

- Every compiler stage has documented input/output units and invalidation rules.
- Unaffected source/section, matching, inference, validation and backend units are
  skipped, with counters proving it on edits to ordinary files and configuration.
- Added overloads, failed lookups and body-driven signature changes cascade fully.
- Healthy partial work survives errors without advancing incomplete stage state.
- Native and tooling/output-mode coverage is explicit; no unsupported path is
  described as incremental.
- Real-program small edits are measurably faster without unexplained common-case
  or cold-build regressions. Keep the historical day-2 comparison visible.
- The uncached required suite passes within 120 seconds, relevant integration
  checks pass, and accepted changes are committed and reviewable.

## Iteration ledger

| Iteration | Goal | State | Evidence / next action |
| --- | --- | --- | --- |
| I000 | P00: establish the plan and a validated comparison baseline | Complete | 14:18–15:01 CET, no extension. Compiler `4ab4d5c106251641513c7c345d85a69ded27daee02477cc282c9dc6306148ac0`: 328 required checks passed uncached in 104.8 seconds. Fixed a verified eviction-fixture assumption without weakening hit/parity assertions. Added and calibrated the paired real-application harness. |
| I001 | P01: measure and evaluate cache-maintenance overhead | Queued | First new optimization after a usable baseline. Compare fast paths and cache-size scaling; keep only a measured gain. |
| I002 | P02/P03: select a complete file/section reuse experiment | Queued | Record current source-stage work and choose a bounded producer/consumer change with a measurable end-to-end benefit. |

Carry-in evidence is under `.cache/incremental-evidence/global-callers-20261113/`.
The pre-linear snapshot `01eb1c6a` passed 328 required checks in 104.9 seconds and
464 integration checks. The inherited modifications subsequently passed the full
required gate in I000. Their isolated scaling comparison reduced 128-query cache
storage from 1,528,001 to 308,801 bytes and dependency roots from 16,384 to 128,
with matching diagnostics. These are focused scaling results, not a claim about
the five real-program graph cases.

Structured evidence is under `.cache/incremental-evidence/structured-20261113/`.
The first controlled run finished with 327 passes and one failure. Inspection
proved that arbitrary half-segment eviction removed all three matcher results
while leaving five reusable metadata segments. The corrected fixture retains a
lookup-containing segment and evicts the others; hit, missing-segment, corruption
and clean-build parity assertions remain.

The initial identical-compiler control exposed roughly 5% variation with different
executable locations. The harness now stages executables and caches into symmetric
`a`/`b` paths, alternates order and checks source hashes. A repeated 15-pair control
measured a CPU ratio of 0.991, with a 95% bootstrap interval of 0.947–1.036,
consistent with no change. P01's minimum gain was raised from 5% to 10% before
implementation so a small apparent gain does not merely reproduce that noise.

Reproduce a comparison with prebuilt compilers:

```bash
python3 scripts/benchmark_incremental_comparison.py \
  --baseline /absolute/path/to/baseline/dynlex \
  --candidate /absolute/path/to/candidate/dynlex \
  --require-identical-executable \
  --output .cache/incremental-evidence/comparison.json
```

The tool stages the pinned real application, compiles without launching its GUI,
and records elapsed and POSIX process-tree CPU time. Other platforms explicitly
report CPU accounting as unavailable. Staged compiler executables must have their
runtime dependencies available from configured locations. Private application
sources, caches and executables are removed afterward; reports are retained.
