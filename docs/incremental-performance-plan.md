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
| I001 | P01: one filesystem metadata query per cache artifact | Complete: keep | 15:02–15:54 CET; one extension used, finished before its 16:32 deadline. Corrected baseline `b9f173e1...`, candidate `5c311f81...`. Metadata calls fell from 516 to 258 for 258 cached objects. Fifteen paired samples per fast path clear the 10% median CPU threshold with intervals excluding no improvement. All executable/diagnostic comparisons passed; 328 required checks passed uncached in 118.1 seconds and 464 integration checks in 928.1 seconds. Code-edit/cold timing gains remain unproven. |
| I002 | P02a: reuse file-preparation products at the importer | Parked; not accepted | 15:56–17:26 CET, one extension exhausted. One edited file is prepared and 44 reused; real unused-body import time fell 16.94→12.23 ms. Native/diagnostic parity and focused source/config/error-order checks pass. No repeatable whole-build code-edit improvement was established. Required suite timed out at 120.17 s; current integration validation is missing. Preserve the experiment isolated from master. |
| I003 | P01/P22: reduce cache-finalization work | Accepted as part of the I006 snapshot | 17:26–18:49 CET, one extension. Isolated compiler `ef722bb9...` based on `14fa0b78` reduces fast-path paired CPU by 15.6–19.6%; all eight executable/diagnostic comparisons pass and 464 integration checks pass in 971.2 s. Initially parked because its required gate returned 124 at 120.12 s. The later accepted snapshot includes this work and passes the uncached gate in I006; the historical timeout remains evidence, not a passing run. |
| I004 | P03/P06: retain unchanged body syntax and materialize only on relevant demand | Parked; not accepted | 18:52–20:04 CET, one extension used. The minimal work test proves unchanged bodies are still assembled despite matcher reuse. Added a demand-capable source slot and ownership tests; its isolated contract probe passes and the full compiler rebuild is finishing. No persisted body provider is installed, no syntax units are skipped, and no performance benefit is claimed. Correct deferral also requires classification query metadata, resolved-match/binding products, and avoiding eager expansion/source-index consumers. Keep this experiment isolated; do not merge scaffolding as an optimization. |
| I005 | P06/P10: exclude only diagnostic-owning units from matcher reuse | Complete: keep, with a limited performance claim | 20:04–20:46 CET, no extension. Candidate `37e3681b` / compiler `b6774332...`. Seven independent first-unused-edit pairs improve CPU 1.3090→0.9451 s, paired ratio 0.7213 [0.6650, 0.7544]. Repeated edits improve only 1–2%, below the initial steady-state expectation. All eight executable/diagnostic comparisons pass; 328 required checks pass uncached in 99.254 s and 464 integration checks in 821.954 s. Three-line recovery repro verifies retained body products; successful snapshots still prune removed units. Accept this optimization for the verified first-edit workload, without claiming a 28% steady-state gain. |
| I006 | P00: prepare and validate the accepted snapshot without the parked body-syntax prototype | Complete: merged and verified | 20:46–22:07 CET, one extension used, before the 22:16 deadline. Exact source `13a3f819`, compiler `4a9cde56...`, excludes I004's unconsumed source-slot prototype and dormant work test. It passes 328 required checks uncached in 112.912 s, 464 integration checks in 890.112 s, eight-case artifact/diagnostic parity, and seven independent first-edit pairs (CPU 1.3007→0.9536 s; paired ratio 0.7306 [0.7091, 0.7885]). Root master `74f703ea`, rebuilt compiler `dcb16906...`, passes all 328 required checks in 98.030 s (99.283 s including the recording driver). The merged worktree is removed; an actual archived-compiler benchmark replay passes after removal and cleans its temporary data path. No further optimization is added to I006. |

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

I001's first comparison stopped on a differing executable hash. Inspection found
identical generated LLVM IR and all 258 native object artifacts byte-identical;
the dynamic-library lists differed only in the order of `freetype` and `m`.
An unordered set leaked discovery order into native linking. Both native paths
now collect library names in a shared deterministic order, with an isolated C++
regression check. The corrected baseline is compiler
`b9f173e10a73cba0e89b2fa7bf505b256989e68caec8707be2fd95fc6223ab72`.
The acceptance comparison applies this correction to both sides and still
requires identical executables. Earlier interrupted comparisons are screening
evidence, not a completed performance result.

That screening measured approximately 20 ms importing/transforming sources,
25 ms constructing sections, and 530 ms resolving patterns for the small active
body edit. P02/P03 address a missing incremental boundary, while P05–P08 target
the larger remaining frontend cost. Source transformations currently operate on
the merged stream: bracket continuation and indentation can cross import
boundaries. Cached fragments must retain those context inputs rather than assume
every source file parses independently.

### I001 decision and limits

Keep the single-query metadata implementation. The corrected comparison uses the
same deterministic library-order fix in both executables, so the measured change
is cache maintenance. The final trace verifies two metadata syscalls per cached
object before and one after, with identical native output. Traced timings are
excluded from the performance samples.

| Warm case | Baseline median CPU | Candidate median CPU | Median paired ratio; 95% interval |
| --- | ---: | ---: | --- |
| Unchanged | 47.08 ms | 39.13 ms | 0.847; 0.771–0.885 |
| Imported comment | 46.57 ms | 39.82 ms | 0.851; 0.823–0.910 |
| Warning wording | 54.50 ms | 47.57 ms | 0.858; 0.851–0.879 |

There is no established speedup for active code edits or cache-disabled builds.
The game-state edit's first seven pairs showed a small increase; fifteen further
pairs also contained both faster and slower outcomes. Across 22 pairs its median
ratio is 1.026, with an interval of 0.983–1.082. All integer semantic work counters
match within each repeated pair. Ten cold-build pairs give a ratio of 0.986,
with an interval of 0.965–1.033. These noisy results do not establish a repeatable
regression or prove equal speed. Keep this uncertainty visible in later comparisons.

Evidence: `i001-comparison-deterministic.json`, `i001-lives-repeat.json`,
`i001-final-metadata-trace.json`, `i001-required.json`, and
`i001-integration.json` in the structured evidence directory. The required run
has little budget headroom; its 118.1-second result is not a claim that the suite
became faster than I000's 104.8-second run.

### I002 closure and I003 evidence

I002 remains an unaccepted source-preparation experiment. The three-way fast-path
control did not reproduce the initial pre-layout regression, so moving fields is
not a verified root cause. This does not establish a net active-edit gain.
The required timeout is real; CPU accounting for the killed process tree is
incomplete and must not be used to attribute the failure to host contention.

The standalone incremental checks pass, with 1,980 build calls and no duplicate
check names. Their summed build wall time is 101.48 s (99.38 s child CPU). Of the
1,184 builds supplying native statistics, finalization sums to 34.20 s, linking
16.46 s, frontend 2.25 s, and inference 1.54 s. The 22.47 s
`check_leaf_dependency_cascade` measurement includes nested groups; it is not the
exclusive cost of one leaf-edit case. Evidence: `i002-incremental-check-timings.jsonl`.

I003 screening: skipping only the below-limit sort reduced median finalization
20.62→20.27 ms but did not establish a whole-build gain (31 paired CPU samples);
the required suite again timed out at 120.12 s. Keep that failed screening separate.
The actual finalizer allocation probe counted 69,539 allocations for 3,160 entries.
Using LLVM directory iteration, filename views and filename-only retained records
reduced this to 6,336; seven alternating probe batches measured median scan time
16.09→11.94 ms. This synthetic result identifies avoidable representation work;
real-application acceptance and suite validation are still pending. Evidence:
`i003-sort-only-fast-control.json`, `i003-sort-only-required.json`,
`i003-trim-allocation-comparison.json`.

I003 validation isolation: the source-preparation candidate remains parked on
`codex/incremental-query-identity`. The independent cache/finalizer and timeout
cleanup patch is validated on `14fa0b78` in `codex/incremental-cache-finalization`.
This preserves the parked experiment without incorporating it into an acceptance
claim. The one extension closes rebuilding, paired benchmarks and validation; no
new optimization is added. A concurrent-suite profile completed 1,937 of 1,980
builds before timeout. Its first 1,261 builds took 66.11 s versus 53.98 s standalone
with scan-entry counts differing by 0.03%; this is not evidence of broader semantic
invalidation. Cache finalization improved in the standalone profiles, 34.20→24.75 s,
but total build time improved only 101.48→98.89 s. Host state and source-path
length differ between those profiles, so this is diagnostic screening evidence.

A related watchdog ownership bug left 909.2 MiB of verified private-worktree cache
files in RAM-backed `/tmp` after killed tests bypassed Python cleanup. The surviving
outer watchdog now owns an isolated temporary root and removes it after child
termination. Nested child/process cleanup and exact output ordering checks pass.
The next full required run still timed out; cleanup alone is not a verified fix
for the suite budget. Evidence: `i003-owned-timeout-cleanup.json`,
`i003-live-suite-temp.json`, and `i003-profile-required.json`.

### Next frontend investigation after I003

`analyzeSections` currently calls `Section::processLine` / `detectPatterns` for body
lines before `resolvePatterns` asks `selectCachedBodiesForResolutionReuse`. This
constructs body syntax before the existing classification proof can skip it.
Investigate moving that materialization behind per-declaration syntax validity and
classification demand, with unchanged-source, syntax-config and diagnostic products.
Changed implicit-parameter exports must materialize every newly affected consumer;
unchanged cached bodies must retain source anchors without rebuilding their pending
expressions. Check whether current classification input/parameter-count construction
can consume persisted read sets instead of requiring every body AST first. Do not
implement this as a whole-program cache or suppress syntax errors in edited unused
bodies. Establish the minimal reproducer, skipped-body counters and real-program
materialization cost before selecting the next iteration. This is a candidate, not
a claim that its eligibility boundary or speedup has already been established.

I003 isolated screen: compiler `ef722bb9...`, based on `14fa0b78` without the
parked source-preparation experiment, passes focused capacity, eviction tie-order,
metadata-error and default-cache checks. Thirty-one alternating real-program pairs
without statistics measured median CPU 43.873→34.505 ms; median paired ratio 0.8118
(95% interval 0.7857–0.8187). With statistics: 43.264→34.916 ms, ratio 0.7942
(interval 0.7814–0.8259). Executable hashes, diagnostics and frozen source hashes
match. Median finalization is 21.413→14.308 ms, with 3,160 entries inspected and
zero sorted below the cap. Full required, edit/cold comparisons and integration
validation remain pending. Evidence: `i003-isolated-fast-control.json`.

### I003 completed comparison; required gate still fails

The isolated comparison completed all eight cases with matching diagnostic and
executable artifacts, clean-build parity and unchanged compiler/source identities.
The 188 rows include population and clean-reference work; these are not all timing
samples. CPU measurements below compare `5c311f81...` with `ef722bb9...`.

| Case | Baseline median CPU | Candidate median CPU | Paired ratio; 95% interval |
| --- | ---: | ---: | --- |
| unchanged | 0.041334 s | 0.033055 s | 0.8035; 0.7901–0.8127 |
| comment | 0.038944 s | 0.031905 s | 0.8176; 0.7836–0.8400 |
| wording | 0.046303 s | 0.038544 s | 0.8444; 0.8042–0.8559 |
| unused | 1.068453 s | 1.060486 s | 0.9950; 0.9925–1.0823 |
| body | 2.112049 s | 2.205058 s | 0.9878; 0.9536–0.9926 |
| lives | 2.780573 s | 2.834215 s | 1.0324; 0.8813–1.0740 |
| shared-type | 2.866443 s | 3.062584 s | 1.0457; 0.9464–1.1029 |
| cold | 6.008830 s | 6.224733 s | 1.0226; 0.9542–1.0775 |

Fast-path paired improvements are 15.6–19.6%. The active-body paired ratio shows
a small improvement despite the higher unpaired median; comparisons must retain
the sample pairing. Other code-edit and cold intervals include 1 and do not prove
equal speed, a gain, or a repeatable regression. Empty-default-cache ratio is
1.0000 (0.9477–1.0767), also inconclusive.

The isolated required run returned 124 after 120.120 s, completing the incremental
checks and reaching pattern precedence before timeout. It is a failure, not a full
test pass. Separate unchanged precedence and associativity groups pass in 4.008 s
and 0.995 s. Integration is still running. No acceptance or merge is recorded.
Evidence: `i003-isolated-comparison.json`, `i003-isolated-required.json`, and
`i003-required-tail-timings.json`.

Verified frontend constraints for the next iteration: classification already
reuses `BodyInput` and query records when `sameBody(path)` holds; do not add a
duplicate unchanged-body fingerprint cache. `InitialParameterCounts::prepare`
still collects current sections and computes body digests after section parsing.
`IncrementalClassification::prepare` collects current body references before
selecting reusable units. Its `contains`/`retain` checks load digest-prefix matcher
buckets that mix result and metadata record kinds. `restoreBodyResolutionUnits`
subsequently restores each deferred reference and reuses its matcher record.
Splitting buckets alone can therefore move work to that later consumer. Couple
a storage change to avoided body materialization, or reject it if whole-build
measurements do not improve.

### I003 closure

Integration completed: 464 passed, zero failed/skipped, 971.162 s inside the runner.
Compiler SHA is `ef722bb9ddb8efff20fcdf4b3e793cd643581701fde933b4d6bfe266d264c44d`.
The required budget gate is still unfulfilled. This candidate stays isolated and
unaccepted; the full incremental objective remains active. The one extension is
finished and no further cache micro-optimization is added to I003.
Evidence: `i003-isolated-integration.json`, `i003-close-validation.json`.

I004 preflight on clean compiler `ef722bb9...`: the two uncalled parameterized
functions compile; adding their first call compiles; adding a global `value`
provider changes implicit classification and makes that call invalid. A bare
unbound name in an uncalled body does not diagnose until inference is demanded,
so syntax-recovery tests must not assume an eager type diagnostic there.

I004 red test and instrumentation: `test_incremental_body_syntax.py` exercises
unchanged local-module bodies, lexical failure/recovery, first activation, a new
provider retracting implicit parameters, and an inference diagnostic delayed until
activation. Its baseline semantic preflight matches clean builds. Instrumentation
now counts unique function bodies whose syntax is assembled during section analysis,
including reconstruction from individual expression-cache entries. This is not a
claim that their text was tokenized again. A cold two-body build reports parsed=2.
An unrelated main-file edit still reports parsed=2, restored=0, skipped=0, while
the existing matcher reports two body units reused. The work test correctly fails
on that edit. No body-syntax persistence or lazy-materialization implementation
has landed yet, and the new test is not wired into the required driver until that
implementation exists. Evidence: `i004-body-syntax-work-red.log` and
`i004-semantic-preflight.log`. The first I004 deadline remains 19:37 CET.

### I004 closure and I005 reproducer

The demand-slot compiler rebuilt successfully, and the expression-ownership C++
executable passed, including actual `cloneSectionBody` demand, independent clone
ownership, and teardown of an unrequested source slot. This is prerequisite work
on isolated commit `3b7ef529`, not an installed lazy syntax cache. I004 is parked
before its final deadline because the remaining consumers require a coupled unit
product; no body-syntax speedup has been established.

I005 baseline compiler SHA is
`abbfb3b57c8a605a8ff649fd58f4bf1770e85f8a69db0c32e78b16e8ee9678a6`.
The minimal same-file warning test is red: its unrelated edit visits both body
references and reuses zero classification/body units. With diagnostic owner
identities it visits one reference and reuses one unit, preserving diagnostics.
Its recovery case then exposed a separate verified cause: a failed build's
matcher index omits the prior successful body's resolution packet, although the
successful program's index still contains it. Partial snapshots must retain their
prior product closure; complete snapshots must still prune removed units.
Evidence: `i005-warning-red.log`, `i005-recovery-root-cause.json`.

I005 candidate `37e3681b` / compiler `b6774332...`: 328 required checks passed
uncached in 99.254 s; the gate driver confirms the compiler and tested source tree
did not change. The eight-case paired comparison completed with all diagnostic
and executable-artifact parity checks. Repeated unused/body edits have paired
CPU ratios 0.9833 [0.9462, 0.9866] and 0.9880 [0.9639, 0.9991], below the planned
5% gain. Their median frontend times fell 451.2→422.7 ms and 483.6→448.4 ms.
Body-reference visits fell 1,684→38 and 1,759→81. Cold-build improvement is not
established. Integration validation is still running.

The first measured edit after cold population had a much larger difference:
unused 1.2314→0.8777 CPU seconds and body 2.0909→1.7680, but each is only one
pair and is not an acceptance result. Before deciding whether to keep this
candidate, separately repeat the identical unused edit across seven independently
populated caches, alternating compiler order. Report it as the first-edit workload;
do not combine it with or replace the steady-state edit medians above. Evidence:
`i005-comparison.json`, `i005-required.json`, planned
`i005-first-edit-summary.json`. No merge or full-goal completion is recorded.

### I005 closure

The seven independently populated first-edit pairs completed with CPU medians
1.3090→0.9451 s and paired ratio 0.7213 [0.6650, 0.7544]. Elapsed medians are
1.3654→0.9624 s. All original source hashes, executable artifacts and clean
diagnostics match; each compiler stays unchanged within its run. Baseline computes
1,254 body matches on those first edits; the candidate computes four. The repeated
edit results remain the separate, much smaller gains above.

Integration completed: 464 passed, zero failed/skipped, 821.954 s. Both gate
drivers confirm compiler and tested source-tree identity. The minimized recovery
case has only one parameterized function, its call, and one discarded literal;
after breaking and repairing it, body reuse changes 0→1 and reference visits 1→0.
No warning is needed to reproduce that separate partial-snapshot bug. Evidence:
`i005-integration.json`, `i005-first-edit-summary.json`, and
`i005-minimal-recovery.json`.

The candidate is accepted for this measured first-edit benefit and the recovery
fix, but its current branch also contains I004 prerequisites that have no syntax
cache consumer. I006 will exclude those prototype commits from the mergeable
snapshot and validate the exact resulting tree. No merge or full-goal completion
is recorded yet.

### I006 exact-snapshot verification

The accepted-only snapshot has no `SourceExpression` type, no lazy-source provider
placeholder and no unimplemented body-syntax test in its proposed patch. Its
required and integration drivers verify unchanged compiler/source identities.
Seven ordinary unused-body edit pairs show CPU 0.9368→0.8912 s and paired ratio
0.9379 [0.9371, 0.9616]. Active-body medians are 1.9309→1.8981 s, but their paired
interval includes no improvement. Seven independently populated first-edit pairs
show CPU 1.3007→0.9536 s and ratio 0.7306 [0.7091, 0.7885]. All executable and
diagnostic comparisons pass. The three-pair cold-build result is screening data,
not an established general cold-build improvement.

Evidence: `i006-required.json`, `i006-integration.json`, `i006-comparison.json`,
`i006-first-edit-summary.json`. Archived executables embed this worktree's data
paths, so `compiler-data/manifest.json` preserves their exact library/runtime
bytes and compiler hashes. Its replay helper can temporarily materialize those
recorded data paths after worktree disposal; its frozen benchmark launcher keeps
the original measurement script and uses the live repository only for Git source
archives. Existing directories are verified and preserved.

The full objective remains incomplete: unchanged source and section processing
still runs broadly. After the accepted snapshot is integrated, return to P02/P03
and their consumers rather than extending cache-maintenance work.

The snapshot is now integrated into local master. The root build has no pending
Ninja work and its uncached required run passes within the unchanged budget.
The archived baseline and candidate also compile the frozen application after
worktree disposal, with matching diagnostics and executable artifacts. Their
temporary data directory is absent after replay. Root verification is recorded
in `i006-root-required.json` and `post-cleanup-replay.json`; compiler and source
hashes remain unchanged during the gate. This closes snapshot preparation, not
the full incremental-compilation objective.
