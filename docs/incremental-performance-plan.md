# Incremental compiler performance plan

Owner: this compiler task. Established 13 November 2026.


## Current state after I023

I023 is published: cached-function reactivation now retries names after each
definition-completion transition. All 329 required checks pass uncached on the
merged compiler in 108.502 seconds, all 464 integration checks pass, and three
real-program edit cases match fresh LLVM text and diagnostics. The full goal
remains incomplete. A named-global initializer dependency bug is verified next;
source/section construction and tooling/output-mode reuse still need work.

I022 does not change the published compiler: its sorted matcher-directory
experiment failed the independent 31-pair CPU gate and remains unmerged.

Native builds reuse source preparation, eligible body syntax/resolution, resolved
signature fingerprints, typed products and native artifacts in the verified edit
cases. I021 removes an unconsumed full declaration table; it does not complete the
compiler-wide goal. Semantic section construction and broad matching/binding,
validation and persistence work still need finer reuse. Debug/trace builds disable
major reuse paths; LSP and other output modes still need the shared frontend.
Next: repair the verified named-global initializer dependency hole (I024),
then resume P03 source-owner deferral using the completed factory/consumer audit.

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
| I007 | P02/P03/P06: retain source syntax without rebuilding unchanged body expression trees | Complete: keep, merged and verified | 22:07–23:31 CET, one extension used, before the 23:37 deadline. Baseline source `ecf05b81`, root compiler `dcb16906...`; candidate source `b85a8d6c`, compiler `adfab87b...`. Seven paired samples per code-edit case meet the predeclared 3% target: unused-body CPU ratio 0.9578 [0.9489, 0.9901], active-body 0.9577 [0.9383, 0.9776]. All eight-case artifact/diagnostic comparisons pass; 328 required checks pass uncached in 102.949 s and 464 integration checks in 825.352 s. An unused edit skips 678 body units, indexing 4,267 source expressions instead of 27,561. Partial parser snapshots retain unvisited products; complete snapshots prune removed products. Root `75d8c407`, rebuilt compiler `a3c07989...`, passes all 328 required checks uncached in 109.636 s. The merged worktree is removed and archived-compiler replay passes afterward, with its temporary data path cleaned. Source preprocessing and section-fragment reuse remain unfinished. |
| I008 | P02/P03: retain complete source-preprocessing products and lexical section inputs | Parked; not accepted | Closed before the 01:04 CET deadline on 14 November, after one extension. Prototype `e33d85ff`, compact compiler `d99e0d2c...`; baseline `4d4768c2` / `a3c07989...`. Correctly reuses 44 files and 79 transformation chunks on the real unused-body edit; import/preprocessing falls roughly 17 to 8 ms. All eight executable/diagnostic comparisons and seven focused groups pass. Compact snapshot size falls from 1.69 to 1.18 MB by encoding span-relative file identities. The decisive 31-pair result fails the predeclared 1% whole-build gain: unused CPU 0.873329 to 0.891347 s, paired ratio 1.02068 [1.00629, 1.03973]; active-body 1.880257 to 1.906683 s, ratio 1.01284 [0.99215, 1.02139]. All 328 required checks pass uncached in 106.824 s. Full integration was not rerun for this rejected candidate. Preserve the branch separately; do not merge the source-cache implementation. Semantic section fragments and efficient source-product persistence remain unfinished. |

| I009 | P06: reusable function-body input queries | Parked; not accepted | Closed 01:00 UTC on 14 November, before the extended 01:32 deadline; one extension used. Baseline `4c73c053` / `a3c07989...`; final prototype `15850647` / `ff21b137...`. First candidate `3278ce18` / `e591d597...` still rehashes 51 bodies after a one-line edit because of whole-file input keys. Per-file physical-line products fix the same-file regression: one changed body is hashed, zero bodies are hashed after code is inserted above them, and the real edit hashes one body while reusing 789 results. All nine focused groups pass, all eight artifact/diagnostic comparisons pass, and 328 required checks pass uncached in 100.421 s. Decisive 31-pair CPU ratios fail the predeclared 2% whole-build gain: unused 1.00782 [0.98712, 1.01979], active-body 1.00714 [1.00127, 1.01901]. Full integration was not run for this rejected optimization. Retain the prototype separately; do not merge its extra cache records. |

| I010 | P00/P14/P22: reduce structured-payload codec costs | Complete: keep, merged and verified | 01:03–02:25 UTC on 14 November, one extension used, before the 02:33 deadline. Candidate source `3dcc89a5` / compiler `c3c0252a...` clears the 25% codec gate (ratio 0.3415) and the 2% ordinary-edit gate: unused CPU ratio 0.9322 [0.9183, 0.9487], active body 0.9610 [0.9253, 0.9672]. Eight-case executable/diagnostic parity passes. Candidate: 329 required checks uncached in 111.020 s and 464 integration checks in 822.425 s. Merged source/docs `73eca64c`; root compiler `cfbeec1f...` passes 329 required checks uncached in 101.873 s. Eleven cold pairs establish no change. The merged worktree is removed; an actual archived-compiler benchmark replay passes after removal and cleans its temporary data root. This reduces persistence costs, not source/section reuse coverage. |
| I011 | P02/P03: source chunks with a compact section-line consumer | Complete: keep, merged and verified | 02:31–03:36 UTC on 14 November; one extension used, before the 04:01 deadline. Source `c4907513`, compiler `111070f3...`: seven paired unused-body edits have CPU ratio 0.97037 [0.96212, 0.98083], active-body 0.96685 [0.95092, 0.99687]. All eight-case 180-row executable/diagnostic comparisons pass; eleven cold pairs establish no change. The unused edit reuses 44 files, 79 chunks and 7,087 section-line products; CodeLine layout is unchanged. All 329 required checks pass uncached in 103.073 s and all 464 integration checks in 899.856 s, with unchanged source/compiler hashes. Merged source/docs `9bccf692`, rebuilt root compiler `a0d50521...`, passes all 329 required checks uncached in 100.451 s. The merged worktree is removed, archived replay passes after removal, and its temporary data root is cleaned. Semantic section reconstruction and early-failure source publication remain incomplete. |
| I012 | P13: publish completed parser/source products before import-stage success | Complete: keep, merged and verified | 03:41–04:56 UTC on 14 November; one extension used, before the 05:11 deadline. Source `91787c29`, compiler `7836413f...`, retains completed products after early import/preprocessing failures. The repeated-failure real application reuses all 45 files instead of processing them; fifteen paired samples have CPU ratio 0.74177 [0.71805, 0.88529]. The complete eight-case 196-row parity matrix and independent 31-pair active-body control establish no ordinary/cold change. Candidate: 329 required checks uncached in 112.431 s and 464 integration checks in 870.334 s. Merged source/docs `2858eb6a`; root compiler `ce8d77e1...` passes 329 required checks uncached in 112.520 s. The merged worktree is removed and actual archived replay passes afterward, cleaning its temporary data root. Semantic section/reference preparation is the next selected boundary; the full goal remains incomplete. |

| I013 | P06: reuse source-proven classification declaration inputs | Complete: keep, merged and verified | 04:57–06:26 UTC on 14 November; one extension used, before the 06:27 deadline. Final source `d77c79cd` / compiler `06ecae56...` follows explicit producers for generated declarations. Real unused/active edits evaluate one input and reuse 1,747. Independent 31-pair unused CPU ratio 0.98479 [0.96568, 0.99350] clears the 1% gate; medians 0.811005 to 0.791013 s. Input preparation falls 27.944 to 8.304 ms. Active-body, other common and cold gains remain unproven; their intervals show no established regression. All 196 current eight-case rows plus the 66-row control pass the stated artifact/diagnostic checks. Candidate: 329 required uncached in 111.728 s, 464 integration in 830.955 s. Merged source/docs `f3c07fd6`; root compiler `4bff53b6...` passes 329 required uncached in 110.554 s. The merged worktree is removed and archived replay passes afterward, cleaning its temporary data path. Full goal incomplete. |

| I014 | P03/P06: demand-driven generated body products | Complete: keep, merged and verified | Started 06:27 UTC on 14 November; first deadline 07:12 UTC, extended deadline 07:57 UTC; one extension used. Baseline `40b94d06` / compiler `4bff53b6...`, final production code `e9a82d20` / compiler `7b01bbee...`. Unused edits index 551 expressions instead of 4,267, deferring all 929 generated property bodies; active edits index 937 instead of 4,601. The independent 31-pair unused CPU ratio is 0.97355 [0.95838, 0.99494], clearing the predeclared 1% gate. All 292 final comparison rows pass their artifact/diagnostic checks; common and cold controls establish no regression. All 329 required checks pass uncached in 119.241 s. The integration run passes 463 checks and exposes three unmigrated C++ ownership-test doubles; after implementing their new ownership interface, the remaining check rebuilds and passes with the production compiler unchanged. Preserve the original failed full run. Root compiler `092562a6...` passes 329 required uncached in 117.219 s; merged worktree removal and actual archived replay pass. Verification overruns the 07:57 deadline; no additional optimization is added. Full semantic section/reference reuse and clean-IR parity remain incomplete. |
| I015 | P03/P06: resolved match products with independently reusable effects | Parked; not accepted | 08:08–09:24 UTC on 14 November; one extension used, closed before its 09:38 deadline. Source `54692e8e`, refined compiler `8cb03cbc...`. Match construction falls sharply, but the independent first-candidate 31-pair CPU gain is only 1%, and the refined screen does not demonstrate the original 2% gate. All focused/parity checks pass; 329 required checks pass uncached in 106.732 s. Integration was not rerun for the rejected candidate. Preserve the unmerged branch and three compiler snapshots; owned worktree removed. Published compiler code remains `9cbdc95a`; full goal incomplete. |
| I016 | P06: independent lookup edges and local definition origins | Correctness fix kept; optimization parked | Started 10:03 UTC on 14 November; one extension used. Lookup-edge candidate `07c4d241` fails its 0.5% gain gate after 31 pairs and remains unmerged. Isolated source-origin fix `df66b881` / compiler `215313d6...` corrects stale implicit parameters after moving a local definition across imported files. Required: 329 uncached in 107.937 s. Integration: 464 in 844.401 s. All 188 matrix and 66 independent control rows pass parity; the control does not reproduce the initial body slowdown. Root merge `60aa8d4f` / compiler `86635023...` passes 329 required uncached in 107.293 s. Owned worktree removed; actual six-row archived replay passes and cleans its data directory. Full goal incomplete. |

| I017 | P03/P06: demand-driven whole-body resolution products | Complete: kept and published | Unused CPU improves 6.35%, active body 5.86%; three full LLVM parity cases pass. Required: 329, root 101.113 s; integration: 464. Unchanged matching/binding products stay encoded until demanded. |
| I018 | P03: section-stage measurement and header-cache hypothesis | Complete: instrumentation kept; optimization rejected | Header-cache target measured below 1% of the workload, so no gain is claimed. Retained scoped section counters and failed-stage timing. Required: 329, root 103.850 s; integration: 464. |
| I019 | P06: ordered source-input joins | Complete: rejected | Independent 31 pairs do not establish the 2% gate. Prototype remains unmerged; evidence and source retained. Published decision: `5a213ad7`. |
| I020 | P14: resolved declaration input/output units | Complete: kept and published | Active edits audit one of 1,750 inputs. Unused CPU improves 10.1%; ordinary/cold controls and LLVM parity pass. Required: 329, root 103.048 s; integration: 464. Published `cf78161d`. |
| I021 | P14: consume fingerprints without redundant full records | Complete: kept, merged and qualified | Independent 31-pair unused CPU gain 4.46% [3.07%, 5.33%]; active gain 1.58% in seven pairs; no established cold change. Required: 329, root 107.682 s; full integration: 464 in 789.208 s; three LLVM parity cases. Measured and root archives replay after worktree removal. |
| I022 | P03/P06 screening, then P14 sorted matcher record directories | Complete: rejected | Eager tokenization and binding-proof hypotheses were screened out. Sorted directories pass focused replay/eviction/corruption checks, but independent 31 pairs give unused-edit CPU ratio 1.002023 [0.972789, 1.011769], failing the predeclared 2% gate. No experimental C++ or inspector change reaches master. |
| I023 | Resolver completion transitions after cached-body reactivation | Complete: published and verified | Baseline fresh success/cached failure and debugger states false/true/true verified. Two-file state-transition fix passes repeated activation/value/deactivation, cached-syntax and classification assertions, broader body/cascade checks, and 329 required checks uncached in 108.166 s. Three real-program LLVM/diagnostic edit cases and all 464 integration checks pass. Merged root a49ebcf0 passes all 329 required checks uncached in 108.502 s; snapshot 5f11c2aa is pushed; worktree/branch removed and archived replay passes after removal. |

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

### I007 deferred source syntax and measured result

Cached syntax now has a consumer. Source-position handles carry matcher captures
without requiring an expression tree. The parser retains reference outlines,
registers current lexical references and bindings, and materializes a line's
source tree when inference or another semantic consumer asks for it. Inference
indexes that publication before callers can clone or regroup the tree. Source
teardown inspects published pointers without demanding unused products. The
parsed-expression packet is version 3; the compiler identity separates it from
older derived cache entries, with no old-format reader.

The focused test keeps two distinct dormant bodies unmaterialized after an
unrelated edit, demands one on activation, checks a newly added provider retracts
implicit parameters, and preserves clean-build diagnostics on activation and
repair. Reclassification can use the outline without demanding either tree.
The four-line early-parser-failure test initially showed repair reconstructing
two unchanged bodies. Partial snapshots now retain all unvisited syntax products;
a subsequent complete snapshot removes a deleted body's sentinel packet.

The frozen real-application comparison uses seven alternating pairs for each
code-edit case and fifteen for the fast paths. Median CPU for an unused edit is
0.8815→0.8495 s; for an active-body edit it is 1.8014→1.7475 s. Their paired median
ratios are approximately 0.9578, with the intervals in the ledger excluding no
improvement. Other cases have no established change. The three-pair cold screen
has paired ratio 1.00005 [0.9882, 1.0210]; it establishes no cold-build improvement.
All 180 rows pass the benchmark's diagnostic and executable parity checks.
Both archived compilers use identical runtime bytes through the supported
installation layout, avoiding differences between runtime builds in separate
source directories. Their exact source/configuration data and benchmark launcher
are retained for replay after worktree removal.

Evidence: `i007-required.json`, `i007-integration.json`, `i007-comparison.json`,
`i007-final-focused.log`, `i007-ownership.log`, `i007-parser-failure-probe.json`,
`i007-partial-parser-red.log`, and `i007-compiler-data/manifest.json` in the
structured evidence directory. Compiler and source identities remain unchanged
during the required and integration runs. The root snapshot is now rebuilt and
passes its uncached required gate in 109.636 s (111.000 s including the recording
driver). Its merged worktree and branch are removed. Actual archived-compiler
replay succeeds after removal and leaves the temporary data directory absent.
Evidence: `i007-root-required.json`, `i007-worktree-cleanup.json`, and
`i007-post-cleanup-replay.json`. A root build was deliberately interrupted while
another agent was building and available memory was about 2 GB; it resumed with
one job after that build finished. No compiler or suite failure is hidden by that
resource wait.

This is a function-body syntax milestone. Imports, source transformations and
section construction still run broadly, and non-function syntax contexts and
other compilation modes need further incremental consumers. The full objective
remains active; the next iteration returns to retained per-source frontend
products and their downstream consumers.

### I008 decision: preprocessing reuse alone does not meet the performance gate

The initial compiler `a89e0b45...` passes required checks in 106.388 s and
all eight paired artifact/diagnostic cases, but its unused-body CPU ratio is
1.02625 [1.00575, 1.03013]. Its parser snapshot includes 762,741 bytes of
transformation products and repeats absolute source paths 3,558 times. Compact
input-span file identities remove about 509 KB from the complete snapshot.
The compact candidate still misses the end-to-end target in 31 ordinary pairs.
This is a rejected optimization hypothesis, not a completed source-stage milestone.

The measured preprocessing saving is real. The added parser products and their
restoration/persistence are also real costs. These measurements do not establish
a complete attribution for the remaining whole-build regression; do not describe
the repeated paths as its sole root cause. No experimental compiler code is
merged into master. Full integration is deliberately not claimed for this branch.

Evidence is in `.cache/incremental-evidence/structured-20261113/`: immutable
`i008-compilers` and `i008-compact-compilers`, `i008-first-source`, the two
eight-case comparisons, `i008-compact-31-comparison.json`, both focused logs,
`i008-first-required.json`, `i008-required.json`, and
`i008-source-payload-analysis.json`. Required runs record unchanged compiler
and source hashes. The parked source commit is `e33d85ff`.

The next independent P06 candidate is reusable function-body input queries.
`definitionBodyDigests` currently walks all logical lines and constructs normalized
body strings at multiple projection boundaries. First verify its input lifetime
and consumers, then retain unchanged unit results and share them within one
compilation. Membership, cross-file continuations, syntax/config changes and
partial failures must remain explicit dependencies. Benchmark the complete
build, including any new query index; do not repeat I008's storage-only gain.

### I009 decision: finer input reuse still misses the end-to-end gate

The normalized-input query implementation is correct on the tested edits, including
source-line insertions, cross-file continuations, deferred-body ownership changes,
configuration, errors and recovery. Its recorded work falls from 51 normalized
bodies to one for the real string-library edit. It adds physical-line records and
body-result records to the segmented matcher cache. The measured complete build
still does not improve by the required 2%; more retained products are not evidence
of a faster compiler. Preserve the source on `codex/incremental-body-inputs`.

All 31 active-body pairs complete normally: CPU medians are 1.780111 to 1.795784 s.
The original two-case 31-pair process exits 143 after all 31 unused pairs but before
clean parity. No child remains; its source hashes and staged compiler hashes are
verified intact. `resume-i009-unused-parity.py` completes clean parity at those exact
paths, compares both clean compiler artifacts, saves a new unused-only report, and
removes its owned workspace. The original report stays incomplete. The completed
unused CPU medians are 0.889054 to 0.896429 s. No termination cause is claimed.

Evidence: `i009-comparison.json`, `i009-line-comparison.json`,
`i009-line-31-unused-resumed.json`, `i009-line-31-body-comparison.json`,
`i009-same-file-red-counters.json`, `i009-line-focused.log`, `i009-required.json`,
and both immutable compiler directories under the structured evidence directory.
The exact final compiler/source hashes remain unchanged during the required run.
The test driver also moves an existing configuration check into its helper module
to remain below 1,000 lines; its assertions and invocation order are retained.

Before adding another cache layer, measure retained projection encoding and loading.
One baseline real-program `all_function_projection` payload is 3,175,927 bytes. Its
largest serialized fields are constraint scheduling (603,948), matcher projection
(547,098), inactive definitions (429,310), and declarations (404,152). Compare the
existing JSON codec with existing-library binary codecs on retained real payloads
first, then pursue a complete-build candidate only if that measurement supports it.
This is a separate P00/P22 investigation; source/section-fragment reuse is unfinished.

### I010 codec screening

On six retained LEXI8 structured payloads (7,093,754 JSON bytes), generic CBOR and
MessagePack do not clear the codec gate. Encoding is faster but their existing
readers decode more slowly. A bounded cache CBOR reader copies complete strings,
reserves arrays and uses ordered map insertion hints. Seven rotated-order samples
with three iterations each reduce combined median codec CPU from 108.854 to
37.177 ms (ratio 0.3415), including 48.977 to 17.126 ms for the function projection.
All six payloads round-trip unchanged. Native property checks compare the reader
with nlohmann on 1,000 randomized values, integer boundaries, floating-point
values, Unicode, binary subtypes and malformed frames. Python inspection checks
consume a native-generated fixture. This establishes only the codec hypothesis;
whole-build timing, required and integration gates still decide acceptance.

Evidence: `i010-codec-study/manifest.json`, both raw codec result files, retained
JSON payloads, the standalone tool sources, and their build logs. No system JSON
library is patched and no new external dependency is added. All program-payload
inspection helpers are migrated to the single current CBOR representation.

### I010 decision: keep the measured codec improvement

Candidate source `3dcc89a5a674dfc28dba805995222b8ee2b97190`, compiler
`c3c0252a48126c66c058f493b0dd6552968eed5a75da14f351ae8c6a7208172c`.
The baseline is source `20e3a323` and compiler `a3c07989...`. These are identical
frozen LEXI8 inputs, with alternating compiler order and seven pairs per ordinary
edit. Reported ratios are medians of paired CPU ratios, which need not equal the
ratio of the two marginal medians.

| Edit | Baseline median CPU | Candidate median CPU | Paired CPU ratio, 95% interval |
| --- | --- | --- | --- |
| Unused library body | 0.868897 s | 0.802388 s | 0.9322 [0.9183, 0.9487] |
| Active function body | 1.881585 s | 1.790403 s | 0.9610 [0.9253, 0.9672] |
| Application lives value | 2.249134 s | 2.181439 s | 0.9659 [0.9619, 0.9676] |
| Shared type | 2.328697 s | 2.212862 s | 0.9541 [0.9406, 0.9735] |

The complete eight-case matrix has 180 rows and passes executable/diagnostic
parity against both compilers and clean diagnostic builds. Fast paths have
15 pairs each; no new wording-only performance claim is made. Eleven additional
cache-disabled cold pairs give CPU medians 5.404107 to 5.388814 s, paired ratio
1.0045 [0.9915, 1.0096], so neither a cold gain nor regression is established.
For the unused edit, median payload loading falls from 51.911 to 24.546 ms and
cache writing from 49.171 to 31.052 ms. Semantic work counters retain the same
stage boundaries: this improves the cost of reuse, not its coverage.

The candidate passes 329 required checks uncached in 111.020 s, including the
native codec fidelity check. The native helper is part of the ordinary C++ build,
so its compilation is outside the post-build suite budget. All 464 integration checks pass in 822.425 s (823.759 s including the recording
driver); compiler and source hashes remain unchanged. The implementation is
merged to local master as source/docs `73eca64c`. Rebuilt root compiler
`cfbeec1f6763f47d44dee85d948b61e10155ea8214fda74a3e0cd074846e93e8`
passes all 329 required checks uncached in 101.873 s (102.787 s including its
driver), with unchanged source and binary hashes. The merged owned worktree is
removed. An actual archived-compiler benchmark replay passes after removal,
with identical executable/diagnostic results and unchanged compiler hashes;
its temporary original data path is removed on exit.

Evidence: `i010-comparison.json`, `i010-cold-11-comparison.json`,
`i010-required.json`, `i010-integration.json`, `i010-focused.log`, and `i010-compiler-data/manifest.json`
under the structured evidence directory. The archive retains the six I009/I010
compiler identities and their shared original source-data path for replay after
worktree disposal. Compiler-specific cache namespaces isolate the current format;
all affected artifact readers use CBOR directly without format guessing.


### Next selected boundary: P02/P03 source and section units

Unchanged source preparation and semantic section construction remain incomplete.
In the I010 real unused-body edit, a measured sample still spends 16.632 ms in
import/preprocessing and 17.017 ms in section analysis. I008 reduced median import
work from 16.584 to 7.950 ms, while section analysis stayed at 14.800 to 15.626 ms;
its complete-build CPU gate failed. Those stage measurements are elapsed times,
not proof of the cause of the CPU regression.

Resume this boundary with the consuming section/body-input paths in scope. Retain
cross-file continuation, syntax, indentation, source identities and error-demand
order. Avoid an independently serialized product that merely moves the scan into
loading and reconstruction. The next iteration must declare its specific product,
consumer, work-count test and performance gate before changing compiler code.
No frontend milestone is marked complete by I010.


### I011 screening: source units reach their section-line consumer

Source `c4907513`, compiler `111070f39138ac63bd051fae465192e3fc7314e8a4c8a7029c6eb58acdc36e24`,
passes the focused source/configuration/import/error-order tests. The immutable
baseline is source `aec8ca12`, compiler `cfbeec1f...`. Seven paired ordinary edits
produce identical executables and diagnostics in all 36 rows. CPU medians are
0.789763 to 0.770632 s for unused-body edits (paired ratio 0.97037,
95% interval [0.96212, 0.98083]) and 1.690405 to 1.642350 s for active-body edits
(ratio 0.96685 [0.95092, 0.99687]). These clear the declared screening gate;
remaining cases and full validation still decide acceptance.

For the unused edit, 44 files, 79 transformation chunks and 7,087 section-line
products are reused; one file, two chunks and 427 section-line products are
processed. The active-body edit reuses 44 files, 75 chunks and 7,357 section-line
products, processing 157 products. The CodeLine layout matches the baseline
exactly. This is not proof that I008's layout caused its measured regression.
The compact stream's consumer is ordinary section analysis; semantic Section
objects are still constructed. No semantic section-unit milestone is claimed.

Evidence: `i011-red.log`, `i011-build.log`, `i011-focused.log`,
`i011-first-comparison.json`, and `i011-compilers/manifest.json` in the structured
evidence directory. Extend once to finish the remaining matrix, uncached required
suite, affected integration and merged-root verification; do not add another
optimization during validation. The extended deadline is 04:01 UTC.


### Verified remaining boundary: source products before import failure

A three-line local-import probe on exact compiler `111070f3...` loads a healthy
helper, then fails on a missing import. Creating only that missing file causes all
three source files to be prepared again; the healthy helper is not reused. The
source product is computed, but native failure publication is gated on
`compilationStage >= ImportedFiles`. Import failure returns while the stage is
NotStarted, so the completed parser/source product has no published index.
This is a remaining P13 incremental boundary, not a wrong program output or a
change to the validated successful-edit claim. Publish parser/source products at
their actual completion boundary independently of completed semantic program
state. Do not advance the stage or invent bindings/inference results after failure.

The minimal sources, compiler hash, diagnostics and before/after counters are in
`i011-source-failure-probe.json`. Address this small publication boundary in its
own iteration, then continue semantic section-unit reuse. The full objective
remains incomplete while either boundary is missing.


### I011 decision: keep, merged and verified

The remaining six-case comparison passes all 144 rows, completing the eight-case
180-row matrix with the earlier ordinary-edit comparison. Fifteen-pair unchanged,
comment and wording checks show no established CPU regression. Seven-pair lives
and shared-type edits likewise show no established change. Eleven independent
cache-disabled cold pairs give CPU medians 5.360796 to 5.344953 s, paired ratio
0.99872 [0.98152, 1.00886]. Do not claim a cold improvement from this result.

The exact source/binary passes all 329 required checks uncached in 103.073 s
(104.034 s with its recording driver), and all 464 integration checks in
899.856 s (901.859 s with its driver). Source and compiler hashes are unchanged.
The implementation clears the declared whole-build and correctness gates.
The merged source/docs revision is `9bccf692`; rebuilt root compiler
`a0d50521bb178de53fa5b501f2e6417d735c9e32c39860d0b418dea8c6f30bb3`
passes all 329 required checks uncached in 100.451 s (101.865 s with its driver),
with unchanged source and binary hashes. Its compiler and test sources match
`c4907513` exactly. The merged owned worktree is removed after verifying it is
clean, fully merged and has no live process users. The data bundle preserves six
I008/I011 archived compiler identities. An actual one-pair archived benchmark
replay passes after removal and cleans its temporary original data path; this is
a reproducibility check, not an additional performance claim.

For unused edits, import/preprocessing median elapsed time falls 15.576 to
7.414 ms, section analysis stays 14.913 to 14.488 ms, and parser snapshot writing
increases 3.357 to 4.557 ms. These explain the measured work boundary, not the
entire CPU change. The next larger representation cost is visible in the same
samples: only 38 body references are revisited, but 7,342 results are restored;
resolution setup takes a median 140.319 ms. The source-unit change does not
complete semantic section reuse or eliminate that reconstruction.

Evidence: `i011-first-comparison.json`, `i011-remaining-comparison.json`,
`i011-cold-11-comparison.json`, `i011-required.json`, `i011-integration.json`,
`i011-root-required.json`, `i011-post-cleanup-replay.json`,
and `i011-compiler-data/manifest.json` under the structured evidence directory.


### I012 checkpoint and cache-maintenance evidence

The root baseline fails the minimal repeated-import-failure reuse test. Initial
source `8af6836b` makes the source/checkpoint/wording/pruning tests pass. Review
finds its new parser index is ignored by the cache budget: the broad `parser-`
prefix matches first, then rejects the longer name. A runtime sparse-entry probe
confirms the file is retained with zero removals. Source `91787c29` registers the
shared index prefix before the broader prefix; the same probe removes the old
index while keeping frontend reuse. The existing required eviction check includes
this artifact kind. All focused checkpoint, source preparation and body-syntax
checks pass on compiler
`7836413f84d3586e4b1efaa10f67d6ea0866e9f54c6b8b951132b02718d3b468`.

The initial seven active-body pairs show CPU ratio 1.01804 [1.00583, 1.04122].
Do not dismiss that result. An independent 31-pair control does not reproduce an
established regression: CPU medians 1.820397 to 1.824057 s, paired ratio 1.00432
[0.98819, 1.02324]. Semantic work is unchanged, including one built and 257 reused
objects. The initial apparent slowdown has no verified external cause; no CPU-hog
claim is made. The remaining common/cold checks still decide acceptance.

A separate 15-pair benchmark uses the same frozen real LEXI8 application with one
missing final import. Baseline reprocesses all 45 files; candidate reuses all 45,
processing zero. CPU medians fall 21.048 to 16.777 ms, paired ratio 0.74177
[0.71805, 0.88529]. Diagnostics match both clean builds; no inference, object
creation or linking occurs. Marginal medians and medians of paired ratios are
different statistics. This proves reuse on an actual failed-program workload,
without claiming an ordinary successful-build speedup.

Evidence in the structured directory: `i012-red.log`, `i012-first-focused.log`,
`i012-index-retention-analysis.json`, the eviction red/green probe files,
`i012-focused.log`, `i012-first-comparison.json`, `i012-body-31-comparison.json`,
and `i012-source-failure-comparison.json`. The first binary is preserved in
`i012-first-compiler`; corrected and baseline binaries are in `i012-compilers`.
One extension to 05:11 UTC is used to complete the matrix, required/integration
gates and merged-root verification. No further optimization is added during them.

The completed eight-case comparison contains 196 rows. The remaining fifteen-pair
unchanged, comment and wording checks, seven-pair lives and shared-type edits,
and eleven-pair cache-disabled cold comparison establish no ordinary-build change.
Cold CPU medians are 5.738215 to 5.809502 s, paired ratio 0.99835
[0.96918, 1.03936]. These results do not establish a successful-build speedup.
All executable and diagnostic parity checks pass. The exact candidate passes all
329 required checks uncached in 112.431 s (113.799 s including the recording
driver), with unchanged source and binary hashes. All 464 integration checks pass
in 870.334 s (871.590 s including the recording driver), again with unchanged
source and binary hashes. The merged root `2858eb6a` has identical compiler and
test sources. Rebuilt compiler `ce8d77e145bdfcc32f8859cbafe2593b93cb3dad862abef2ceed83f2b67c554b`
passes all 329 required checks uncached in 112.520 s (113.708 s with its driver),
with unchanged source and binary hashes. The clean merged worktree is removed;
an actual archived benchmark replay passes after removal and removes its temporary
data directory. Its single timing pair is a reproducibility check, not new
performance evidence.

The next frontend boundary is semantic section/reference preparation. In the
31-pair active-body control, median resolution setup is 171.640 ms; classification
is 114.561 ms, including 29.668 ms preparing inputs and 35.634 ms loading products.
Only one query is built, but 1,748 are reused and their units are still prepared.
The resolver visits 81 references while restoring 7,299 saved results. Reusing
lexical products and deferred expression syntax does not yet avoid that work.
The full goal also remains incomplete for semantic section fragments, tooling,
other output modes, and broader semantic/backend metadata materialization.

Evidence completing I012: `i012-remaining-comparison.json`, `i012-required.json`,
`i012-integration.json`, `i012-root-required.json`, `i012-worktree-cleanup.json`,
`i012-post-cleanup-replay.json`, and `i012-compiler-data/manifest.json`.

### I013 first-candidate evidence and refinement

Source `82965a7b`, compiler `e9c1dfc3...`, passes the minimal work checks,
independent retained/fresh input parity, and full classification cycle, nested
lookup and arity-retraction checks. The 36-row real-program comparison passes
artifact and diagnostic parity, but reuses only 818 of 1,748 declaration inputs
while evaluating 930 generated units: 46.8%, below the predeclared 50% gate.
Unused-edit input preparation falls 28.163 to 25.584 ms; whole-build paired CPU
ratio 1.01999 [0.97908, 1.02587] establishes no gain. Active-body CPU ratio is
0.96953 [0.94639, 1.00732], also uncertain. Do not accept this candidate.

The single extension adds explicit source provenance for compiler-generated
declarations and consumes those producer body dependencies in the same input
reuse proof. Cover all declaration generators and test generated signatures
before changing the compiler. Keep the original work, CPU and correctness gates.
No further extension is available after 06:27 UTC.

### I013 producer-aware screening

Source `d77c79cd` / compiler `06ecae56...` passes the generated-declaration
work tests and all focused classification, propagation, nested lookup and arity
checks. Its retained inputs match fresh recomputation, including producer identities.
On each real unused/active edit it builds one of 1,748 declaration inputs and
reuses 1,747, clearing the 50% work gate. The first seven-pair CPU screen is
uncertain: unused ratio 0.97240 [0.90010, 1.00635], active 0.99375
[0.98152, 1.01630]. An independent 31-pair unused comparison clears the
predeclared CPU gate: medians 0.811005 to 0.791013 s, paired ratio 0.98479
[0.96568, 0.99350]. Wall ratio 0.98530 [0.94559, 1.00399] remains uncertain.
Input preparation falls 27.944 to 8.304 ms; classification 97.386 to 78.018 ms.
Ordinary-body/cold/common gates and full suites still decide acceptance.

The remaining six-case comparison completes all 160 rows, giving 196 current
eight-case rows plus the independent 66-row unused control. Baseline/candidate
native artifact equality and incremental/clean diagnostic parity pass. Unchanged,
comment and wording CPU intervals include no change, as do seven-pair lives and
shared-type edits. Eleven cache-disabled cold pairs have CPU medians 5.632530 to
5.713482 s, paired ratio 0.99448 [0.98349, 1.01702]; no cold improvement or
regression is established. The speedup claim remains limited to the verified
unused-body CPU workload. The candidate passes all 329 required checks uncached
in 111.728 s (112.817 s with its recording driver), with unchanged compiler and
test source hashes. All 464 integration checks pass in 830.955 s (832.423 s with the recording
driver), with unchanged compiler and test source hashes. Merged-root checks are
complete: merged source/docs `f3c07fd6`, rebuilt compiler
`4bff53b639a7a88e3240c452b7e1f29968ea7fb286e9a8f9d7ed8b321e389d2c`,
passes all 329 required checks uncached in 110.554 s (111.791 s including its
driver), with unchanged compiler and test sources. The clean merged worktree is
removed; archived replay passes after removal and cleans its temporary data root.
Its single pair verifies reproducibility, not additional performance.

The benchmark's clean comparison checks diagnostics. Its native executable hash
comparison is between baseline and candidate; clean native executables have
different bytes because their compilation routes differ. Focused runtime fixtures
compare incremental and clean execution output. Do not describe the real graphical
application benchmark as proof of clean executable equivalence; normalized clean
IR or rendered-output parity remains a full-goal verification gap.

I013 evidence: `i013-first-comparison.json` preserves the rejected authored-only
screen. `i013-producer-comparison.json`, `i013-unused-31-comparison.json`,
`i013-remaining-comparison.json`, `i013-producer-unit-input-parity.json`,
`i013-required.json`, `i013-integration.json`, `i013-root-required.json`,
`i013-worktree-cleanup.json` and `i013-post-cleanup-replay.json` record the
accepted candidate and verification. Three compiler identities and their data are
preserved in `i013-compiler-data/manifest.json`.

Next selected boundary: P03/P06 body-product materialization. Measure remaining
generated body construction and reference-outline restoration, then remove actual
consumer-driven materialization of unaffected units. Existing `SourceExpression`
body products and generated-declaration provenance are the relevant interfaces.
Add the work-count regression before changing them; avoid another hash/codec
layer. Preserve the clean-IR verification gap above until it is directly tested.

### I014 first screen and consumer refinement

Source `e04a386e` / compiler `553a66d1...` passes the focused generated-body,
body-reuse, source-syntax and classification tests. All 36 real-program rows pass
the benchmark parity checks. Unused edits index 551 expressions instead of 4,267,
with zero generated bodies materialized and 929 deferred; active edits index 937
instead of 4,601, materializing 13 generated bodies and deferring 916. The seven-pair
unused CPU ratio is 0.99087 [0.94760, 1.00270], below the predeclared gain
and certainty gates. Active CPU is 0.99642 [0.97095, 1.00897]; active wall
ratio 1.01476 [1.00717, 1.03409] needs independent verification. No acceptance.

Source-index time remains 10.046 to 11.858 ms on unused edits despite fewer
nodes. Its constructor still copies/sorts all scopes, registers every root and
indexes all declaration bindings. The single extension removes that eager consumer
work: retain the existing ordered scope map, register deferred scope roots on demand,
and index declaration bindings on their first actual query. Materialized source
syntax still receives its canonical identities before inference can change grouping.
The added scope-work check precedes this correction. No further extension is
available after 07:57 UTC.

### I014 demand-driven index verification

Refined source `e9a82d20` / compiler `7b01bbee...` passes the focused body,
source-syntax, source-scope and classification checks. An independent 31-pair
comparison per ordinary case passes all 132 artifact/diagnostic rows. Unused
CPU medians are 0.791647 to 0.781350 s; paired ratio 0.97355
[0.95838, 0.99494] clears the predeclared 1% gate. Wall ratio is 0.96129
[0.91728, 0.98429]. Index time falls 10.944 to 0.895 ms; only 48 scopes
and zero declaration-binding records are indexed, with 551 expression nodes.
Active edits index 104 scopes and 937 nodes, materializing 13 generated bodies
and deferring 916. Their CPU ratio 0.99933 [0.97660, 1.01877] establishes no
change; wall ratio 1.04022 [0.97393, 1.09339] remains uncertain. The initial
apparent wall regression is not reproduced as established. Retain all outliers:
one candidate sample takes 4.801 wall seconds and 1.810 CPU seconds. This
shows waiting but does not identify its cause.

The remaining six-case matrix completes all 160 rows. Together with the 132-row
ordinary-edit comparison, all baseline/candidate native artifact checks and
incremental/clean diagnostic comparisons pass with unchanged compiler binaries.
Fast-path and shared-type intervals include no change. The top-level lives edit
has paired CPU ratio 0.96850 [0.94448, 0.99337], although the unpaired medians
move in the other direction; preserve the alternating-pair evidence. Eleven cold
pairs have CPU ratio 0.98676 [0.96251, 1.00579], establishing no change.
The observed outer comparison session returned 143 after writing its complete
report and final summary; all 160 child invocations have return code zero. The
signal's cause is not established, and the report is not evidence of clean native
executable equivalence. Focused runtime parity passes; full suites are pending.

I014 comparison evidence: `i014-first-comparison.json` retains the initial screen;
`i014-index-31-comparison.json` and `i014-remaining-comparison.json` contain the
final measurements. `i014-compiler-data/manifest.json` preserves all three compiler
identities and 105 unchanged library/runtime data files for replay after cleanup.

The candidate passes all 329 required checks uncached in 119.241 s (120.621 s
including its observation driver), with unchanged compiler and test sources.
The enforced suite timer excludes that outer observation overhead. Headroom is
only 0.759 s; this is a passing run, not evidence that the budget has ample margin.
The broader integration gate is running against the same frozen candidate.

The remaining-stage audit uses all 31 candidate pairs. For an unused edit,
median parse time is 15.184 ms and resolution 345.579 ms, including 129.416 ms
setup, 68.073 ms cached-body selection and 26.260 ms match restoration. It still
restores 7,342 body references and rebinds 3,637 variable references. These are
existing products rebuilt into current mutable state, not skipped units.
`i014-remaining-stage-audit.json` records the measured boundaries and the native,
tooling and parity gaps. The next consumer goal is to retain unchanged resolved
reference products rather than reconstruct their match/binding state eagerly;
semantic section reuse remains required. No implementation or timing claim for
that next goal is made here.

The integration run finishes in 869.169 s (871.765 s including its driver):
463 pass and one fails because three C++ ownership-test products lack the new
pure virtual ownership accessor. This is an incomplete interface migration in
the proposed patch, not a DynLex language failure. All five implementations are
now migrated. The affected ownership check rebuilds and passes, preserving every
existing assertion and adding a check that querying ownership does not demand
syntax. The production compiler remains byte-identical, so the 463 passing
integration checks and this corrected check cover all 464 checks; do not relabel
the original full run as passing. Evidence: `i014-integration.json`, its full log,
and `i014-ownership-migration.log`. No compiler source changed after the matrix or
required run. Accept the measured optimization and proceed to root verification.

Merged source/tests `4d835526` is rebuilt in the root repository in 701.796 s,
using one job and a measured peak child RSS of 2,259,208 KiB. Root compiler
`092562a6131d285b70f5dddbf3845ecf12849f6601c6e1768a3e19b3be670c7b`
passes all 329 required checks uncached in 117.219 s (118.531 s including the
observation driver), with unchanged sources and executable. The worktree was
removed after verifying it was clean, fully merged, archived and unused by live
processes. Its 105 data files and all three compiler identities are preserved.
The actual archived replay passes all six rows after removal and cleans its temporary data root. This single pair verifies reproducibility, not another performance claim. Evidence: `i014-post-cleanup-replay.json`.

The 07:57:24 UTC deadline was exceeded during merged-root verification. Record
the overrun rather than treating verification as another optimization extension;
no additional optimization or repeated benchmark was added. Root build and gate
evidence are `i014-root-build.json` and `i014-root-required.json`; cleanup evidence
is `i014-worktree-cleanup.json`. The full incremental compiler goal remains active
and incomplete, including semantic section/reference reuse, tooling and other
output modes, and clean graphical IR/rendered parity.

I014 closes at 07:59 UTC, roughly two minutes past the extended deadline.
The remaining requirement audit is `i014-full-goal-audit.json`. The next named
consumer boundary is P03/P06: unchanged resolved-reference products. Its
implementation must skip reconstruction of unaffected match/binding state, retain
positive and failed lookup dependencies and body-driven parameter changes, and
be consumed by the existing compiler stages. Another unconsumed snapshot or
codec change does not satisfy that goal. Define its work/time gates before code.

### I015: resolved match products and independently reusable effects

Started 08:08 UTC on 14 November; first deadline 08:53 UTC. Baseline is published
`9cbdc95a`, root compiler `092562a6131d285b70f5dddbf3845ecf12849f6601c6e1768a3e19b3be670c7b`.
The previous turn made progress: I014 is merged, measured, validated and cleaned.

P03/P06 hypothesis: an unchanged reference can reuse its resolved match product
without rebuilding the detailed match tree. Binding effects and definition reads
are separate consumers and must remain correct before later inference demands
the detailed match. Existing cache keys and classification/tree proofs determine
eligibility; no new persisted metadata or syntax-specific shortcut is needed.
An equal initial/final cached precedence result can also stop propagation before
another match tree is reconstructed. Persisted products must outlive cache hooks;
destruction and presence checks must never demand unrequested matches.

Add minimal work/runtime regressions before implementation. Require at least 50%
fewer detailed cached match trees materialized on real unused/active edits and a
repeatable 2% paired unused-edit CPU improvement. Keep the common/cold controls,
cross-process clean runtime/diagnostic parity, last-parameter-use removal, overload
and precedence changes, missing products, indirect body activation and ownership
checks. The required suite remains uncached and limited to 120 seconds. One
extension is available only after an implemented, verified reduction; otherwise
park this hypothesis. Binding-group hydration, reference-outline construction,
semantic section reuse and non-native/tooling coverage remain subsequent work;
this iteration does not claim to complete those boundaries.

The first seven-pair screen passes all 36 artifact/diagnostic rows but establishes
no whole-build gain. The independent 31-pair control passes all 132 rows: unused
CPU ratio 0.99007 [0.97795, 0.99917] is below the predeclared 2% gain; active CPU
0.99253 [0.98232, 1.00933] remains inconclusive. Do not accept the candidate yet.
Unused edits defer 6,862 match products and materialize 137, restoring 387 detailed
trees overall; active edits defer 6,819 and materialize 156. Binding/reference
hydration remains broad. Candidate matching falls 12.930 to 1.550 ms in the first
screen, but replay setup rises 7.978 to 13.276 ms. Detailed trie identities are
still validated and captured for every deferred result.

Use the single extension, ending 09:38 UTC, to profile that remaining capture/replay
work and make one measured refinement. Keep the original 2% CPU and work gates;
if the refinement cannot meet them, park the implementation. Preserve both initial
comparisons and do not treat fewer constructed objects as a timing result.

I015 decision: park; do not merge. Refined compiler `8cb03cbc...`, source
`bc520731` with test commit `1287d8cd`, passes all 36 refined parity rows and all
focused body, syntax, classification, precedence-rule and ownership checks.
Seven paired unused-edit samples have CPU ratio 1.00429 [0.97801, 1.03721]; active
CPU ratio 1.00005 [0.99279, 1.02211]. The 2% gate is not demonstrated. Retaining
2,310 precedence equality results and reducing detailed-tree construction does
not establish a sufficient whole-build benefit. No further refinement is added.
All 329 required checks pass uncached in 106.732 s (107.817 s with the observer),
with unchanged compiler and test sources. The full integration suite was not run
for this rejected optimization. Preserve the branch and three compiler identities;
remove its owned worktree after archival verification. Published compiler code
remains `9cbdc95a`. The full goal remains active and incomplete.

Evidence: `i015-first-comparison.json`, `i015-31-comparison.json`,
`i015-refined-comparison.json`, `i015-first-stage-comparison.json`,
`i015-product-profile.json`, `i015-refined-focused.log`,
`i015-refined-full-focused.log`, `i015-refined-ownership.log`, and
`i015-required.json` in the structured evidence directory. The next independent
inspection is classification dependency preparation: 12,015 edges and a global
candidate index are processed after one input/query unit changes. Measure the
actual preparation and invalidation path before selecting that implementation.

I015 closes before its extended deadline. The unmerged branch is preserved at
`54692e8e`; its clean worktree is removed after validating 105 archived data
files, three compiler hashes and the absence of live users. The actual archived
replay passes six rows after removal and cleans its temporary data path. This
is reproducibility evidence, not another performance sample. Evidence:
`i015-worktree-cleanup.json` and `i015-post-cleanup-replay.json`. Only this plan
changes on master; compiler, library and test sources still match `9cbdc95a`.

## I016: classification lookup edges and local definition origins

Started 14 November 2026 at 10:03:16 UTC; first deadline 10:48:16 UTC.
Baseline `94890b9c` / compiler `092562a6...`. The lookup-edge experiment
`07c4d241` / compiler `1dab0ac0...` eliminates 351 candidate checks on a real
unused-function edit, but independent 31-pair CPU ratio 0.997587
[0.978889, 1.015006] fails its predeclared 0.5% gain gate. Both the first
36-row screen and independent 132-row control pass artifact/diagnostic parity.
Preserve the experiment on unmerged `codex/incremental-lookup-inputs`; its
provider/query metadata and edge changes are excluded from the proposed fix.

The experiment exposed a correctness defect: an imported pattern block can
move a local definition between files without changing its text or owning
section identity. Declaration inputs and exported classification signatures
omitted that definition's file origin. A cached caller then kept an implicit
parameter which a fresh build removed. The minimal regression compares the
published parameter classification through both move directions, alongside
clean-build diagnostic/runtime parity. The isolated fix records local origins
for authored and generated declarations. Necessary correctness work is kept
independently of the rejected optimization. Exact source `df66b881` / compiler
`215313d6...` passes 329 required checks uncached in 107.937 s and all 464
integration checks in 844.401 s, with source/compiler hashes unchanged.
The complete eight-case 188-row control matrix passes artifact/diagnostic
parity. Its first active-body CPU result (+2.6%) prompted an independent
31-pair, 66-row control and stage analysis: resolution, optimization and
object work counts match; the repeated CPU ratio is 1.00146
[0.98608, 1.00668], so no repeatable regression is established. No performance
win is claimed for this necessary correctness fix.

The application harness compares baseline/candidate executable bytes and
incremental/clean diagnostics. Focused noninteractive tests also compare runtime
output and inspect the actual retained parameter classification. Normalized
clean-IR or rendered-output parity for the graphical application is still missing;
these controls do not establish that broader requirement.

The single extension ends at 11:33:16 UTC and is used only to close validation,
merge/root verification and cleanup. No extra optimization is added. Root
merge `60aa8d4f` / compiler `86635023...` passes all 329 required checks
uncached in 107.293 s (108.272 s observer), with compiler/source hashes intact.
The merged worktree is removed; an actual six-row archived replay afterward
succeeds and removes its temporary data directory. The full goal remains incomplete.

Next selected boundary: whole-body resolution products. The real unused edit
visits only 38 resolver references but restores 7,342 cached references across
705 bodies; matcher restoration takes 25.802 ms and binding 32.818 ms.
`replayCachedBodies` currently replays every skipped body because later
inference might request a function value. Audit all consumers before moving
that restoration to first demand, including precedence, positive/negative
lookups, capture effects, diagnostics and never-demanded snapshot retention.
I015 already showed that deferring individual match allocations is insufficient.
Name/timebox that next implementation before editing it; require real unit
work reduction and at least 5% paired CPU gain for its larger consumer change.

## I017: demand-driven whole-body resolution products

Started 14 November 2026 at 11:08:12 UTC; first deadline 11:53:12 UTC.
Baseline `13658706` / compiler `86635023...`. Reuse validated body-resolution
products without restoring each match and binding until a semantic consumer
requests that body. Preserve whole-unit dependencies, source/capture effects,
precedence, diagnostics and snapshots for units that remain undemanded.

Add minimal no-library activation/invalidation tests before implementation.
Require actual restoration counters to fall and at least 5% real-program paired
CPU gain for this larger consumer change, minimum seven pairs (31 if uncertain),
no established ordinary/cold regression, required uncached <=120 seconds and
relevant integration. I015 allocation-only and I016 lookup-edge experiments
remain rejected. Audit all consumers before code; park if the full consumer
boundary cannot be completed within the allowed iteration and one extension.


I017 implementation is frozen at C++ source `112c01a5`; compiler
`cd347e3f5b43e590e107af6ca6b19021c1b2671cedef4ae9b544d040dc0dd5aa`.
The complete whole-body consumer boundary includes initial/final resolution and
header products, binding-domain retention, late source binding publication,
validation inputs, inference/cloning demand, and provider-mutation demand.
Two focused activation failures identified consumer bugs in the prototype:
negative lookup keys were omitted from retained body closures, and late
inference demand incorrectly used the normal matcher lookup-stage guard.
Both now have minimal regression coverage. Validated body restoration shares
the result decoder but has its own explicit API; ordinary lookup eligibility
is unchanged. Existing tests now permit a whole proved unit to remain encoded
rather than require reconstructing its matches solely for a work counter.
Stale provider and changed precedence rejection checks remain intact.

The first candidate misses the 5% gain gate. Profiled unused-edit match restore
saves 19.2 ms, but new projection work costs 18.5 ms and replay setup costs
13.4 ms. The single refinement reuses the existing initial/final section indexes
and captures final/header demand products only for statically eligible owners.
No further compiler refinement is made after that measurement.

Frozen real LEXI8 results, CPU seconds (paired alternating order):

| Edit/control | Pairs | Baseline median | Candidate median | Paired ratio and 95% interval |
| --- | ---: | ---: | ---: | --- |
| Unused library body, independent control | 31 | 0.707618 | 0.664129 | 0.93647 [0.92995, 0.94082] |
| Active application body | 7 | 1.579589 | 1.487194 | 0.94136 [0.92517, 0.94507] |
| Cold build | 7 | 5.274928 | 5.245519 | 0.99338 [0.97940, 1.00744] |

The independent unused-edit control demonstrates a 6.35% paired CPU reduction;
active edits demonstrate 5.86%. Cold improvement or regression is not established.
The refined screen has 50 artifact/diagnostic parity rows and the independent
control has 66. They compare exact baseline/candidate native artifact bytes and
incremental/clean diagnostics; they do not prove clean graphical runtime output.
Minimal noninteractive fixtures compare actual incremental/clean runtime output.

Unused edits restore 1,923 instead of 7,342 references and rebind 808 instead
of 3,637 references. Binding groups materialized fall 770 to 166; 342 body
units remain encoded. Median matcher restoration falls 25.451 to 5.566 ms,
binding work 30.080 to 23.866 ms, and resolution 312.658 to 275.363 ms.
These are actual work counters, separate from timing acceptance.

All 329 required checks pass uncached in 103.793 s (104.639 s observer), with
unchanged compiler and test sources and the existing 120-second limit.
Earlier qualification runs failed only old minimum-replay work assertions;
all associated runtime/diagnostic and expanded focused tests pass after their
migration. The full integration qualification is running against the frozen
candidate. No merge or full-goal completion is claimed before that result.

Evidence: `i017-first-comparison.json`, `i017-refined-comparison.json`,
`i017-31-comparison.json`, `i017-final-work-comparison.json`,
`i017-positive-backtrace.log`, `i017-all-replay-focused.log`, and
`i017-required.json` in the structured evidence directory. Source/tests are
`1666b1da`; no compiler or test edits occur during qualification. The original
whole-compiler objective remains active, including persistent section products,
remaining broad projection/dependency preparation, and non-native/tooling paths.

The single extension ends at 12:38:12 UTC. At that boundary the implementation
is parked unmerged, with performance and required gates passed but full
integration qualification still live. Preserve the existing test process and
frozen candidate; no further implementation refinement is authorized by this
iteration. A terminal passing integration result is required before acceptance.


I017 qualification subsequently completed against the same frozen compiler:
all 464 integration checks pass in 824.700 s (825.901 s observer), with unchanged
compiler and test sources. The candidate remains unchanged after the timebox;
only verification continued. A stronger real-application parity check compares
unused-library, active-function and game-state edits with separate empty-cache
native builds of the exact same sources. All fresh-build reuse counters are
zero. Complete LLVM 23 printed modules match in all three cases after removing
only the inspector's input-filename ModuleID comment. Raw bitcode already matches
for the unused edit; active edits differ in binary encoding while their entire
printed IR agrees. The graphical application is never launched.

Decision: accept I017 after the pending qualification is resolved. Both measured
edit cases exceed the predeclared 5% gain; correctness, work counters, cold control,
required budget and integration gates pass. This accepts the bounded optimization,
not the full incremental-compilation objective. Merge to local master, rebuild
there with one job, run the uncached required gate, then push and dispose the owned
worktree after validating the archived compiler replay.

Additional evidence: `i017-integration.json`, `i017-ir-parity-final/report.json`
and its paired `.bc`/`.ll` files. The small inspection utility links the installed
pinned LLVM 23 library; no mismatched external LLVM reader is used. The verifier
uses archived repository cache readers. Its original wrong-checksum inspector
failure is retained separately and did not affect compiler or test source.


I017 is merged as `42e18e4e`. The root compiler
`e1bead02c8b1606834b2aa7ef3b8821cef2dd8d3f591d4b776397eaa40aca3b7`
builds with one job; all 329 required checks pass uncached in 101.113 s
(102.842 s observer), with unchanged compiler and source hashes. The owned
worktree is removed. A real six-row archived-compiler replay passes after
removal, and its temporary compiler data root is removed on exit. The archive
also fingerprints the live external compiler data so future source changes
cannot silently alter a preserved comparison.

Next selected area: P03 section construction, before further low-level tuning.
The 31-pair unused-edit baseline reuses 44/45 source preparations, 79/81 transform
chunks and 7,087 lexical lines. It evaluates 427 lexical lines; it does not lex
all files again. Import work is 6.863 ms and section analysis 14.219 ms, while
resolution remains 275.363 ms. Section analysis still visits the merged line
stream and reconstructs current section objects. Audit a reusable construction
product together with its consumer, distinguishing necessary object rehydration
from repeated source/header decisions. Do not add another serialized-only cache.
Freeze a named hypothesis, minimal tests and an end-to-end gain gate before the
next implementation. The full cross-process incremental objective stays active.


## I018: section construction work and reusable-product decision

Started 14 November 2026 at 13:13:43 UTC; first deadline 13:58:43 UTC.
Baseline `a7e0ddb1`, published compiler `e1bead02...`. The current real unused
edit spends 14.219 ms in section analysis. Cached shorthand and lexical decisions
already exist, so another header cache has an unproven consumer benefit.

Measure section creation, body-line dispatch, finalization, locality processing
and cached source-outline restoration separately. These are stage operations,
not syntax-specific warnings or application-name cases. Keep profiling off the
ordinary no-statistics path. Add focused minimal work/runtime checks before code.
Also check partial-stage timings: the existing stage recorder updates only on
success, so an error currently hides work done in its failing stage.

Implement a construction product only if the profile supports at least a 1%
real unused-edit paired CPU improvement, with reduced repeated construction
work, clean runtime/LLVM parity, no established ordinary/cold regression, and
the uncached required gate within 120 seconds. Minimum seven pairs, 31 when
uncertain. If constructor decisions cannot plausibly meet that gate, reject that
cache before implementation and select the actual remaining source consumer.
Stage observability and accurate failed-stage accounting are separately required
correctness evidence; do not describe them as a speedup. No serialized-only
product, duplicated syntax recognition or unconditional profiling overhead.

The published baseline fails the work test because scoped counters are absent;
the independent two-line unknown-section fixture confirms `parse_ms == 0`.
The candidate adds scoped, statistics-only measurement and records the unfinished
frontend stage on return without advancing its validity boundary. LLVM 23's
deprecated helper was replaced by its documented direct scope-exit constructor.

Use the single additional iteration, ending 14:43:43 UTC, to finish measurement
and the keep/reject decision. The implementation and reduced failure have been
verified against source; core C++ units compile. The shared heavy-job slot was
unavailable during the other chat's build and focused checks (13:27–13:42).
The 1% construction-cache gate is unchanged; observability is not a speedup claim.


I018 decision: reject the header-only construction cache before implementation.
The seven-pair real unused-edit profile evaluates 2,360 headers in 2.728638 ms,
dispatches 4,174 body lines in 9.603195 ms, finalizes in 0.044050 ms and applies
localities in 0.420277 ms. Total section analysis is 13.744796 ms. Nested outline
restoration takes 4.506968 ms and creates 12,007 nodes, 6,308 pattern references
and 85 explicit variable references. Even perfect header elimination is only
about 0.4% of the roughly 0.67-second edited build, below the predeclared 1% gate.
A new serialized header layer cannot justify its cost on this evidence.

Retain accurate stage observability and the independently reproduced failed-stage
timing correction only after qualification. No new cache or speedup is claimed.
The initial three-pair cold screen appeared slower; a separate seven-pair control
has CPU ratio 1.002413 [0.991015, 1.006523], so that regression is not reproduced.
All 42 screen rows and 14 independent cold rows pass native artifact and clean
diagnostic comparisons. The scoped probes are enabled only by requested statistics.

The focused work and two-line failure tests pass. All 329 required checks pass
uncached in 102.843 s (103.807 s observer), with unchanged compiler and test
sources. Full integration qualification is running on frozen source `0f555c31`,
compiler `85694129419a6fa68c51eea23c15cb28284d98285bdab1e51ebfd31af041eed6`.

Evidence: `i018-red.log`, `i018-red-failure.log`, `i018-focused.log`,
`i018-section-profile.json`, `i018-profile-comparison.json`,
`i018-cold-control.json` and `i018-required.json` in the structured evidence
folder. Next inspect section-identity/index preparation and the actual consumers
of cached source outlines; do not repeat the disproved assumption that adding
another header descriptor alone will provide a material whole-build gain.

Full I018 integration qualification passes: 464 checks, zero failures,
820.408 s suite / 821.297 s observer, with unchanged compiler and test sources.
Accept the scoped observability and independently verified failed-stage timing
correction. The header-cache hypothesis remains rejected; this is not a claimed
incremental speedup. Publish only after the merged root build and uncached required
gate pass. No additional implementation or investigation extends this iteration.

Merged I018 root qualification: `a80605e8`, compiler
`cae4985b33551323c3f30c6f30b9cd30911fe8a09f2948b9d2af4c5f2f2218f0`.
The root rebuild passes and all 329 required checks pass uncached in
103.850 s suite / 104.781 s observer, with compiler and test sources unchanged.
The owned merged worktree and branch have been removed.

The preserved benchmark replays the baseline and published root compiler after
worktree removal; all six build rows pass artifact/diagnostic parity and the
temporary data root is removed afterward. This single replay proves archive
reproducibility, not a performance improvement. Evidence: `i018-root-build.json`,
`i018-root-required.json`, `i018-archived-replay.json`. Implementation and the
keep/reject decision were closed within the extended iteration; terminal
qualification publication completed afterward without extending implementation.

## I019: source-input preparation and body-summary reuse

Started 14 November 2026 at 14:48:00 UTC; first deadline 15:33:00 UTC.
Baseline `b85d277d`, published compiler `cae4985b...`. The I018 real unused-edit
median spends 24.278 ms in initial-parameter cache work, 29.407 ms loading
classification inputs, and repeats body digest construction in several semantic
projections. These overlapping measurements do not yet identify the costly call.

First split the existing preparation with debugger probes on the frozen real
application, without changing compiler behavior. Then implement one complete
source-summary producer/consumer boundary if the measured work supports it.
Unchanged source units must skip actual repeated summary work, not merely
serialize another copy. Source transitions, synthetic lines, nested definitions,
changed import membership and configuration must remain explicit dependencies.
Require minimal cascade/parity tests before implementation and counters of real
recomputed/reused work. Keep an optimization only with at least 2% paired real
unused-edit CPU improvement (seven pairs; 31 if uncertain), no established
active/cold regression, clean LLVM/runtime/diagnostic parity and the uncached
required gate below 120 seconds. If the hypothesis fails, preserve the evidence
and move on; do not ship redundant metadata or extend with unrelated work.

The debugger splits the initial preparation into about 0.9 ms indexing,
3.7 ms body digests and 1 ms prior-state selection. The broader 24 ms count-cache
counter also covers 1,534 per-section lookups and recording; it is not all
body hashing. Narrow the experiment to ordered indexing of existing persisted
count records: join sorted section/body/count identities once, then look up
validated records by the current section identity. Preserve candidate-name
checks and every body/syntax dependency. No new serialized product or schema.
Existing minimal count-reuse, nested-edit and last-use cascade tests provide
correctness qualification before the experiment. The 2% real paired gate stays
unchanged; reject this smaller optimization if it fails that gate.

I019 decision: reject the ordered count-record index. The 50-row seven-pair
screen passes every executable/diagnostic comparison, but its unused-edit CPU
ratio 0.980965 [0.973119, 1.006958] is uncertain and near the 2% gate. An
independent 31-pair, 66-row control gives ratio 0.998958
[0.990841, 1.016351], failing that gate. CPU medians are 0.694267 s baseline
and 0.697331 s candidate. A substage reduction did not produce a repeatable
whole-build benefit. Do not merge the experimental C++ change.

The same screen has active-body ratio 0.997699 [0.972675, 1.022400] and
cold ratio 1.020644 [0.992727, 1.025488]; neither establishes an improvement
or regression. These are controls, not a claim that cold performance improved.
The candidate builds successfully and existing minimal count-reuse, nested-edit
and last-use signature-cascade checks pass against clean builds. Since the
optimization is rejected, it has not received a full required/integration gate
and is not a qualified compiler snapshot. Master retains I018's qualified
compiler and 329-check uncached 103.850-second required result.

Preserve experiment source `142b11a9`, compiler
`5e1865496ccbbfcdddc087f3a6fd04f2a9809aa1eaf69640a4153222d0ca09b4`,
raw reports and frozen input data. Evidence: `i019-build.json`,
`i019-baseline-focused.log`, `i019-candidate-focused.log`,
`i019-profile-preparation.json`, `i019-comparison.json` and
`i019-unused-control.json`. Close this iteration without extending it.

Next selected boundary: P14's broad semantic projections. The I018 unused-edit
profile spends 78.019 ms on inactive projection and 43.107 ms on final
projection, while the initial body digest is only about 3.7 ms. First identify
the actual declaration/match/source consumers inside those larger passes with
one-shot debugger probes. Then select a persistent unit and its complete
consumer; do not repeat a small input-index change or add a serialization-only
facade. Keep the full source/configuration, diagnostic and target/tooling goal.

The rejected source remains on `codex/incremental-source-inputs` (`b784f5ef`);
its worktree is removed. An actual six-build replay from the preserved compiler
and data archive passes after removal and cleans its temporary data root. The
one-off I019 profiling cache is pruned; frozen sources, probes and reports remain.
Only this decision/evidence record is published; no experimental compiler code
from I019 reaches master.

## I020: persistent resolved declaration projections

Started 14 November 2026 at 15:18:00 UTC; first deadline 16:03:00 UTC.
Baseline `5a213ad7`, compiler `cae4985b...`. The unused-edit inactive
projection costs about 78 ms; its declaration digest currently reconstructs
structured values for every resolved declaration. The final projection costs
about 43 ms and may repeat or copy those outputs. These are hypotheses until
one-shot debugger probes identify the actual work.

Profile the frozen real application first. Then retain declaration results as
independent units, validate the source/classification/matcher/scope inputs they
read, and make the aggregate consumer use those units. Preserve every provider
and failed-lookup cascade, generated/nested declarations, source identity and
configuration. Avoid another serialized-only facade. Add minimal unit-work and
semantic cascade tests before implementation. Expose actual reused/built unit
counts. Require at least 5% paired real unused-edit CPU improvement (seven pairs,
31 if uncertain), clean LLVM/runtime/diagnostic parity, no established ordinary
or cold regression and the uncached required gate below 120 seconds. Keep the
full source/configuration and target/tooling objective active.

The one-shot profile measures 49.609 ms constructing declaration records,
30.384 ms serializing/hashing their aggregate, and 2.006 ms checking inactive
sources. Implement a shared declaration value stream consumed by structured
JSON and a lightweight fingerprint writer. At the immutable frontend boundary,
audit each resolved input once and retain the prior structured result when its
fingerprint matches. Aggregate ordered path/fingerprint pairs, avoiding the
whole-program JSON tree. Current resolved values include implicit promotions,
choices, argument packs, locality, conversion, precedence and return metadata;
provider changes remain visible through their actual settled results.

Migrate the former pattern-only fingerprint table completely to
`declaration_fingerprints`, including lifecycle codegen records. Existing cached
programs are isolated by compiler build identity; no old-schema reader is added.
Minimal work tests were added first and fail on the baseline's missing counter.
The registry reports changed/reused summaries and actual built/reused structured
records separately; input auditing remains explicit work, not skipped work.

The lifecycle audit corrected the aggregate to include every indexed declaration
unit and added an exact settled-interface check to top-level projection reuse.
The first complete compiler (`eb5d569f`, `2197b42b...`) passes the new ordinary
body-edit and last-parameter-use checks but fails a minimal failed-build reuse
check. Root cause: the registry received the last native program, excluding
healthy declaration results from a newer failed typed build. The existing
frontend-state selector already preserves and hydrates that state. Connect the
registry to that selected state; retain the old native snapshot separately for
IR validation. The new three-line typed-failure regression precedes this fix.

Focused semantic verification finds an obsolete work-counter assertion: the
older parameter-cascade test required zero reused declaration records whenever
one provider changed. A private inspection retained every original declaration,
caller-match, runtime and diagnostic assertion and passed them all: exactly one
provider record changes, while the caller's declaration record remains equal
and its match changes. Counters confirm one built and one reused record.
Migrate that assertion to require exactly the affected declaration to rebuild,
keeping every semantic assertion. This strengthens unit isolation rather than
weakening the cascade. The prematurely started timing screen was interrupted
and excluded; focused validation must pass before a replacement screen starts.

The 50-row identified screen passes artifact/diagnostic parity and meets the
unused-edit gate: seven paired CPU ratio 0.917397 [0.911704, 0.946414],
medians 0.680592 to 0.622536 s. Active body ratio 1.004385
[1.001633, 1.010395] shows a small slowdown; cold 1.008166
[0.992690, 1.018085] is inconclusive. Stage analysis identifies 13.921 ms of
eager declaration input auditing even when the ordinary eligibility guard
rejects inactive reuse in 0.094 ms. The unused path instead replaces 78.848 ms
with 14.571 ms input work plus 2.859 ms inactive checking.

Use the single additional iteration, ending 16:48:00 UTC, for demand-driven
input auditing and qualification. Delay the declaration provider until inactive
source eligibility succeeds. A source-proof path may retain unaudited inputs
only with identical unit membership, unchanged initial/final matcher trees,
scope rules and global bindings, unchanged sources outside function bodies,
and explicit settled-signature checks for each changed ordinary body. Other
cases audit current resolved inputs normally. Counters distinguish actually
checked inputs from retained units. Keep the original 5% gate and ordinary/cold
controls; do not add unrelated optimizations or a second extension.

Demand-driven refinement `4926a9bb` / compiler `fc7d2442...` builds and
passes the exact focused cascade/failure checks. A minimal body edit now audits
one declaration input; the preceding eager binary fails that assertion. Retain
that red evidence and the new check. No performance acceptance is claimed until
the refined binary repeats the paired screen and qualification gates.

The refined screen has unused CPU ratio 0.920070 [0.903976, 0.923334],
0.664523 to 0.608756 s medians. Real active-body edits now audit one input,
retain 1,750 summaries and build no records. The independent 31-pair controls
(128 total rows) show active-body CPU ratio 0.994811
[0.989778, 0.997006] and cold 1.003667 [0.996719, 1.016764]; no cold
regression is established. All artifact/diagnostic comparisons pass.

The configuration audit adds an explicit syntax-profile dependency to the
source-only proof. A minimal long-form function remains semantically equal
when an unused action shorthand is renamed in config.dl, but the prior binary
incorrectly skips checking its declaration input under that changed parser
profile. The new regression reproduces that omitted input. Reuse the existing
compatible-observations result for this proof; current resolved fingerprints can
still preserve the unchanged output record after auditing. Wording-only edits
remain on the existing no-frontend fast path. This is the final source change
before full qualification; no further optimization extends this iteration.

I020 qualification: accept the declaration projection producer/consumer change.
Final frozen source `8edd89d1`, compiler
`1aed552b4c667a70aa28a0b36d2053c2e580fe5216f59319ebd77bfc346f65ff`.
The final seven-pair screen (50 build rows) gives unused-edit CPU ratio
0.899477 [0.892954, 0.931892], medians 0.681789 to 0.611354 s;
active-body ratio 0.982816 [0.947035, 0.999603], medians 1.495829
to 1.476218 s; cold ratio 1.005481 [0.998692, 1.017861], medians
5.246041 to 5.261879 s. No cold regression is established. The independent
31-pair body/cold controls preceded the final syntax-profile guard; they are
recorded separately rather than described as measurements of the final binary.

Every final screen row passes executable/diagnostic comparisons. Full fresh
versus incremental LLVM text and diagnostics also match for all three real
edits (unused library body, active renderer body and emulator lives constant).
Only the inspector-input ModuleID comment is removed; all fresh semantic reuse
counters, including declaration reuse, are zero. Retain the nine build reports
and complete .bc/.ll intermediates; transient parity caches have been pruned.

All 329 required checks pass uncached in 102.581 s suite / 103.873 s
observer. All 464 integration checks pass in 811.030 s suite / 811.433 s
observer. Compiler and test sources remain unchanged across both gates.
Implementation closed on the frozen source; terminal qualification completed
after the extended timebox without introducing another optimization. Publish
only after merged root build and required qualification. The full incremental
compiler objective remains incomplete.

Evidence: `i020-final-focused.log`, `i020-final-comparison.json`,
`i020-ordinary-controls.json`, `i020-ir-parity-final/report.json`,
`i020-required.json`, `i020-integration.json`. Intermediate eager/refined
compilers, rejected/incomplete timing data and minimal red tests remain named
separately. Temporary benchmark compiler slots use same-filesystem hardlinks
to immutable archived binaries, verified by hashes before/after, to avoid
400 MiB of redundant staging copies; shared runtime inputs remain pinned.

Next inspect the actual consumers and persistence costs of full declaration
records. The new fingerprints may let equality/membership consumers avoid
decoding, copying and encoding those structured values at all. Verify every
reader before choosing that next bounded unit; do not add another storage
layer without a measured consumer. Source section reconstruction, broad
validation/setup, debug/trace reuse and tooling/output-mode coverage still
require work under the full objective.

I020 publication: merged root `f9ac899f`, compiler
`33a30814ef55dbcf9f10f5f481225df4e74293ae043af98c8e714de96b440a60`,
passes all 329 required checks uncached in 103.048 s suite / 104.007 s
observer, with compiler and test sources unchanged. The merged worktree and
branch are removed after verifying no process uses them. A partial archive copy
failed for lack of disk space; it was removed before retrying successfully.
The published compiler is frozen independently of the mutable root build.

An actual six-row archived baseline/published replay completes after worktree
removal: executable and diagnostic comparisons pass, compiler hashes remain
unchanged, and the temporary compiler-data path is absent afterward. This is
a reproducibility check, not a new performance estimate. Root proof and replay
evidence are `i020-root-build.json`, `i020-root-required.json`, and
`i020-archived-replay.json`. The full objective remains active and incomplete.

## I021: consume resolved declaration fingerprints without full records

Start: 2026-11-14 17:15 UTC. First deadline: 18:00 UTC; no extension
used. Baseline `cf78161d`, published compiler `33a30814...`. The shared
heavy-job slot belongs to the UI cache-fix rebuild; prepare without a concurrent
C++ build or benchmark and record any verified wait separately.

Verified consumer audit: runtime readers of `function_declarations` use only
key membership, counts, equality, and a hash input. Settled declaration values
already have complete typed fingerprints. `matches` records precisely the
reusable-section membership; `declaration_fingerprints` also includes flex and
lifecycle declarations, so substituting that larger key set alone is incorrect.
Keep membership checks tied to reusable units and preserve all existing
source, matching, inference and backend dependencies.

Hypothesis: removing unused full declaration records from persistent function
projections reduces decoding, copying and serialization on ordinary small edits.
Use fingerprints as the equality/input product and existing match membership as
the reusable-unit set. Remove the unconsumed record registry/counters and migrate
all test inspectors before running them; inspect classification's actual output
units for promotion assertions. Do not add a second metadata table solely for
tests or retain a compatibility field. Compiler-build identities isolate cache
schemas. Other semantic uses of structured section declarations remain.

Keep only with at least 3% paired CPU gain in active or unused real edits,
seven ordinary pairs (31 if uncertain), fresh full-LLVM/diagnostic parity,
no established ordinary/cold regression, all required checks uncached under
120 seconds, and affected integration coverage. First add a minimal persistence
regression and retain its baseline failure. If migrating inspectors reveals a
real semantic consumer, reassess before changing the schema.

The baseline fails the new persistence regression as expected (`i021-red.log`).
All migrated classification inspectors pass independently on that same published
compiler (`i021-inspectors-baseline.log`): recursive parameter retraction,
transitive propagation, nested lookups and file-local definition moves retain
their assertions. Outputs are read from the real classification topology and
its CBOR matcher units. In that schema whitespace is a leaf, so `value` is
element 2 in `alpha value`; the minimal probe verifies that position against
the baseline's full declaration before removing it.

I021 first screen completes against frozen source `bef73008`, compiler
`2d89d8a28b72d4cc5d164581256e9a42f48fbcee945e45951e9d583cc8a76936`.
All 50 rows pass artifact/diagnostic parity. Seven unused-edit pairs give CPU
ratio 0.957637 [0.914705, 0.984821], medians 0.644339 to 0.603837 s.
Active-body ratio is 0.984203 [0.942355, 0.993631]; cold is 0.997839
[0.972706, 1.006951], with no established cold change. Wall time for unused
edits remains noisy. Cache payload load medians fall 26.77 to 20.51 ms for
unused edits and 74.62 to 63.28 ms for active edits; writing falls 35.38
to 30.24 ms and 104.20 to 98.67 ms respectively. These scoped timings nest;
do not sum them. Active edits still audit one declaration input and retain
1,750 fingerprints.

Use the single additional iteration, ending 18:45 UTC, to confirm the near-
threshold unused gain with 31 independent pairs and finish LLVM parity plus
uncached required/integration qualification. The ordinary source change is
closed; no unrelated optimization extends this iteration. UI resource sharing
and the cold C++ build consumed much of the first timebox. Record any failed
gate without relabeling it as a pass. The full incremental goal remains active.

The independent 31-pair confirmation (66 rows) clears the 3% target across
the interval: CPU ratio 0.955410 [0.946650, 0.969349], medians
0.654363 to 0.623757 s. Wall ratio is 0.958225
[0.932984, 0.977585]. All executable and diagnostic comparisons pass;
compiler hashes remain unchanged. No extra performance run is needed.

Full fresh/incremental LLVM text and diagnostics match in all nine builds
covering unused/body/lives edits; fresh reuse counters are zero. All 329
required checks pass uncached in 105.395 s suite / 105.870 s observer, with
compiler and test sources unchanged. The six completed parity caches were
removed after a live-process audit; .bc/.ll files, application sources and
reports remain. Integration and merged-root qualification are still pending.

Evidence: `i021-comparison.json`, `i021-confirmation.json`,
`i021-stage-comparison.json`, `i021-ir-parity-final/report.json`,
`i021-focused.log`, `i021-inspectors-candidate.log`, `i021-required.json`.
The full incremental compiler objective remains active and incomplete.

I021 qualification: accept the fingerprint-only persistence/consumer change.
The full integration retry passes all 464 checks in 789.208 s suite /
790.495 s observer, with compiler and test sources unchanged. The earlier
integration command ended with external SIGTERM/143 before its observer could
write a report. It is preserved as incomplete (`i021-integration-interrupted.*`)
and is not counted as passing. Available logs did not identify that sender.
The retry observer records signal senders; it received no termination signal.
No compiler code, test expectation or timeout changed for the retry.

The performance gate is met by the independent 31 pairs; required and full
integration gates pass on the exact frozen compiler. This closes implementation
and candidate qualification within the single extension. Merged-root rebuild,
required verification and archive replay remain publication work. No further
optimization extends I021. Full-goal gaps remain: semantic section construction,
broad matching/binding/validation setup, debug/trace reuse and tooling/other
output modes. Next return to P03's immutable parser products, auditing factory
effects before caching them: definition header dispatch mutates owner flags,
and PatternDefinition construction itself does not parse its element syntax.

Final candidate evidence: `i021-integration-retry.json`,
`i021-integration-retry.log`, `i021-confirmation.json`,
`i021-required.json`, `i021-ir-parity-final/report.json`.

I021 publication proof: merged root `2091bb9f`, compiler
`7657790ea96f5ce13c90ec96f0b8fbb9822ccca0c33c8259deba3bef1a494c90`,
passes all 329 required checks uncached in 107.682 s suite / 108.646 s
observer, with compiler and test sources unchanged. The first root test-observer
invocation omitted its suite argument and stopped before launching tests; its
corrected invocation passes. Root rebuild and qualification make no source change.

The accepted worktree and branch are removed after a live-process audit. An actual
six-row comparison runs the frozen measured development binary against the frozen
root binary after removal: all executable/diagnostic comparisons pass, both binary
hashes remain unchanged, and the temporary compiler-data directory is removed.
This verifies archival replay and root parity, not a new performance estimate.
Evidence: `i021-root-build.json`, `i021-root-required.json`,
`i021-archived-replay.json`. Candidate acceptance closed within the single
extension; root publication closure continued afterward without another optimization.
The full incremental compiler goal remains active and incomplete.


## I022: matcher record directories after source-consumer screening

P03 screening began 14 November 2026 at 19:04 UTC. The implementation
timebox was recorded at selection, 19:30 UTC, with a 20:15 UTC deadline.
The full audit-to-decision interval ended at 19:53 UTC; count the 31-pair
confirmation as its single extension rather than excluding the earlier audit.
Baseline master `33ee096a`, compiler `7657790e...`. The preceding P03 audit
found declaration-element products already cached. On the frozen LEXI8 unused
edit, three debugger observations isolate eager reference tokenization at
5.74–7.10 ms CPU and eager binding-domain proof construction at 1.26–2.14 ms.
Reject both speculative container/proof changes before implementation. The
initial wider tokenization probe included initial count setup; the corrected
probe stops before that call. Debugger wall time is not a benchmark estimate.
Evidence: `i022-reference-profile/token-profile.json`, `binding-profile.json`.

One P14 producer/consumer goal: store sorted matcher-record directories and look
up demanded record spans directly, eliminating the broad `previousEntries` hash
map built whenever a bucket is loaded. Preserve all key/value bytes, partial
retention, eviction, corruption errors and initial/final body products. Migrate
the sole segment inspector with the new format; no runtime compatibility path.
Add the format/work regression first and verify red against the frozen baseline.
Keep only if paired real unused-edit CPU improves by at least 2% (seven pairs;
31 if uncertain), with no established active/cold regression, clean LLVM and
diagnostic parity, and the uncached required suite within 120 seconds. Full
section persistence, tooling and every-stage incremental coverage remain open.


I022 decision: reject. The exact frozen candidate `086b69ab...`, source
`48f3484c`, builds successfully and passes focused format, replay, untouched
segment retention, partial eviction, corruption, binding and signature-cascade
checks against clean builds. Seven unused-edit pairs suggested CPU ratio
0.968870 [0.954989, 1.013735], requiring confirmation. Independent 31 pairs
produce ratio 1.002023 [0.972789, 1.011769], CPU medians 0.619357 versus
0.621298 seconds; wall medians 0.634228 versus 0.627282 seconds, paired ratio
0.992964 [0.963312, 1.002465]. This fails the 2% CPU gate. A lower substage
load cost does not establish a whole-build gain. Do not merge the C++ or the
new-format inspector.

The seven-pair active-body CPU ratio is 0.989956 [0.975851, 0.998523]; cold
ratio is 0.994844 [0.982791, 1.020039]. These controls do not override the
predeclared target. All 50 screening rows and 66 confirmation rows complete,
with identical executables/diagnostics and unchanged frozen compilers. The
unused-edit checks also prove zero object builds and no link. No full required
or integration qualification is claimed for this rejected prototype.

Evidence: `i022-build.json`, `i022-baseline-red.log`, `i022-focused.log`,
`i022-comparison-index.json`, three `i022-*-comparison.json` files,
`i022-confirmation.json`, `i022-stage-confirmation.json`. Preserve the source
branch and immutable compiler/data archive. The completed one-off profiling
cache was pruned; source, debugger probes and reports remain. The complete
screening interval includes the intervening direct-question turn.

Next bounded correctness goal: import and qualify the already reduced resolver
retry fix from the UI agent's `b265ca5f`. Its six-line, library-free regression
activates an unchanged function with a local binding after a cached build.
Definition reclassification reopens the resolver, but the current one-shot
unmatched-variable wakeup cannot run again. Keep this distinct from the
remaining named-global dependency and diagnostic-flow fixes on that branch.
Then resume source-owner deferral across classification and body replay; a
header-only cache and a lazy token vector remain rejected hypotheses.


## I023: reopen unmatched-variable eligibility after reclassification

Started 14 November 2026 at 19:55 UTC; first deadline 20:40 UTC.
One correctness goal: when cached function activation reopens definition
resolution, retry unmatched local-variable references each time definitions
finish again. Port the UI agent's six-line, import-free regression first, prove
fresh success and cached failure on the published baseline, then import only
the two-file resolver state-transition fix from `b265ca5f`. Repeated activation,
changed input and deactivation must agree with clean builds. Run the focused
body/classification regressions, uncached required suite and relevant integration
coverage before publication. This does not change the rejected I022 format or
complete source-owner deferral. Other UI inference/diagnostic fixes stay separate.


I023 qualification progress: the published baseline fails the new cache-only
activation case after its clean build succeeds. GDB through `scripts/debug.sh`
records the definition-complete wakeup flag as false, true, true; later
completion events return without waking blocked names. The committed fix tracks
current eligibility and retracts it while reopened definitions are pending.
Candidate source `d0ec97c4`, compiler `322cd31d...`, passes the new repeated
activation/value/deactivation test, its cached-syntax/reactivation work assertions,
whole-body reuse cases and implicit-parameter cascades. All 329 required checks
pass uncached in 108.166 s (109.173 s observer), with source and executable
unchanged during the gate. This is not full qualification or publication: full
integration and real-program LLVM/diagnostic parity are still pending.

Evidence: `i023-baseline-red.log`, `i023-repro/gdb-guard.log`,
`i023-build.json`, `i023-focused.log`, `i023-regressions.log`,
`i023-required.json` and `i023-required.log`. The heavy-job slot was released
after the required gate for the UI agent's queued browser rebuild. Only I022's
rejection documentation has reached master (`d5b5eb0c`); its experimental cache
format and inspector remain on the archived, unmerged source branch.


I023 candidate qualification is complete: full integration passes all 464 checks
with zero failures/skips in 819.926 s suite / 821.110 s observer. Compiler
`322cd31d...` and source remain unchanged. The inherited observer names its
files `i023-integration-retry.*`; this is the first and only I023 integration
attempt. All nine real LEXI8 builds also pass complete normalized LLVM-text and
diagnostic parity for unused-body, active-body and game-data edits. Fresh builds
report zero reused products; source/IR/reports remain after six completed caches
were pruned. Root merge/rebuild/required qualification and publication are next.

A separate correctness screen confirms the next dependency hole on this same
root-derived compiler: changing a named global's initializer provider from 42
to 43 leaves incremental output at 42 while a clean build prints 43. The reader
unit records only its own source and one global type input, with no execution
callee or value query. Entry inference reruns, but one instantiation and two pure
results are reused. This identifies a missing producer dependency in inference,
not an IR-only cache issue. Port the existing UI fix with a minimal required
regression before resuming source-owner deferral. Evidence:
`i024-global-initializer-screen/{report,dependency-screen,screen-summary}.json`.
The first copied UI shorthand was unsupported by this baseline; the verified
repro uses ordinary long-form function syntax and no imports.


I023 root qualification: local master `a49ebcf0`, rebuilt compiler
`d7ca3497...`, passes all 329 required checks uncached in 108.502 s suite /
109.768 s observer. Its code/tests match qualified candidate `d0ec97c4`;
source and executable remain unchanged during the root gate. The Root C++ build
passes in 28.568 s with one job. A frozen copy of the published executable is
added to the replay manifest. Publish the documentation/fix, remove the merged
worktree/branch, then prove replay from the archived binary and data paths.
Evidence: `i023-root-build.json`, `i023-root-required.json` and their logs.
The global-initializer screen remains an unresolved next correctness task;
this iteration does not complete source-unit deferral or the full goal.


I023 publication closes at 20:39 UTC, within its first 45-minute timebox.
Snapshot `5f11c2aa` is pushed to origin/master. The merged worktree and its
branch are removed. An actual six-build archived replay passes after removal,
comparing frozen development compiler `322cd31d...` with published root compiler
`d7ca3497...`; executables and diagnostics agree, both binary hashes stay fixed,
and the temporary 105-file compiler data root is removed. This replay proves
reproducibility, not a new performance estimate. No runtime code or test changes
are added after root qualification. Evidence: `i023-archived-replay.{json,log}`.
The full compiler-wide goal remains active and incomplete.


## I024: named global initializer execution dependencies

The first read-only screen began 14 November 2026 at 20:22 UTC during I023's
integration wait. Count that preflight in this iteration: first deadline 21:07
UTC, with one extension available only after concrete progress. Implementation
starts from published master `ec8fab93`, compiler `d7ca3497...`, in a fresh
project worktree.

One correctness goal: a cached reader of named global data must retain the
execution that initialized its value, including values known only during pure
execution. A provider-body edit currently prints 42 incrementally and 43 fresh;
the reader's stored unit reads itself and a global type, with no producer
execution/value dependency. Add a minimal no-library required regression before
the fix, covering repeated provider edits, changed initializer arguments, rename,
unchanged repetition and reversion at O0/O2. Confirm red on the published root.

Port the relevant producer-read and optional-scope API changes from UI commits
`10872562` and `b265ca5f`, preserving the distinction between source-only reads,
function execution and entry execution through pure forwarding. Migrate every
producer/consumer of that API. Do not include unrelated UI behavior or add a
compatibility reader. The separately found C++23 shared-ownership API removal
will be corrected with its standard equivalent and reported explicitly.

Acceptance: every edited result and diagnostic matches clean builds; repeated
input still bypasses semantic/code generation/link work; existing independent
unit and configuration tests continue to pass; all required checks remain
uncached within 120 seconds. Run relevant integration and complete real-program
LLVM parity, recording any measured cost rather than calling a correctness
repair a performance gain. Then qualify/publish root and remove the worktree.
Preflight evidence is in `i024-global-initializer-screen/`; no optimization is
claimed from its single observations.


I024's one extension is reserved through 21:52 UTC. Concrete progress: the
minimal required O0 test fails on published root with cached 42 versus clean 43;
source/graph inspection identifies the missing producer execution, and commit
`e5a24ee4` migrates every dependency callback and pure forwarding site. Ordinary
instantiations allocate storage identities; entry execution alone uses an
engaged null identity. The candidate is not yet built or accepted. The UI
browser compiler's live CMake/Ninja job (453043/453044) and wasm-opt child
454233 occupy the shared heavy slot. Queue the native build without overlapping
it; use the extension only to finish this correctness goal and its qualification.
