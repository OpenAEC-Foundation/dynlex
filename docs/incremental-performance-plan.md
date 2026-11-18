# Incremental compiler performance plan

Owner: this compiler task. Established 13 November 2026.

Current tradeoff policy: prioritize speed, then memory, disk space and code size,
while judging the combined result rather than treating that order as absolute.
A worthwhile disk reduction can justify a small slowdown; extra memory needs a
commensurate speed benefit. Keep small, clearly positive changes promptly and
move on to larger opportunities. Percentage floors in historical experiments
are historical decisions, not current acceptance requirements. Measure combined
changes end to end rather than adding overlapping stage savings.

## Qualified snapshot I119

Native eaaf1cc6 passes all 391 required checks in 63.588 seconds after the compiler build. Native LSP, focused grouping and source checks, rebuilt browser host LSP, and all 487 integration checks pass. This snapshot includes fixed grouping, physical source-line proofs, early diagnostic replay and native object output. The overall goal remains active.

## Iteration I118: replay source-loading and preprocessing diagnostics

Unchanged early failures and message-only edits now validate source and
configuration observations and render the diagnostic without importing source
lines. Missing candidates remain live inputs. A temporary import failure
preserves the last completed semantic products, allowing repaired sources to
reuse unaffected constraint results. The focused CR, lexical-error, missing
import, repair and prior constraint checks pass at O0 and O2 on 1f17764b.

On the archived 45-file Breakout application with a missing trailing import,
nine pairs of wording edits match clean diagnostics and leave no executable.
Replay skips restoring 7,023 prepared source lines, with zero import-stage or
code-generation work. Median CPU time drops from 15.219 to 12.547 ms, including
source observation checks. The implementation is a4631b66. Combine the verified
grouping proof, source correctness fixes and object output for production gates.

## Iteration I117: correct physical source line reuse proofs

The published compiler reproduces three independent failures. An executable
edit following a CR-only comment can reuse an old executable. A mixed-line
ending file can mask a changed top-level statement with an inactive function's
body even when the frontend runs. A reserved argument byte added to a comment
can bypass the source loader's diagnostic. Minimal no-import examples establish
wrong output or diagnostics against clean invocations.

The correction uses the source file's physical line index for semantic digests,
diagnostic line ordinals and source projections. It retains comment text when
trimming would hide a reserved argument byte. The targeted source preparation
and checkpoint checks pass at O0 and O2 on the corrected image fe1b0a8a. A CR-only
comment insertion moves a warning and its notes to the clean-build locations
with no codegen. The source commit b8b4c9d3 affects native incremental builds.
Production suite verification still follows.

## Iteration I115: prove a unique operand layout before retaining grouping state

The focused preinference reproduction changes a wrapper's integer literal.
Before the proof, an earlier real-argument call establishes a grouping input
and the first edit re-infers an unchanged integer helper. A chain of prefixes
or a chain of suffixes has one nesting order. Record grouping reads only where
matched operand edges admit exchanges. Existing grouping-state tests now use
mixed operators where they depend on genuinely different candidate layouts.
Those cases pass on the baseline and the candidate.

The isolated proof at ae8a9f82 passes the full focused grouping-state check.
The 45-file Breakout comparison uses the shared compiler from 3d5ee1ad and
separate caches. All five cases pass output and diagnostic parity. The first
body edit drops from 2.441 to 1.699 CPU seconds and from 32 to 10 inferred
bodies. Entry inference is reused, and measured inference drops from 966 to
475 ms. Across three successive edits the baseline infers 32, 11 and 1
bodies. The candidate infers 10, 1 and 1. The corresponding median CPU times
are 1.590 and 1.392 seconds. Cold performance is unresolved with three pairs.
Wording updates keep zero code generation and have medians of 48 and 52
CPU milliseconds in this run. The work saved on the first real code edit
justifies full production qualification of the proof. The remaining ten
body passes and the broader dependency-locality objective stay open.

The shared I113 snapshot was pushed as 3d5ee1ad after bounding Git's pack
workers and window memory. The first push was killed at the publication
unit's 256 MiB cap after all local gates had passed. A new owner worktree
retains the verified build data. Its production I115 build plan recompiles
the inference translation unit and relinks.

The production grouping image d3e428f8 passes all 391 required checks in 96.096 seconds, native LSP and the focused checks, the rebuilt browser's host LSP test and all 487 integration checks in 1164.512 seconds.

## Completed iteration: I113 integrate and publish the corrected compiler

Started18 November at12:05 UTC; implementation deadline12:50 UTC. The previous
goal turn answered the user's timing question and verified the live integration
job. This continuation completes I112's487/0/0 integration and eight-build
resource comparison, commits the qualified source/browser artifacts as61c92732,
and integrates local master8674e44f as220e2575. Preserve its literal-priority
matcher fix, portability fixes and complete required-suite worker coverage.
Resolve the sole test-driver conflict by running grouping-state regressions in
the native inference group and retaining the inference-failure check exactly
once. No test or assertion is dropped.

Freeze the combined sources; build native and actual WebAssembly serially, run
focused regressions, all391 required checks uncached within120s, full LSP,
real-program parity/cost controls and all487 integration checks. Refresh the
shipped browser artifacts from the combined source build. Then integrate with
local master, rebuild/verify the shared-root compiler, push the qualified
snapshot and dispose the merged worktree while retaining verified build data.
Do not add another optimization inside this qualification boundary or label the
correctness repair a performance gain. The full every-stage goal remains active.

Combined native image0d28feda builds successfully under the serial memory limit
and passes the focused grouping/dependency checks in33.073s, all391 required
checks uncached in67.745s and the full LSP suite in6.886s. The real-program
controls, combined browser build and full integration remain outstanding;
earlier846c0a1a results are not attributed to this newer image.

The combined real-program matrix now passes exact diagnostics and executable
parity in all five cases. Successive body edits still infer32,11,1 bodies; the
first matched inference stage is933.565ms versus261.717ms on master370e530a's
qualified baseline. The actual browser build completes in362.921s and its host
LSP edit/parity regression passes in2.742s. Full487-check integration is live.
The first-edit debugger probe and guarded shared-root publication pipeline wait
for that exact qualification job, then run serially. The debugger checks actual
inference rejections rather than borrowing counts from an earlier image, and
removes its disposable source/cache after retaining the report. Publication
requires frozen source/browser hashes, a clean tracked root, reviewed mainline
integration and successful root required/LSP/focused checks before push/disposal.
Master1a9f9a90 adds test portability and a Windows-only UTF-8 process manifest;
its exact diff leaves compiler C++, browser transport, libraries and config
unchanged. The manifest is not a browser embedded-library input. Preserve the
completed compiler qualification, verify those production hashes after merging,
and run the updated required/LSP/focused checks on the shared-root build. Do not
restart the live integration run for this test/build-metadata-only change.

Integration stopped at 486 passes and one failure after 1160.048 seconds.
The completion visibility fixture aborts at the cursor following
`set {same line value} to 1 and add 2 to `. Both the combined compiler and
the compiler built from local master reproduce the same invariant failure.
The debugger reaches the completion frontier's ancestor walk. That walk
canonicalizes the current progress only, then reads older parent groups
directly. The shared matcher moves a merged group's values to its canonical
group, so an older continuation can legitimately reference an empty redirect.
Fix ancestor access through the canonical group and share it with matcher
debug rendering. Preserve the existing completion regression and add a
no-import reproduction. Publication waits for the corrected build and tests.

Local master41093e7f independently fixes the same ancestor access and prepares
required test tools before the suite timer. Its original suite passes391/0/0
required and487/0/0 integration checks. The incremental worktree integrates
that exact compiler fix at3dffa73e and adds a16-line no-import regression. The
regression aborts before the fix and verifies the earlier variable remains a
completion candidate after it. The corrected native image861c29d7 passes that
test, the original completion fixture and the focused grouping/dependency tests.
A serial validation job owns the remaining required, WebAssembly and integration
checks. The earlier486/1/0 is retained as failure evidence.

Independent first-edit progress: an eight-line no-import program with an
earlier real-argument call and a later integer-argument wrapper exhibits5,2,1
body passes across population and two literal edits. Both edits can reuse
entry execution. The first edit changes the unchanged integer helper's saved
grouping transition from checked-to-checked to absent-to-checked, with identical
operand layout. Preinference begins before the earlier real call establishes
the grouping. This is observable extra inference in the unchanged helper,
independent of the completion crash. The real-program attribution records the
phase and owning query before candidate cache state is removed.

The repaired snapshot passes391 required checks uncached in74.707 seconds and
the full native LSP suite. The required job uses254.005 CPU seconds including
its children. Parallel workers account for the difference between CPU and wall
time. The browser rebuild and WebAssembly LSP checks pass. Full integration is
running on the repaired image, followed by the shared-root qualification.

The real first-edit trace on861c29d7 confirms32 body passes and entry inference.
It measures240 grouping checks, with17 rejections during speculative inference
and15 during ordinary inference. The mismatched reads include172 absent current
layouts,241 absent saved inputs and53 unavailable prerequisite instances. These
counts describe reads within rejected products, so one rejection can account for
many mismatches. The matched cache population stores7.75MB of grouping data
within8.41MB of concrete summaries, including3.21MB of scope identity strings
across3336 uses. The trace preserves evidence and removes the temporary cache.

The next bounded experiment uses short identities for grouping scope references.
It preserves the exact grouping and availability checks. The prototypeba25bd04
keeps complete typed storage origins for their actual consumers and interns
grouping-only scopes by the same stable digest. Its serial benchmark queue runs
after the repaired snapshot publishes. Measure cold and edited CPU time, peak
RSS and cache bytes with exact diagnostics and output parity before retaining it.
This addresses storage and restoration costs. Causal inference of changed bodies
in their original grouping context is a separate unresolved boundary.

An isolated cache-maintenance probe holds the program and output constant while
adding unrelated entry names. Across nine alternating samples per size, 15,
2015 and6015 entries take0.066,4.442 and13.094 ms to scan, with corresponding
process CPU medians6.201,10.738 and19.414 ms. Every invocation reuses the
frontend and does zero object or link work. The real-program first edit scans
3479 entries in12.43 ms, so removing that scan cannot account for the observed
inference regression. Existing eviction checks deliberately add entries between
unchanged builds. Retain that behavior and avoid treating the historical
whole-suite cache-growth correlation as a measured stage breakdown.

Master5bf9accf updates test progress and timeout regression reporting only.
The reviewed merge preserves exact compiler and browser inputs. Verify the
updated required checks after merging without discarding the active integration
result for this test-driver change.
Master7fd18a2c also normalizes CRLF output in an existing LLVM-output check.
Both test-only updates are included in the shared-root verification.

Combined qualification completes: all391 required checks pass uncached in74.707s, full native LSP and focused grouping checks pass, including the reduced no-import completion regression, and the rebuilt browser compiler passes host LSP and all487 integration checks in1007.440s. Exact source, compiler and browser hashes are retained in i113-repaired-inputs.json and i113-repaired-validation.json. This qualifies the combined worktree snapshot; shared-root verification and publication follow. The full objective remains incomplete.

Shared-root qualification also passes after a fresh serial C++ rebuild: all391 required checks uncached in68.311s, full LSP and the focused grouping/dependency/integration-smoke checks. Root image isa13171600b8be98083d3d4e9db7e353edc8453c796ebe4b4f639587225ea0692; owner image is861c29d77b6437ddabbd02582a574d726020e4fee93f0c31bed34abc0a9dae5a. Compiler and library sources match the qualified worktree. The shared-root run verifies the reviewed suite-reporting changes. The first-edit inference locality issue and the broader every-stage goal remain open.

## Completed iteration: I112 restore the browser's shared matcher implementation

Started18 November at11:33:26 UTC; deadline12:18:26 UTC. I111's integrated
native image846c0a1a passes389 required checks uncached in72.950 s, full LSP,
focused regressions and the complete real-program parity matrix. Its actual
WebAssembly build fails to link because local master excludes
incrementalMatcherCache.cpp even though incrementalAnalysis.cpp uses its
in-memory constructor, reuse, record and snapshot methods. The implementation
uses supplied byte loaders and has no native cache-entry/file dependency.
Remove that source exclusion while keeping the native-only cache-entry driver
excluded. Do not disable browser matcher reuse or supply stubs. The red link
and complete compiler command are retained in i111-merged-web-build.log.

The project suppresses automatic CMake regeneration; a first build retry kept
the old source list and repeats the link failure. Explicit reconfiguration then
builds the shared matcher implementation and links successfully. The native
reconfiguration reports "ninja: no work to do" and its binary SHA remains
846c0a1a, preserving its exact native qualification. Record that equivalence and
the updated CMake input in i112-frozen-inputs.json. Actual build receipt,
source hashes and module hashes are in i112-web-build-receipt.json.

The fresh WebAssembly module passes the complete host LSP edit/parity regression.
Both generated browser artifacts are refreshed from this build; the unchanged
translator retains its qualified master inputs. The full487-check integration
suite passes487/0/0 in1083.106s against this native compiler and these actual
shipped browser artifacts. The resource comparison also completes with exact
executable parity: first-edit peak RSS278608->322348KiB; third-edit RSS
278096->284476KiB; retained cache after three edits67.908->80.632MB. This
correctness repair increases work and retained data; it is not an optimization
claim. Integration reaches its1.5GiB memory cap through reclaim, with no OOM
events/kills, swap use or task denials observed. The final source image remains
frozen in i112-frozen-inputs.json and all successful gates retain exact hashes.
Local master has advanced to8674e44f, adding portability and literal-priority
matcher fixes. Integrate those before the shared-root build, preserving both
sets of compiler fixes. Merge/push, shared-root verification and disposal of
the worktree remain outstanding.
The full every-stage goal remains active/incomplete. The per-argument-type
grouping semantics question is still pending; no answer is assumed.

## Previous iteration: I111 grouping-dependent constant observations

Started18 November at10:44:09 UTC; deadline11:29:09 UTC. I110 remains isolated
and unqualified. Close a verified constant-query dependency omission before
further optimization. A24-line no-import repro prints0 with a clean build but
aborts incrementally with "pure query produced inconsistent observations".
The debugger captures the same real1.0 argument yielding integer1 versus0;
return types and pure_value_inputs are equal, but operand layouts differ.
Evidence is i111-grouped-values/{literal.dl,literal-report.json} and
i111-value-context.{json,log}. A reduced23-line variant without a direct known
argument does not fail; retain that minimization result rather than claiming it
reproduces the bug.

Keep constant observations only for the same actual grouping results, alongside
their existing type/global input proof. Migrate both retention paths: persisted
record joins and in-memory values carried across body reinference. Capture the
new grouping product before deciding whether the old constant table still
belongs to that query. The red regression covers initial execution, a different
constant argument, removal of the grouping-establishing call, and changed call
order underO0 andO2. No assertion is removed or replaced by a conflicting-value
overwrite. Build, focused parity, real-program costs and all qualification gates
remain required. The source-fact and first-edit locality work remain unfinished.

I110's controlled required comparison runs only after the other observed
qualification job exits. Both exact qualified baseline and candidate reach120 s;
neither is a pass. These controls do not isolate a patch-only timing cause.
The queued full qualification therefore stops without launching broader jobs.
Keep the fixed120 s limit and preserve i110-budget-check.json plus both logs.
The full every-stage goal is active/incomplete.

The implemented correction passes every focused grouping check, the full focused
leaf-dependency checks and all three integration compile smokes. The regression
includes fresh/reusedO0/O2 execution, known-argument edits, removal of the
grouping-establishing call (which changes output0->1), restoration of that call,
and exact warning parity. The clean removal case independently prints1 before
testing the correction. Both in-memory and persistent observation joins now
check actual operand layouts while ignoring diagnostic-check progress alone.

Local master advances to370e530a while these checks run. Its qualified required
suite has389 checks in88.027 s and splits independent incremental groups without
dropping coverage. It also contains substantial compiler/runtime/library fixes
and moves website source to dynlex-website. Integrate that local master before
further combined qualification, preserving both sets of compiler fixes. Do not
claim the previous compiler's focused results qualify the merged artifact. The
earlier profile's rising incremental cost (44.53->78.31 ms per build quintile,
versus steady clean29.72->31.95 ms) remains attribution evidence; gather actual
cache/stage counters before choosing a cache optimization.

The integrated snapshot is45661fd6, with constant-query fix3d69d3fa and local
master370e530a both retained. The two merge conflicts preserve retained payload
handles together with root grouping replay, and recursive inference observation
together with grouping dependency reads. All changed compiler files remain below
1000 lines. Source/baseline inputs are preserved in i111-frozen-inputs.json and
i111-master-baseline/manifest.json; the qualified master baseline is retained
without duplicating its binary's disk blocks.

The rebuilt846c0a1a image passes the focused regressions, all389 required checks
uncached in72.950 s, and the complete LSP suite in6.457 s. Its fixed real45-file
five-case matrix preserves exact diagnostics and executables. Body-edit CPU
medians are1.161->1.502 s, with32,11,1 inferred bodies versus1,1,1 on the
qualified master baseline. The first edit still has avoidable work; this is a
correctness snapshot, not an optimization claim. The actual WebAssembly rebuild
and full487-check integration run remain live qualification gates. Rebuild and
test the shipped browser compiler from this source before using the integration
suite's --shipped cases; the unchanged translator artifact retains its verified
master inputs. No source change enters the qualification window.

A language-semantics clarification is pending: CodeLine's committed grouping has
been shared across specializations since5ef729b5 (12 July), before this goal.
The reproducer proves that adding generic1 can change generic1.0 from1 to0.
Ask whether grouping should instead be independent per argument type. Preserve
existing clean-build behavior while qualifying this snapshot; do not assume an
answer or build a larger global-state optimization before that decision.
Implementation is frozen before the11:29:09 deadline. Subsequent live jobs are
verification, and the full cross-process/every-stage objective remains active.

## Previous iteration: I110 native grouping proof and completion closure

Started18 November at09:34:37 UTC; deadline10:19:37 UTC. I109's correctness
boundary remains unpublished, and I108's editor source facts remain parked.
Fix the verified native reuse crash before returning to optimization: saved and
current string-concatenation operand layouts are identical, but their completed-
instance sets differ21 versus22. Native instruction reuse must compare operand
layouts, while inference execution continues to validate availability separately.

Image6da2f25e passes the focused grouping regressions and the three previously
crashing integration compile cases. Its real45-file edit sequence now completes
with clean-build diagnostic/executable parity. It is not a speed improvement:
body-edit CPU is3.845,2.097,2.263 seconds against1.231,1.339,1.169 on the
qualified baseline. The required uncached suite is333/334 in109.380 seconds;
the remaining failure is the existing nested compile-time result dependency
test, whose second edit unnecessarily invalidates the entry point. Preserve the
full results as i110-projection-*; do not weaken its reuse assertion.

The minimal test's retained report5 query exposes the missing completion closure:
reading an already inferred wrapper records only the wrapper as complete, whereas
inferring it here also records its compute callee. Operand grouping and runtime
interfaces are unchanged. Observe all completed instances exported by the wrapper,
retaining their availability prerequisites, so fresh and previously completed
calls publish the same closure. Build and rerun the exact failing test before
the broader gates. No publication is qualified yet. The full every-stage goal
remains active and incomplete.

At09:56 UTC the exact nested-result regression passes after retaining the full
completed-callee closure. Debugger tracing of the real first edit finds780
grouping checks,106 rejections, and37 rejections of already completed instances.
An eight-line no-import regression confirms that lazily materializing a completed
callee repeats first-execution checks and body inference. Separate execution
replay from facts materialization using the existing completion prerequisite.
Keep ordinary operand-result and diagnostic-profile checks. The red test is
i110-materialization-red.log; the corrected image is building and will run the
full focused matrix and uncached required suite before broader qualification.

Imageebd76500 passes every focused grouping test, including the eight-line
materialization regression. The complete real-program matrix retains exact
diagnostic/executable parity and reduces body passes to33,11,1 (previously105,
23,1). Paired body-edit CPU medians are1.225->1.631 s, ratio1.344; imported
edits1.665->1.812 s, cold5.157->5.207 s. Correctness remains unpublished:
required is333/334 in103.195 s. Its next failure is the existing callback-identity
test. Record inspection proves that callback A/B and choose have no grouping
observations or prerequisites; only their unconditional completion outputs differ
when the edited choose stops referencing callback B. Such independent query facts
must not create grouping execution dependencies. Retain their facts independently
of grouping-trial rollback and emit completion prerequisites/effects only for
queries with actual grouping observations or dependent completions. Keep the
existing callback root-reuse assertion, both optimization profiles and exact
fresh/reused behavior. Archive the previous artifact as i110-materialization-*.
The corrected image is building; all gates remain outstanding for that image.

At10:12 UTC the independent-query correction passes the full focused leaf
dependency checks, including both callback optimization profiles, but the workspace
integration compile crashes. Stop broader qualification. Debugger evidence shows
noteInferenceProduct publishing a recursive placeholder with valid=true,
inferring=true, bodyInferred=false and no completed grouping product. The return
type is already usable, but the recursive body has not completed. A seven-line
no-import recursion reproduces SIGSEGV before the fix (i110-recursive-red.log).
Publish products from non-flex call sites only when that instance is no longer
being inferred; keep recursive type dependencies and ordinary call inference.
An invariant rejects any future unfinished publication. This repairs the producer
phase rather than manufacturing an empty product. Implementation is frozen for
the final focused, real-program, required<=120 s and broader qualification gates.
The iteration's10:19:37 deadline remains the implementation limit; live gate jobs
are verification, not permission for another optimization variant.

Image67369f12 completes all focused grouping and leaf-dependency checks plus the
three previously crashing integration compile cases. Its complete five-case
matrix preserves exact diagnostics and executables. Paired body-edit CPU medians
are1.866->2.247 s, ratio1.204 [1.046,1.715]; imported edits2.107->2.581 s,
ratio1.344 [0.873,1.425]. Cache-disabled cold ratio0.996 [0.992,1.044], comment
0.992 [0.942,1.005], wording1.001 [0.893,1.069] establish no change. Do not
compare these absolute timings to older runs as if machine conditions matched.
Successive body edits infer32,11,1 bodies versus the baseline's1,1,1; entry
reuse isfalse,true,true. Implementation stopped before the10:19:37 deadline;
the unchanged image is now in required and broader verification. No optimization
claim or publication is qualified yet. The remaining avoidable first-edit work
must be addressed after this correctness boundary passes, with its actual state
and interface dependencies preserved.

The final required run hits its unchanged120 s wall limit; it is not a pass.
An independent agent's dynlex-merge-qualification.service, running the
startup-runtime worktree's integration suite, overlaps the matrix and required
run (active since10:18:15 UTC). Do not attribute the timeout solely to contention
without a control. The unchanged candidate's complete incremental regression
group passes all2657 builds under a512 MiB cap and lower CPU priority:125.937 s
instrumented wall time,115.426 child CPU s. Preserve per-function/build cost in
i110-incremental-test-profile.json. This proves those functional checks, not the
whole required suite or its budget. A queued controller waits for the observed
other job, then verifies the qualified root's exact commit/binary before running
baseline and candidate required gates sequentially. A changed baseline stops
that comparison explicitly. Only a334/0/0 candidate pass within120 s unlocks
resource comparison, full LSP/integration and actual web qualification. No merge
or push is qualified; the full every-stage goal remains active and incomplete.

## Previous iteration: I109 qualify grouping execution dependencies

Started18 November at06:05:16 UTC; first deadline06:50:16 UTC. I108's source-fact
boundary remains parked. Finish the demonstrated cross-stage grouping bugs:
semantic query replay must preserve first-instantiation choices and warnings,
evicted bodies and new constant observations must infer ordinarily, and LLVM
reuse must depend on the actual grouping results. A runtime regression prints10
incrementally versus20 cleanly before the native fix. The real Breakout control
also exposes a grouping-result invariant failure; minimize and diagnose that
context transition before further optimization. Complete one correct producer /
consumer boundary with exact fresh/reused diagnostics and runtime parity, then
measure real-program CPU, memory, disk and code. The required uncached suite is
currently334/0/0 in97.802 seconds on the preceding candidate; that is not final
qualification. Do not publish the current patch until all gates pass. One45-minute
extension requires concrete progress. The full every-stage goal remains active.

At06:48 UTC all focused grouping regressions and the five-case native parity
matrix pass, and the uncached required suite passes334/0/0 in100.525 seconds.
Do not publish this candidate: a body edit grows from one inferred body to104,
and1.14 to3.49 CPU seconds. Grouping outputs currently use changed line text as
identity and interface comparison includes contextual grouping inputs. Complete
the bounded precision correction using unit-relative source positions and output
interfaces, then remeasure. Memoize final IR grouping checks only after inference
has established immutable results. This is concrete implementation and benchmark
progress; use the sole extension to07:35:16 UTC. Editor source facts remain parked.

At07:35 UTC the bounded precision iteration ends without a publishable performance
result. The broad completeness filter passes all focused grouping regressions,
but the complete five-case control records body edits at1.066->2.476 CPU seconds
and imported edits at1.390->2.750. Cold and fast paths remain approximately equal.
Do not retain that filter as measured: it rejects read-only observations of another
query's unchecked state along with actual unchecked writes. Its required run was
stopped when the complete benchmark established this rejection; no full-pass claim
applies to image6f3e0a05. Preserve its evidence as i109-broad-filter-*.

The narrowed write-only filter also fails the real-program benchmark. Inspection
of selected-grouping replay establishes that the completeness assumption was wrong:
the compiler intentionally disables repeat ambiguity search after retaining the
selected diagnostics. An unchecked write therefore does not prove that the semantic
query is incomplete. Reject both filters, preserve their measured artifacts, and
keep exact execution-precondition validation plus independent diagnostic products.
A minimal conversion regression now checks actual fresh/reused ambiguity warnings
when a previously rejected conversion is later selected, instead of imposing that
incorrect internal cache-state assumption. Finish this verified correctness boundary
before extending source-fact work or adding unrelated features. Interface comparison
uses operand layouts rather than ambiguity-check progress alone; measure that
independent correction without either rejected filter.

The functional conversion regression is red on imagea0d4bbd4: adding the selected
conversion loses the clean build's ambiguity warning. The retained red repro is
i109-conversion-profile/red.json with its22-line no-import source and compiler
logs. The missing input is the requested diagnostic profile, not a blanket
completeness rule for grouping writes. Retain probe products, record whether their
caller requested ambiguity checking, and require that proof before a stronger
semantic call consumes the result. Empty grouping products need no such search.
This correctness correction is being built single-job under the memory cap;
full focused parity, real-program cost and required uncached<=120 s remain gates.

Image59345509 fixes the conversion-warning regression and passes334 required
checks uncached in102.081 s, the full LSP suite, and focused nested/associative/
precedence checks. Its real45-file control preserves exact diagnostics and native
executables but still costs3.088,1.366,1.147 CPU s over three successive body
edits, versus1.071,1.044,1.122 on the qualified baseline. Body passes are105,23,1.
The separate resource comparison sees363864 versus269072 KiB peak RSS on the
first edit; by the third edit it is268404 versus267536. Retained cache data after
those edits is79.590 versus67.115 MB. Do not label this a speed improvement.
Evidence is preserved in i109-query-profile-* before further source changes.

Direct debugger tracing finds105 grouping-state rejections and no diagnostic-
profile rejections in that real first edit. Across the rejected products,172
observations expect a grouping that is absent,799 expect absence after a grouping
has been established, and61 differ only in check progress. No two present states
have different operand trees. Constraint probes roll back their state and do not
establish the proposed missing upstream grouping effect. Preserve the actual
evidence in i109-query-rejections{,-coarse}.{json,log}; debugger runs are work
attribution, not timing measurements. Disposable debug caches were removed after
retaining550 summary versions in i109-query-summary-versions.json.gz, freeing
221.7 MB without deleting benchmark evidence.

Full integration on59345509 is459/462, with three crashes in
filesystem_invalid_path_queries, workspace_edit_apply, and path_host_runtime.
Stop publication and web qualification. The same failure now reproduces in20
no-import lines: a completed instance observed absence during a rejected
alternative; another call later creates that grouping; reading the already valid
instance incorrectly asserts that the historical absence is a current result.
i109-negative-observer/before.json records clean success versus SIGABRT.
An added exposed-function regression also tests reading facts after cached entry
execution. Grouping-query products now separately retain required/provided
instance availability, journal it with trial state, and preserve negative reads
at the original producer. This draft is being built and has not passed its new
tests. All older timings and passes remain specific to59345509.

Complete this compiler correctness boundary before returning to optimization.
First rerun the minimal regressions and the three failed integration cases, then
the full required<=120 s, affected integration, LSP and actual WebAssembly gates.
Only then evaluate a bounded postcondition-reuse experiment for warning-free
queries whose exact grouping effects already hold; retain strict failed-alternative
reads and actual entry-state capture. No relaxed assumption or special syntax may
replace the missing dependency proof. The every-stage goal remains incomplete.

## Parked iteration: I108 shared source inference results

Started18 November at04:37:01 UTC; first deadline05:22:01 UTC. Previous turn is
progress: qualified I107 is published as224cac46, its merged worktree disposed,
and one verifiedf35682d9 baseline retained. Complete a genuine semantic-query
reuse boundary for current source facts. Native return/effect summaries alone do
not satisfy current hover, constant, overload and call-expression consumers.
Produce immutable source facts with portable identities and current grouping,
then share the existing dependency proof and migrate consumers fully. Avoid I102's
rejected runtime-body replay and retaining entire old contexts. Do not publish
consumer-only scaffolding without an actual justified reuse result. First verify
that semantic query replay respects source grouping when a different
instantiation is demanded first. Fix any demonstrated compiler issue before
continuing. Minimal no-import fresh/reused parity tests and actual work counters
must drive the change. Compare fixed real-program code/cold CPU, memory, disk and
code together; required uncached<=120 s, affected integration, LSP and actual web
behavior remain gates. One45-minute extension is available only for concrete
implementation progress. The full cross-process, every-stage goal stays active.

At05:21 UTC the no-import regression demonstrates a pre-existing correctness
failure: a cached integer call followed by a new real call accepts incompatible
shared source groupings, while a clean build rejects them. The regression now
covers both first argument types, added calls, reordered calls and removal.
Implementing explicit grouping input/output products and transactional replay is
concrete progress; use the sole extension to06:07:01 UTC for this correctness
boundary. Current-source editor facts remain a separate subsequent boundary.

## Completed iteration: I107 structurally unique grouping inference

Started18 November at03:09:53 UTC; first deadline03:54:53 UTC. Previous turn is
progress: I106 is qualified, pushed as4f55e68e, and its merged worktree disposed
while retaining verified build artifacts and one baseline. Work from the ordinary
editor inference stage, about0.96 CPU s on real Breakout edits. Reuse semantic
query results only with current source grouping, type/value, class, dependency and
diagnostic facts intact. Prefer sharing existing compiler query products; avoid
I102's rejected runtime-body serialization path and retaining whole old contexts.
Begin with a minimal red work/parity regression, complete one producer/consumer
boundary, then compare fixed real-program edit/cold CPU, peak memory, retained
bytes and code together. Keep a small clear combined win promptly; prioritize
substantial avoided inference rather than another parser micro-optimization.
Park a stalled boundary instead of extending research indefinitely. One45-minute
extension is available only for concrete implementation progress. Required
uncached<=120 s, affected integration, full LSP and actual web behavior remain
gates. The complete cross-process all-stage goal stays active/incomplete.

At03:15 UTC complete the source-fact consumer audit without adding another
serialized inference engine. Return summaries omit source grouping/value facts;
sharing the full native controller also constructs source projections and class
restoration before query reuse. Move this bounded iteration to a structural
inference producer: an expression with at most one grouping-capable node has no
alternative operand grouping. Inside an existing grouping trial, infer that
unique grouping directly under its parent's rollback rather than opening another
transaction. Current types, overloads, diagnostics and callee groupings still run.
No cached typed result or syntax spelling decides validity. Add a minimal no-import,
two-profile work/parity regression before the implementation; it is red because
the qualified baseline performs no such direct unique-grouping inferences.
Measure real edit/cold CPU and RSS before retention. This reduces required current
inference work; it does not claim cross-edit typed-query reuse or complete the goal.

I107 candidate194ccf13 freezes457 compiler/build inputs. The new minimal
regression, complete LSP suite and nested/associative/precedence runtime checks
pass. The first error fixture mistakenly used integer-plus-string, which is
valid byte-pointer arithmetic on both images. Multiplication is invalid; both
images report identical errors and inference notes. Correct the fixture from
that verified language behavior, without changing compiler expectations. A
separate controller invocation used the wrong argument shape for two helpers;
correct those arguments and resume only the remaining checks. No benchmark had
run before those corrections.

The fixed primary comparison uses POSIX nanosecond process CPU clocks on the
real45-file Breakout program:120 observations, three independent paired sessions
per case and three edits per session. Complete tokens/diagnostics match. Body,
imported lives, imported VM statement and moved-comment edit medians are
1.58016->1.48931,1.58904->1.48066,1.57550->1.48879 and1.58316->1.47969 CPU s.
The twelve independent session-total ratios have median0.932516 and interval
[0.927536,0.936529]. Every main-body edit skips14447 nested trials while parser
and matcher counts/payloads remain unchanged. Fifteen paired cold editor opens
have CPU medians3.58966->3.49530 s, ratio0.970742 [0.958455,0.976144].
Measured edit peak-RSS medians are1.5-3.0% lower; this is not a universal memory
claim. The implementation adds16 net production lines,12696 native-binary bytes
and no persistent or in-memory cache payload. Keep the favorable combined result.

The original88-observation native control matrix is complete with byte-identical
executables/diagnostics. Five active-body pairs have CPU medians1.06496->1.05449 s,
ratio0.993970 [0.936317,1.012414]: no established native-edit gain. Three cold
pairs observe4.61420->4.49324 s, ratio0.973459 [0.963666,0.976133]. Comment and
wording paths remain around26 and30 ms; no algorithmic fast-path gain is claimed.
Complete required uncached<=120 s, affected integration, actual web and shared-root
qualification before publication. Ordinary editor inference still runs; this
iteration eliminates redundant nested grouping work rather than restoring typed
queries across edits. The complete all-stage objective remains active/incomplete.

At03:38 UTC the initial194ccf13 candidate fails six of334 required checks:
one source range retains leading whitespace and five diagnostics lose inference
notes. Stop all publication gates. The shortcut incorrectly borrowed the fully
ordered branch's rule of fixing every child node; the ordinary generator fixes
only the grouping-capable root in this structural case. It also omitted the
generator's range normalization and the ordinary failure propagation boundary.
Keep those contracts while sharing the parent rollback journal: normalize current
ranges, fix only the actual root, and preserve/reset the failed expression's
snapshot before publishing its captured diagnostic with the current outer trace.
The six unchanged, minimal required fixtures now match the qualified baseline's
complete output and exit codes exactly. The complete LSP suite and focused runtime
checks pass again. Debugger line probes did not reach the selected helper paths;
they provide no additional runtime attribution evidence.

The initial source/measurements remain in i107-initial-frozen-inputs.json,
i107-initial-validation.json, i107-initial-required.log, i107-lsp-paired and
i107-native-controls.json. They do not qualify the corrected artifact. Run the
same fixed120-observation editor and88-observation native matrices once on the
corrected image, then require every full gate. Do not change diagnostic expectations.
The full objective remains active/incomplete.

Use the sole qualification extension, ending04:39:53 UTC, for the corrected
boundary and its full gates. No further optimization enters this window. Corrected
imagef35682d9 is frozen; each measured main-body edit skips20974 nested trials
with unchanged matcher/parser work and payloads. Complete its independent fixed
matrices, required/integration, actual web and shared-root gates before retention
or publication. The initial194ccf13 result remains unqualified.

The correctedf35682d9 comparison is complete and separate:120 editor
observations with full token/diagnostic parity. Body, lives, VM statement and
moved-comment edit CPU medians are1.60325->1.49913,1.60677->1.49105,
1.60648->1.48272 and1.58550->1.48873 s. Twelve independent session-total ratios
have median0.931089 [0.927353,0.937715]. Fifteen paired cold editor opens have
medians3.60920->3.49728 s, ratio0.970058 [0.955533,0.977009]. Config wording
still reuses its context; its interval includes parity. Peak-RSS edit medians
are1.4-2.9% lower in these cases. Corrected code adds37 net production lines,
32128 native-binary bytes and no cache payload, disk product or user setting.

All88 corrected native observations preserve exact executable/diagnostic parity.
Five native body-edit pairs have CPU medians1.05313->1.05080 s, ratio0.998900
[0.975829,1.018928]; no established native-edit gain. Three native cold pairs
observe4.64899->4.56287 s, ratio0.983278 [0.970412,0.986460]. Native comment
and wording controls stay around26 and30 ms, with intervals including parity.
The primary comparison was repeated only because the corrected compiler is a
different artifact, not to retry a failed performance gate. Keep the corrected
combined tradeoff, subject to all full gates. Evidence is
i107-corrected-{diagnostic-parity,performance-analysis,editor-cold-analysis,
tradeoff-ledger,native-controls,prequalification}.json and
i107-corrected-lsp-paired/results.json. No timing from194ccf13 qualifiesf35682d9.

I107 closes18 November before its04:39:53 UTC extension deadline. Corrected
ownerf35682d9 passes all334 required checks uncached in96.488 s,462 integration
checks in706.826 s, the full LSP suite and actual WebAssembly host parity. Final
WASM is6ec95c72. Local master is rebuilt from the same457 source/build inputs:
root093a7f19 passes all334 required checks uncached in95.002 s and all nine
focused editor regressions. No original194ccf13 gate or timing substitutes for
this corrected artifact's qualification. Keep the37-line, no-new-payload change
for its measured editor/cold speed and memory benefits. Ordinary editor inference
still evaluates current types; complete per-unit typed-result reuse remains work.
The complete cross-process, all-stage objective is active/incomplete.

The next larger boundary is immutable source inference facts shared with native
semantic queries. Current hover/constant/overload consumers read instantiated
bodies through source-to-clone mappings; return summaries alone cannot satisfy
them. Complete that producer/consumer boundary without retaining entire old
contexts or repeating I102's rejected runtime-body serialization engine. Preserve
current grouping, values, types, selected instantiations and diagnostics, then
prove actual query reuse and measured combined benefit before publication. The
fresh native body-edit inventory also exposes resolution setup96.326 ms,
snapshot89.152 ms, cache writing80.624 ms and payload loading66.284 ms; these
scopes overlap parents and must not be added. Read-only evidence is
 i107-native-stage-inventory.json and i107-next-inference-boundary.json.

## Completed iteration: I106 shared editor syntax products

Started18 November at01:45 UTC; first deadline02:30 UTC. Previous turn is
progress: I105's full real-program comparison rejected an unhelpful matcher
algorithm and restored the exact qualified images. Target the larger ordinary
editor inference stage, roughly0.96 CPU s per edit. Audit the source-expression,
grouping, type/value and diagnostic facts consumed after inference, and reuse
unchanged units through their actual input/dependency proof. Share compiler
products and semantics; do not reintroduce I102's encoded runtime-body engine or
skip facts which editor consumers need. Begin with minimal fresh/reused parity
tests including parameter-last-use, overload/failed-lookup changes and errors.
Complete one producer/consumer boundary, measure actual units skipped and real
program CPU/RSS/bytes, and qualify required uncached<=120 s plus affected native,
LSP and actual web behavior. Prioritize the full cross-process/all-stage goal,
which remains active/incomplete. One45-minute extension is available only for
concrete implementation progress; park a stalled boundary and change targets.

At02:04 UTC park the typed-inference boundary after the consumer/dependency
audit: return summaries omit source grouping and per-instantiation body facts,
and the native constraint product also depends on class restoration, source
projections and failed/callable/conversion reads. Moving just cached values would
be incorrect; moving the whole rejected runtime-body path would repeat I102.
The original deadline still applies. Switch this iteration to shared unresolved
syntax products, including temporary type-constraint expressions. The compiler
already has that codec; decoded nodes use current ranges and ordinary matching
and inference. There is no new semantic engine, disk product or user setting.
Retain the latest used syntax and one last complete set through errors, guarded
by current builtin/project non-message syntax identity. The minimal two-file,
two-profile work/parity regression is red on the qualified baseline. Build the
candidate and let real-program CPU/RSS/bytes decide whether to retain it. This
does not claim that ordinary editor type inference has become incremental.

At02:28 UTC retain the small positive parser boundary and use the sole
qualification extension, ending03:15 UTC. No further optimization enters this
window. Candidate82ea576d freezes457 compiler/build inputs. The minimal two-file,
two-profile parity/work test passes, including syntax changes and error recovery.
On the real45-file Breakout edit, it reuses2842 expression products (including65
temporary type expressions) and819 definitions, with one new expression parse.
Encoded syntax is892763 bytes per measured profile; matcher bytes remain6283411.

The original150-observation matrix remains unchanged: forty paired code edits
have CPU totals63.75->63.42 s and medians1.59->1.585 s; its tick-resolution
paired interval includes parity. Twenty-five cold-opening pairs also include
parity, ratio1.00557 [0.99726,1.00824]. Do not claim a broad cold-build gain.
One secondary precision check uses the POSIX process CPU clock in nanoseconds,
with three independent session pairs and five edits each for main body, imported
lives and imported VM statements:108 observations,45 paired edits. All tokens
and diagnostics match. Analyze the nine session totals as independent blocks,
not the45 correlated edits: median ratio0.992889, interval[0.986484,0.996516].
The cases' aggregate CPU is24.38910->24.26244,24.50665->24.28269 and
24.78305->24.46247 s. This is a small established gain, not a percentage-floor
exception. The payload is under1 MiB and no disk product/setting is added.
First-edit process RSS is about2% lower in the primary matrix; repeat peaks do
not establish a general memory gain. The larger cgroup peak in the precision
run includes its result-holding Python client; retained compiler products stay
flat across edits. Finish native controls, required<=120 s, affected integration,
full LSP, actual web and shared-root qualification before publication.

I106 native qualification passes334/0/0 required checks uncached in98.201 s,
462/0/0 integration checks in698.601 s and the complete LSP suite. The actual
web build finds a pre-existing native-only codec boundary:64-bit decoded lengths
narrow into32-bit address-sized frame fields. A minimal C++ repro confirms the
compile error. Return address-sized lengths and reject values beyondSIZE_MAX;
keep literal words64-bit and preserve the valid cache wire format. The extracted
actual reader passes native and32-bit WebAssembly boundary/overflow tests, and
the parsed-product LSP regression passes again. The fixed native image d1230f74
has identical allocated ELF sections, addresses and sizes to the benchmarked
82ea576d image, excluding only the build-id note. Thus the native executed code
and program data are unchanged; retain the original native gates and measurements
with this explicit provenance. Actual full web and shared-root gates remain
required before publication. No native cold algorithmic speedup is claimed.

I106 closes18 November before its03:15 UTC extension deadline. Actual web build
and host editor parity pass with WASM435eaf9e. Local master is rebuilt from the
same457 frozen source/build inputs: all334 required checks pass uncached in
95.657 s, followed by all eight focused editor regressions. Root native image is
e3e4dfb8; owner is d1230f74, with its explicit82ea576d benchmark/gate code-data
equivalence proof. The implementation adds151 net production lines and1,021,232
bytes to the debug-enabled native binary; its measured per-profile syntax payload
is892,763 bytes. It adds no persistent cache or user configuration. Keep this
small positive shared-parser boundary under the combined tradeoff policy. Do not
claim native cold speedup or incremental editor inference. The original complete
native control matrix remains unchanged; body/comment/wording comparisons include
parity, and three cold pairs alone do not establish an algorithmic cold gain.

Next priority is the ordinary editor inference stage, about0.96 CPU s per edit.
Complete a compact producer/consumer boundary for current-source inference facts,
including grouping, types/values and diagnostics, with real dependency invalidation.
Reuse the native query semantics without repeating I102's rejected runtime-body
serialization engine. The full all-stage, cross-process source/config goal stays
active/incomplete. Evidence is i106-{frozen-inputs,benchmark-inputs,validation,
web-qualification,root-validation,native-controls,tradeoff-ledger}.json,
i106-lsp-{paired,precision}/results.json, i106-precision-session-analysis.json
and i106-length-portability/{native-equivalence,reader-tests}.json.

## Closed iteration: I105 rejected adaptive signature intersection

Started18 November at01:19 UTC; first deadline02:04 UTC. Previous turn is
progress: I104 is qualified and published asc8fed286. Start from its retained
c7d01ebc owner image with no other compiler/test job running. Measure current
matcher fingerprint cost and repeated input construction on the fixed real
Breakout program before selecting one immutable producer to retain. Complete
one reduction in relevant work; preserve positive/failed lookup dependencies,
overload additions/removals, scope visibility, parameter reclassification,
diagnostics and source ranges. Compare native and editor cold/edited controls,
CPU, peak memory, retained/disk bytes and code size. No percentage floor; keep
only a justified combined tradeoff. Avoid I102's rejected encoded editor bodies.
One45-minute extension is available only for concrete implementation progress.
Required uncached<=120 s and affected integration, LSP and actual web checks
remain gates. The full source/config/classification/inference/IR/object/link
goal stays active/incomplete.

I105 closes18 November at01:43 UTC, before its first deadline, without an
extension. The candidate adds27 net production lines in one C++ file: traverse
the smaller of a signature node's children and the remaining sorted query
literals, then preserve the original sorted visit order. The minimal eight-word,
two-line regression changes37 probes to8. Separate sparse/branching/punctuation
fixtures retain byte-identical lookup fingerprints, encoded outcomes, diagnostics
and executables while changing38->8,52->13 and17->4 probes. Focused matcher,
segment retention and eviction checks pass. Candidate image40e48237 and its455
compiler/build inputs are frozen before measurement.

The full150-observation, five-case editor matrix preserves complete tokens and
diagnostics. Forty paired source edits have CPU medians1.59->1.59 s and total
CPU63.69->63.66 s. The paired ratio is1.00000, interval[0.99377,1.00000].
Twenty-five cold openings have paired ratio1.00000, interval[0.98913,1.00280].
There is no established end-to-end gain. Existing key timers change201.62->190.35
and193.70->183.55 ms on the two instrumented edits; signature probes drop from
1478196 to591861, with identical11294 key computations and11276 reused results.
This component saving does not justify the larger algorithm without a program-
level improvement. Reject the candidate; do not retry the matrix or claim a
speedup from fewer probes. No arbitrary percentage threshold was applied.

The first30-observation probe completed its parity comparisons but its summary
attempted a median of no ratios when all baseline analysis CPU samples were zero
at10 ms resolution. Report that ratio as undefined. The original rows are retained
unchanged and reanalyzed, without repeating the measurements. The full matrix
uses that numerical correction. GDB's optimized compile-wrapper finish timer
measures only the inference tail; its whole-compile CPU field is invalid. A first
sampling attempt stopped after one interrupt; a corrected sampler's broad entry
name also matched inline callables, so per-phase grouping is invalid. Retain only
aggregate stack locations as qualitative hotspot evidence. These limitations are
recorded explicitly in i105-profile/validity.json; no debugger timing is an
acceptance measurement. Verified compression reduces118 MB of raw samples/logs
to2.34 MB without losing their contents.

Archive the rejected patch and frozen manifest, then restore both owned compiler
and test files exactly. Rebuilding recovers the qualifiedc7d01ebc native image;
all455 compiler/build inputs and the actuald8ff9641 web image remain identical
to I104. Focused matcher and LSP checks pass after restoration. Reuse the exact-
image I104 required/integration/web qualification rather than rerunning unchanged
tests. Only this research record is retained in the repository.

Next bounded work targets ordinary editor inference, about0.96 CPU s per edit,
rather than another matcher-key micro-optimization. Investigate reusable source-
inference outputs and their dependency boundary: unchanged source groupings,
types and diagnostics must survive while relevant parameter/overload changes
cascade. Do not reuse I102's expensive encoded runtime-body engine or publish
tooling state that omits the source-expression facts its consumers read. The
full cross-process, all-stage objective remains active/incomplete.

Evidence: i105-{frozen-inputs,fingerprint-parity,rejection,restoration-audit}.json;
i105-rejected-experiment.patch.gz; i105-matcher-work-red.log;
i105-{focused,restored-focused,restored-lsp}.log; i105-lsp-probe/results.json;
i105-lsp-paired/results.json; i105-profile/{stages,candidate-stages,validity,storage}.json.

## Completed iteration: I104 stable matcher source identity

Started18 November at00:02 UTC; first deadline00:47 UTC. Use one45-minute
iteration to attribute and reduce the
extra matcher work on the first edit which moves code. The fixed real-program
control computes5342 matcher queries on its first inserted-comment edit versus
752 on later coordinate-moving edits; equivalent source results alone do not yet
explain that difference. Target P08's shared matcher query inputs, rather than
reintroducing I102's encoded editor runtime bodies. First isolate the relevant
fingerprint or lifecycle input with minimal no-library fixtures and current
work counters. Include ordinary added/removed statements as well as comments;
retain exact diagnostics, tokens and native output, and parameter/overload/scope
cascades. Remove irrelevant key inputs only after proving their semantic role.
Complete one measured consumer boundary, with CPU/RSS/bytes/cold controls and
required<=120 s plus affected integration/web gates. If the boundary does not
repay its cost, record the result and move to the next P03/P14 materialization
consumer. One extension is available only for concrete progress. The full
source/config/classification/inference/diagnostic/IR/object/link goal remains active.

I104 verified attribution,18 November at00:39 UTC: physical line movement is
not the cause of the first-edit penalty. A five-code-line, two-file reproducer
performs four new matches when the helper is opened after the main file, versus
zero when it is opened first. GDB through `scripts/debug.sh` shows the imported
snapshot change from an absolute filesystem spelling to a `file://` URI while
its canonical key remains identical. The shared matcher/trie fingerprints used
the former spelling. The first real-program statement edit consequently performs
5346 ->756 matcher computations; imported lives edits4018 ->754 and moving
comments5342 ->752. The remaining work includes ordinary dynamic inference
lookups; this does not establish complete editor inference reuse.

The earlier source-line breakpoint counts are inconclusive: even the known cold
miss run recorded zero absent keys. They are explicitly marked unusable and are
not evidence about orphan-node frequency. The first snapshot-printer attempt also
stopped on its map-child representation; the corrected function breakpoint records
the actual old/new snapshot URIs and equal canonical keys. Raw logs and both
failed probes remain separate from usable measurements.

Retain one canonical path per SourceFile snapshot; display spelling and source
coordinates retain their distinct roles. Import deduplication and recording use
that same identity. All six matcher/trie source-identity inputs use the cached
canonical path, with no per-query filesystem normalization, new cache files,
configuration, dependencies or alternate semantic engine. This is one net
production line after shared producers and formatting, with one stored string
per source snapshot. Normalizing the public display URI was rejected because
source projections and protocol/diagnostic consumers address that distinct value.

The minimal regression is red on I103 (eight computations, zero hits across two
profiles) and passes on the candidate (zero computations and reused outcomes).
It compares complete token vectors and client diagnostic state for main and
helper files with fresh analysis, including body edits, overloads, parameter
retraction and repair. Empty diagnostics are state transitions, so client state
is retained across notifications rather than comparing notification histories.
All full LSP scripts pass. The455 frozen inputs produce compiler
`c7d01ebc5531d0d81a6c8fe94c7e24213b59871c2c5a7a4297e17a03e6055807`.

`i104-lsp-paired/results.json` retains150 observations: five independent sessions
per case and image, each with a first and repeat edit on fixed45-file Breakout.
Both compilers use identical tracing; complete tokens and diagnostics match in
every comparison. Each sample edits saved source and the editor buffer. No GUI
program is launched. Added statements reset the real VM's sound value at entry
to its existing step function, with no added imports.

| First editor edit | I103 CPU median | I104 CPU median | Paired ratio,95% interval |
| --- | ---: | ---: | ---: |
| Open main body |1.60 s|1.59 s|0.98750 [0.95882,1.01274]|
| Opened imported lives code |2.43 s|1.59 s|0.65714 [0.65306,0.65975]|
| Add imported VM statement |2.67 s|1.60 s|0.59925 [0.59041,0.60456]|
| Insert imported comment lines |2.68 s|1.60 s|0.60377 [0.57348,0.60687]|
| Diagnostic wording |0.09 s|0.10 s|1.11111 [1.00000,1.25000]|

CPU includes analysis and token rendering at10 ms resolution; the last row is
at that measurement scale and repeat wording remains0.10 ->0.09 s. Ordinary
repeat source edits remain roughly1.57–1.59 s, with intervals including parity.
Cold opening medians over25 sessions per image are3.58 ->3.59 s. The added-
statement first-edit RSS medians are437824 ->438332 KiB, paired1.00132
[0.99842,1.00195]; no material first-edit memory increase is established. Other
edited cases have stable or lower process peaks. Retained matcher bytes remain
identical for every shown pair (6283411 normally,6286285 for the added statement).
Keep the clear34–40% first-imported-edit speed gain with negligible storage/code
cost; do not apply a percentage floor.

At00:47 UTC use the sole45-minute qualification extension, ending01:32 UTC.
The implemented fix, minimal red/green regression and completed150-row real-program
matrix establish concrete progress. No further optimization enters this window.
Native controls retain88 observations with exact executable and diagnostic parity:
body CPU1.070739 ->1.059797 s, cold4.697140 ->4.730025 s, comment0.025605
->0.025694 s and wording0.031196 ->0.031035 s. Their paired intervals include
parity; no native speed gain or regression is established. All334 required tests
pass uncached in98210 ms. Integration, actual web and shared-root qualification
remain pending. Keep source/compiler inputs frozen throughout those checks.

I104 qualification completed18 November at01:16 UTC, before its sole extension
deadline. Source commit13e46fb6 retains the frozen455 compiler/build inputs and
native imagec7d01ebc. Owner required334/0/0:98210 ms; integration462/0/0:
715819 ms. The full native LSP suite passes. The actual WebAssembly module
`d8ff9641225fc25fcd860870cabbe925c9e7fd47f338fb618f1940b4bdb20e6d`
passes host edits covering overloads, negative lookups, classification, profiles
and fresh-analysis parity. The local-master rebuild produces image3ba7bc65;
its uncached required suite passes334/0/0 in96998 ms, and all seven focused
LSP regression scripts pass. No compiler input changed between measurement and
qualification. Heavy jobs ran sequentially within the recorded memory caps.

Keep the measured first-imported-edit gain, not a general cold/repeat-build gain.
There is no new disk product or setting, retained matcher bytes are identical,
and production code grows by one net line. Ordinary editor context construction
and inference remain live; the full cross-stage goal is still active/incomplete.
Next bounded work returns to P08's shared matcher-query inputs on this qualified
baseline, measuring current key/input construction before choosing a producer
to retain. Do not resurrect I102's rejected encoded editor runtime engine.

Evidence: i104-provider-repro.json; i104-provider-identity/snapshots.json;
i104-source-identity-{red,test}.log; i104-lsp-suite.log;
i104-lsp-paired/results.json; i104-native-controls.json; i104-validation.json;
i104-web-qualification.json; i104-root-validation.json; i104-frozen-inputs.json.

## Completed iteration: I103 source-layout-preserving editor comments

Started17 November at23:17 UTC; first deadline18 November at00:02 UTC,
with one further iteration only for concrete progress. Target P19/P21: ordinary
editor comment edits still recreate full contexts at roughly1.7 CPU s. Reuse the
qualified context when configured lexical code, import observations and physical
source coordinates are unchanged; render lexical comments from the current
editor document. Preserve custom comment spelling, strings, imported files,
configuration changes, profiles, current diagnostics/tokens and error recovery.
Changes that move executable coordinates still require the ordinary pipeline;
line-mapping replay remains explicit unfinished scope, rather than an unsafe reuse.
First add focused fresh-analysis parity and work-count tests. Compare fixed real
program comment edits, body edits and cold opening, including peak RSS and retained
bytes. Keep only a clear combined tradeoff; no percentage floor. Remove the whole
experiment if the proof or consumer stalls within the timebox. Required uncached
<=120 s and affected integration/web gates remain mandatory before publication.

I103 candidate verification,17 November at23:37 UTC: the455 frozen compiler/build
inputs produce`8466c968462cc0f24024980174972b210a6d615ca2b2834bba7181934344f404`.
The first prototype incorrectly reacquired an old imported source through its live
filesystem; opening a buffer could return new text and hide a code change.
The existing minimal `test_lsp_import_snapshots.py` catches this. Compare the
importer's retained `importedFiles` pointer instead. The corrected comment test
also changes an imported body after opening it. All focused and full LSP scripts
pass. Preserve the exact prototype helper and its original frozen manifest;
its image is`45fa25345bad02ffc300130560c99ecfc8721b052132d1441eb1974e07283f2d`.

`i103-lsp-paired/results.json` retains64 observations, with seven paired edits per
case on the same fixed45-file real Breakout. Both images use the same tracing
option, with exact diagnostic and complete-token parity. CPU includes analysis
and token rendering at10 ms resolution. An existing CHIP-8 header comment is
changed without moving executable code; this is distinct from inserting lines.

| Editor edit | Qualified I101 CPU median | I103 CPU median | Paired ratio,95% interval |
| --- | ---: | ---: | ---: |
| Body |1.65 s|1.68 s|1.01183 [1.00000,1.03030]|
| Lives |1.68 s|1.68 s|0.99177 [0.97674,1.00000]|
| Existing comment |1.67 s|0.10 s|0.06250 [0.05848,0.06587]|
| Diagnostic wording |0.10 s|0.10 s|1.00000 [0.90000,1.11111]|

For the comment case, peak-RSS medians are473688 ->247216 KiB; the paired
ratio is0.52190 [0.51760,0.52754]. It evaluates zero contexts and computes zero
matcher results, retaining the same6283411 matcher bytes. Cold editor-opening
CPU medians are3.530 ->3.535 s over four openings per image. Other source edits
retain ordinary compilation and establish no general speed gain. There are17 net
production lines, no extra disk products, dependency or user configuration.
Keep the combined clear comment speed/memory win; no percentage floor is used.

A first insertion control then edits the inserted comment's text; only its first
sample actually moves code. Keep that report separate. The corrected
`i103-comment-lines-moving/results.json` adds a different number of comment lines
on every sample. All three edits evaluate a context, with exact parity; both CPU
medians are1.63 s (paired1.00000 [0.99371,1.00377]). Source-coordinate replay is
still required for that boundary.

Native controls pass88 rows with exact executable and diagnostic parity. Five
body pairs have CPU medians1.056213 ->1.037896 s (paired0.99488 [0.97884,1.01089]);
three cold pairs4.590252 ->4.589081 s (paired0.99809 [0.98950,0.99974]). Native
comment/wording controls remain around26/30 ms. Do not claim a substantial native
improvement from these controls. Required tests pass334/0/0 uncached in98523 ms.
Integration tests pass462/0/0 in701758 ms. The actual WebAssembly build and
host-API comment/edit/parity regression pass. Its module SHA256 is
`326e5dc2075da00d7d97553e4ec1302a275e333d4815b9b87dee9e039117cd6b`.
Shared-root source commit`673b430f` rebuilds to compiler
`25b75b7b8b7d4f166cf2ec134a7605681634fb71ef92c2f82bc7213756b9212e`.
All334 required checks pass uncached in96853 ms, followed by all six focused LSP
regressions. Qualification completes17 November before the00:02 UTC first
deadline, without an extension. This is a qualified snapshot, not full-goal completion.

## Rejected iteration: I102 shared encoded LSP inference products

22:30–23:15 UTC on17 November; no extension used. The qualified I101 compiler
remains the baseline. Stage attribution on the fixed45-file Breakout finds
ordinary inference0.95–0.97 CPU s, matching0.383–0.397 s, and import/sections
roughly46 ms. The experiment shares the native inference codecs and dependency
controller through memory-backed immutable blobs, without native IR ownership.
It adds377 net production lines; no new dependency or configuration.

The corrected minimal regression is red on I101 and passes on the candidate:
a leaf edit reduces body-inference passes4 ->2 across CPU/fragment profiles;
complete tokens and diagnostics match fresh analysis through overload, failed
lookup, classification and error-repair edits. Retained typed data over61 failed
edits stays33006–33012 bytes. These narrow passes are not whole-program proof.

The real-program gate rejects the experiment on its first edit. I101 CPU is1.61 s;
the candidate is3.27 s, with peak RSS438140 ->640560 KiB and43151416 retained
inference bytes. It reuses399 instantiations with one body-inference pass, yet the
complete semantic token vector differs while diagnostics match. The benchmark
stops at that failure; no further cases or full-suite pass are claimed.

A seven-line no-import reproducer isolates source operand grouping. Ordinary
inference's `commitCodeLineGrouping` updates `CodeLine::expression` and its committed
grouping, while typed-body decoding restores only the executable body. The editor
reads the former, so replay does not reproduce the chosen source grouping.
This incomplete editor boundary is absent from the qualified compiler. Do not
change token expectations or suppress reuse for operator spellings.

A separate GDB stage attribution uses `scripts/debug.sh`. Current scoped counters
show body restoration1136.8–1209.5 ms, versus the entire old inference's950–970 ms.
Source-match projection costs roughly360–380 ms; declaration projection30–40 ms,
post-inference graph work110–120 ms, and retained-byte accounting70–90 ms on edits.
Optimized source-line breakpoint intervals are attribution aids, not disjoint
compiler-stage totals. The direct body-restore counter establishes that reduced
pass counts alone do not repay materialization. Encoded disk-oriented bodies are
not kept as the editor cache architecture.

All51 modified code/build/test files and18 new files are removed after verified
archival. `i102-rejected-experiment.patch.gz` is34415 bytes; its source manifest
records compiler`90fc3ce3ac0ff8aa1c60caf827b35c42924b8fa641e8620ed1b6b8e1ad9e69c2`.
The seven-line reproducer, failed complete-token comparison, focused test, profiling
scripts and raw measurements remain in the evidence directory. Restore the exact
I101 owner compiler`e38fa66cb01c161f9594716cca067230b166528aff59f5fb057be4ab1e23fa5b`.
No factor-only storage/backend/namespace scaffold is retained. No compiler behavior
from this rejected experiment is published; the full incremental goal remains active.

## Completed iteration: I101 shared LSP matcher queries

Started17 November at21:38 UTC after I100 publication; first deadline22:23 UTC.
At22:19, the consumer-visible reuse boundary, bounded-error lifetime, native/web
regressions, paired benchmark and required gate are complete. Use the single
permitted extension until23:08 UTC for remaining integration/shared-root
qualification and publication; do not start another optimization here. Complete one
ordinary source-edit reuse boundary by connecting the existing native
`IncrementalMatcherCache` to LSP compiler hooks. Matching was2.33–2.95 s in the
LSP; I100's native population spends221 ms computing matcher keys versus1827 ms
body matching and565 ms rematching. Use its existing positive/negative lookup
proofs, current-source restoration and complete/partial snapshot semantics rather
than a separate resolver. Clear temporary hooks before their cache lifetime ends.
Retain only current segments; expose actual evaluations, hits and retained bytes.

First add minimal overload, failed-lookup, parameter-classification, import,
profile and error-repair regressions. Compare fixed real-program code edits,
complete tokens/diagnostics, cold opening, peak RSS and retained bytes. Keep a
clear combined speed/memory/disk/code win without a percentage floor. No new user
setting or disk cache. The shared LSP also builds into the web compiler; reconcile
its current matcher-source exclusion and verify the installed Emscripten/LLVM
boundary without downloading or rebuilding LLVM. If individual-lookup caching
loses, record it and move to shared whole-body products within the bounded plan.
The full compiler-wide goal remains active.

I101 implementation evidence,17 November at22:03 UTC: a minimal source-edit
regression is red before implementation because matcher work is unavailable,
and passes after connecting the shared native cache. It checks current tokens
and diagnostics against fresh analysis for both CPU and fragment profiles,
including body edits, overload insertion/removal, implicit-parameter retraction,
failed-lookup repair, imports and pruning. The full LSP script suite also passes.
The initial fixed455-input prototype image is
`51de50986242dffcdb4d29b84bccfb02e6a59543cb8a436b1fbb9899f686c45b`.
Its strict real-program comparison is retained in`i101-lsp-paired/results.json`:
three ordinary edit CPU medians fall approximately5.0 ->1.7 s, with exact
complete-token/diagnostic parity; peak RSS medians rise7.6–8.7%. Cold opening
rises3.32 ->3.645 s. There is no extra setting or disk cache. Typical edits
reuse11276–11294 lookups while computing752–770; encoded products are6283411
bytes. These are prototype measurements, not final acceptance after the fix below.

A focused repeated-failure probe finds that directly using native partial-snapshot
retention in an editor accumulates obsolete failed lookups:707 ->12507 bytes over
61 edits on one tiny program. The larger regression also exposes2376 ->26708 bytes.
Fix the editor lifetime before qualification: retain the last successful product
set plus only queries used by the current failed analysis. Both lookup sets use
the existing native fingerprints and restoration code; successful analyses prune
the previous failed set. The fixed probe stays within900–1197 bytes, and the
61-edit regression passes with clean-analysis parity and recovery. The exact
prototype source is retained with its verified original SHA256 alongside the
prototype frozen-input manifest. Final benchmarks must use a new frozen image.
The actual web compiler builds against installed pinned LLVM/Emscripten with
one build job and memory/swap caps; its optimizer/link step is still running.
Native/web qualification and publication remain pending.

Final I101 source and editor qualification,17 November at22:08 UTC: the frozen
native image is`e38fa66cb01c161f9594716cca067230b166528aff59f5fb057be4ab1e23fa5b`;
the actual web module is`a45890799efda3e608c267d4f333207f724333f94fa893db96d7c0df98b10c87`.
The full LSP script suite and actual WebAssembly host-API edit/parity test pass.
`i101-lsp-final/results.json` retains64 observations with seven paired edits per
case on the same frozen45-file Breakout. Both compilers enable the same trace
option, retaining work counts. All diagnostics and complete token vectors match.
The initial prototype report remains separate and is not substituted for this
final retention-safe image.

| Saved LSP edit | Qualified I100 CPU median | Final I101 CPU median | Paired CPU ratio,95% interval |
| --- | ---: | ---: | ---: |
| Body |4.81 s|1.66 s|0.34719 [0.34082,0.39196]|
| Lives |5.02 s|1.69 s|0.33658 [0.33465,0.38499]|
| Comment |4.90 s|1.70 s|0.34968 [0.33797,0.37055]|
| Diagnostic wording |0.10 s|0.09 s|1.00000 [0.90000,1.11111]|

These include analysis and semantic-token CPU, sampled at10 ms resolution.
Ordinary-edit paired peak-RSS medians rise6.8–8.3%; process peak medians are
436852 ->465284,437480 ->463920 and434596 ->461776 KiB. Cold editor-opening
CPU medians are3.260 ->3.535 s across four openings per image.
Keep this explicit cold/memory cost alongside roughly65–66% less ordinary-edit
CPU,97 net production C++ lines, no new dependency and no disk cache. Native
controls pass exact executable and diagnostic parity. Five native body pairs have
CPU medians1.050157 ->1.046090 s (paired ratio1.00967,[0.99023,1.01370]); three
cold pairs4.774998 ->4.739672 s (ratio0.97158,[0.95982,1.01464]). Neither establishes
a native body/cold speedup. Fifteen native comment pairs have ratio0.98630,
[0.97548,0.98941], and wording0.98931,[0.97875,1.01157]. Native algorithmic work is
unchanged; these remain controls, not a claim of a new native reuse boundary.
The uncached required suite passes334/0/0 in98032 ms, below120 s. Full integration
passes462/0/0 in701879 ms. The final native/web source hashes and both compiler
artifacts remain unchanged. Integration's1.5 GiB cap records no OOM events or
kills. Implementation`fb86d6d4` contains the retention-safe code and tests.
Shared-root qualification completes17 November at22:27 UTC. Local master is
rebuilt from the same455 source/build inputs and passes334/0/0 uncached in99950 ms,
plus all five focused LSP regressions. Its independently built image is
`baf07303b796fdd63b555ade3f65cf96fa9dad73ff414150ff71b9c645b9cea2`.
All qualified CPP/CMake, native owner-image and web-module hashes remain intact.
The final record is pushed with implementation`fb86d6d4` and owner qualification
`a27808b9`. Dispose the merged worktree while preserving its native/web build
objects and one current comparison image; remove only the SHA256-verified
superseded comparison image. No timing or test rerun is used to erase the small
costs recorded above. The full P01–P24 objective stays active: current-source
setup/classification/inference, broader native stage locality/persistence and
other output paths still need work.

## Completed iteration: I100 LSP diagnostic-only configuration queries

Started17 November2026 at20:15 UTC after I099 publication and merged-worktree
disposal; first deadline21:00 UTC. By20:54 the regression is red and the reuse
boundary, query tracking and diagnostic provenance migration are implemented;
use the single permitted extension until21:45 UTC for build and qualification. Complete
one actual reuse boundary: when only message configuration changes, retain valid
LSP semantic results and rerender affected diagnostics. Share the native driver's
configuration identities and diagnostic provenance rather than recognizing syntax
or particular warning names. Preserve positive and missing-file/import observations,
builtin/project configuration dependencies, target profiles and unsaved snapshots.
Changed structural syntax or source inputs must still invalidate the units that
read them. Add minimal cascade/error-repair and work-count regressions first;
compare current real-program wording edits against their approximately5-second
analysis baseline, including memory and cold/default controls. Finish a small
clear positive change promptly. Ordinary source matching remains the next large
P19 boundary: resolution alone was2.33–2.95 s, while source/section setup was under
60 ms. Avoid another extraction-only source-cache framework. All P01–P24 completion
gates and the full compiler-wide objective remain active.

I100 implementation and initial qualification,17 November at21:09 UTC: the
minimal regression and full LSP script suite pass. The shared observation wrapper
supports local/editor filesystems; the old native-only implementation is removed.
Configured related notes and all configured quick-fix producers now retain message
provenance, including typed-body and failed-inference serialization. Shader/program
return failures use the existing configured failure API instead of losing their
configuration identity in a rendered string. Old compiler images have independent
cache identities; no compatibility reader is introduced.

The fixed454-input candidate image is
`65200c1f7c8883b990f3cb4eae2bd07d910268313c0386397af1f0005d1d40aa`.
The qualified I099 baseline is `e0861113f577b419c8161f112e86d9bf4c920acc3dc571f062799a794d44ae1d`.
`i100-lsp-paired/results.json` retains64 observations, seven edit pairs per case,
on frozen real Breakout revision`d913a7db39d7705b738aeab0db6204ecc801f352`
(45 DynLex files), without launching its GUI. Both disk and editor receive each
edit. All diagnostics and complete token vectors match exactly. Process CPU has
10 ms sampling resolution; the5.7 ms configuration analysis figure is wall time.

| Saved LSP edit | I099 analysis + tokens CPU median | I100 CPU median | Paired CPU ratio,95% interval |
| --- | ---: | ---: | ---: |
| Body |5.08 s|4.95 s|0.96318 [0.96145,0.97441]|
| Lives |4.87 s|4.92 s|1.01027 [1.00495,1.01279]|
| Comment |4.95 s|5.06 s|1.03030 [0.99505,1.03586]|
| Diagnostic wording |4.93 s|0.10 s|0.02028 [0.02000,0.02228]|

Wording analysis wall time falls4.82548 ->0.005694 s. Its process peak RSS falls
429688 ->229700 KiB (paired ratio0.53457). Source-edit peak RSS ratios are
0.97184–0.97547. Cold LSP opening CPU medians are3.245 ->3.235 s; there is no
claim of faster ordinary code-edit analysis or native cold compilation here.
Production changes total303 added/198 removed,105 net lines, including factoring
shared observations and provenance. There is no new disk cache or dependency.
Native controls in`i100-native-controls.json` pass exact executable and diagnostic
parity. Five body pairs have CPU medians1.074762 ->1.082052 s (paired ratio1.00678,
95% interval[0.97382,1.03267]); three cold pairs4.692112 ->4.726275 s (paired ratio
0.99946,[0.99190,1.03099]). There is no demonstrated native improvement. Fifteen
comment pairs have ratio1.01343,[0.99870,1.01867]; fifteen native wording pairs
have ratio1.01262,[1.00529,1.02321], approximately0.4 ms paired CPU cost. Keep that
small cost visible alongside the large editor win, lower editor memory, no new
persisted cache and105 net production lines. Do not retry to erase it.

Qualification completes17 November at21:36 UTC, within the single permitted
extension. The worktree's uncached required suite passes334/0/0 in96469 ms and
full integration passes462/0/0 in714499 ms. Local master is rebuilt with one job,
MemoryMax1792 MiB and swap limited to1024 MiB. Its independently built image
`1efe39ac848a074d557bd1ef64067123d0a603456b5f6d9985187642a0ee6153`
passes334/0/0 uncached in96662 ms and all four focused LSP regressions. A final
minimal regression opens a missing import only in the editor, then closes it;
both profiles repair and restore the import error correctly. That test-only
extension passes on both owner and root images. All454 C++ input hashes still
match the frozen benchmark sources. Implementation`1f4e7e39` and regression
`2e5c319f` are merged into local master; the final qualification record is pushed
with them. The merged worktree is disposed while retaining one current comparison
image and reusable build objects; the superseded comparison image is removed
only after verifying its recorded SHA256.

The current reuse boundary excludes early import/configuration failures and still
reruns ordinary source analysis. Those are unfinished parts of the full objective,
not grounds to mark it complete. The P01–P24 objective remains active.

## Completed iteration: I099 semantic constant queries and blocking tooling bugs

Started17 November2026 at18:44 UTC after I098 publication and fresh worktree
creation; first deadline19:29 UTC, with one further iteration only for concrete
implementation progress.
Move away from the two rejected matcher-storage attempts. P19 remains a verified
missing path: recompileMainDocument creates fresh ParseContexts and calls the
uncached compile pipeline for every editor edit, independently of the native
incremental products.

Complete one shared frontend reuse boundary for LSP edits. First measure actual
edit-to-diagnostic latency/stage work on frozen real Breakout without launching
its GUI, then reuse existing native source/parser or semantic products at their
real consumer. Choose the largest measured avoidable work; avoid a separate
editor semantic implementation or an extraction-only framework with no consumer.
Respect open unsaved documents, configuration edits, imports, target profiles,
source ranges and error repair. Add minimal locality/cascade and token/diagnostic
parity regressions before implementation. Compare speed, peak memory, retained
bytes and code together, including cold/default native controls. Keep a small
clear positive change promptly; if the boundary stalls, record why and move to
another remaining plan stage. Required uncached <=120 s and affected tooling
integration checks remain mandatory. The full P01–P24 objective stays active.

The first read-only baseline completes at18:50 UTC using the qualified I098 image
and the same frozen real Breakout source as native comparisons. Three unsaved
edits per case have median server CPU: rendering6.82 s, gameplay7.39 s,
comment10.13 s and diagnostic wording7.59 s. No GUI is launched; a semantic-token
request provides the completion barrier. The initial claim of no diagnostic
errors was wrong for wording: a later strict check exposes a false config-key
error. Those initial wording observations are not a healthy baseline.
Peak process RSS is roughly408 MiB. CPU samples use the Linux process clock at
10 ms resolution; the measurement helper is platform-specific, not a production
dependency. Attribute compile/import/parse/resolve/inference/destruction before
choosing the shared product boundary. Source/compiler hashes and raw measurements
are retained in i099-lsp-baseline/results.json. These are baseline observations,
not a new performance claim.

At18:57 UTC attribution changes the bounded implementation target. A read-document
barrier separates analysis from highlighting: analysis medians4.07/4.18/4.20/4.84 s
and highlighting2.67/3.31/6.17/3.09 s for rendering/gameplay/comment/wording edits.
An independent background GDB run through scripts/debug.sh records resolve2.33–
2.95 s and inference1.02–1.09 s, while import/section construction total under
60 ms. Its initial wrong-argument and wrong-working-directory observations are
invalid and retained separately. Do not spend this iteration extracting a source
cache for that small share of the work.

First finish the large read-side duplication: directExpressionHasConstant walks
all source expression trees separately for each variable token and definition.
Share an on-demand reference-identity index within one semantic-token request.
Keep instantiation/parameter constant semantics and all variable scope identities;
use no spelling or syntax special cases. A new minimal no-import regression covers
shadowing, repeated queries and constant/runtime edits before changing production.
Then use one fixed seven-pair-per-case real-program comparison of full semantic
token vectors, diagnostics, separate analysis/highlighting CPU, wall time and RSS.
This closes one tool-query boundary; it does not claim the shared incremental
frontend is implemented. Return to that remaining P19 boundary after this small
change if its whole-request benefit is established.

At19:11 UTC the first fixed matrix stops on that config error after all seven
pairs for rendering/gameplay/comment pass complete token/diagnostic parity.
Minimal repro: a three-line config overrides ambiguous operand grouping; native
compilation accepts it, but the LSP's duplicated message-key allowlist rejects it.
Fix validation for arbitrary message IDs, nested variants and quoted empty values
to match the compiler. Remove the duplicated completion catalogue too: load current
builtin message/variant data independently of the unfinished project config.
Focused regressions verify native acceptance, editor diagnostics/completions,
escaping and an unsaved builtin override. Local commits a191bb82/1ef70055 preserve
the separate token-index implementation and generic configuration correction.

Before accepting the configuration path, a second minimal no-library import test
in a directory containing spaces proves that an unsaved message override still
reads its disk value. LspFileSystem manually concatenates file:// with a physical
path, missing the escaped editor URI. Use the shared platform URI utility; commit
0f020872 fixes the provider and the regression then passes. The in-flight matrix
is explicitly stopped and retained incomplete; no failed or interrupted run is
converted into acceptance.

At19:29 UTC use the single permitted verification extension, ending20:14 UTC.
Concrete progress is established: the small index saves several seconds in three
complete seven-pair cases, and both independently minimized tooling bugs are
fixed. Finish verification/publication only; add no optimization variant. The
final frozen image is
e0861113f577b419c8161f112e86d9bf4c920acc3dc571f062799a794d44ae1d.
One new fixed seven-pair matrix includes every final change. It retains raw
diagnostics and requires exact token parity plus exactly one intentional baseline
delta: the old compiler emits the false config-key error and the fixed one removes
it. All other diagnostics must match. Native required and full affected integration
qualification remain pending. The full incremental frontend goal remains active.

At19:34 UTC exact parity stops that final-image unsaved comparison at gameplay
sample4. The retained reproducer records the actual difference: the baseline
number token is one character, while the changed value10 requires two. A separate
two-file no-library regression proves that the old LspFileSystem also ignores
unsaved CWD-relative imports. The existing URI-provider fix already covers it;
no further production change is needed. Commit d5c2890c adds this regression.
The raw failed comparisons and their source/compiler identities remain intact.

The accepted performance protocol must compare equal work. A new fixed seven-pair
matrix writes each edit to both disk and the editor snapshot, so the old baseline
also reads the actual changed imported code. The earlier unsaved timings are
screening/bug evidence only. Complete semantic vectors must match, including
longer numbers and shifted comment lines, with only the explicitly corrected
baseline config-key diagnostic allowed to differ. Unsaved-buffer correctness
is verified independently by the minimal space-containing-path and CWD-relative
import regressions. The final compiler image remains the same e0861113 hash.

At19:50 UTC the fixed saved-edit comparison completes:64 observations, including
28 paired edits. Both compiler images stay unchanged; complete semantic vectors
match, including longer numbers and moved comment lines. Diagnostics match apart
from the exact, intentionally removed false config-key error. The earlier failed
and interrupted protocols are not acceptance evidence.

| Saved LSP edit | Baseline analysis + tokens CPU median | Current CPU median | Paired CPU ratio,95% interval |
| --- | ---: | ---: | --- |
| Rendering function body |8.19 s|5.19 s|.61566 [.60000,.63614]|
| Gameplay function body |8.67 s|5.08 s|.58184 [.56633,.58520]|
| Comment in imported source |12.02 s|5.06 s|.40262 [.38470,.41870]|
| Warning wording |8.46 s|5.15 s|.60422 [.59574,.62264]|

Highlighting alone falls from3.07/3.55/7.07/3.34 s to.10/.11/.11/.10 s.
The analysis stage still runs broadly; do not attribute the whole-request saving
to incremental compilation or claim a native cold-build gain from this change.
Peak-process-RSS paired ratios range.98568–1.00239, effectively unchanged. No new
disk cache, persisted format or dependency is introduced. The combined production
diff is120 added/106 removed lines, net14, including the generic configuration and
URI fixes. This is a favorable speed/memory/disk/code tradeoff under the user's
policy, pending required integration/shared qualification.

Owner required334/0/0 passes uncached in104.360 s after the compiler build.
The full integration suite is running; shared rebuild/required and publication
remain pending. No further optimization variant belongs to this iteration.

At20:04 UTC full integration462/0/0 passes in739.444 s, including the complete LSP
suite and its three new minimal regressions. Required334/0/0 had passed uncached
in104.360 s. The implementation is retained as a191bb82,1ef70055,0f020872 and
d5c2890c. The final image and all450 C++ source hashes remain unchanged. Owner
qualification is complete; shared-master rebuild/required and publication follow.
This closes the measured query duplication and both reproduced provider/schema
bugs, without claiming that ordinary LSP compilation has become incremental.

I099 closes at20:10 UTC within its single verification extension. Local master is
fast-forwarded and rebuilt serially; all450 qualified C++ source hashes match.
Shared required334/0/0 passes uncached in103.812 s. All three new LSP regressions
also pass against the rebuilt shared image
a966c177a57489d9cec5598671aa614a27d417c0643145db6bf094100142f533.
Publish this record and dispose the merged worktree, retaining one verified owner
image for the next comparison. The full compiler-wide goal remains active and
incomplete; I100 is the next bounded reuse boundary, not a completion claim.

## Completed iteration: I098 constraint-source identities

Started17 November 2026 at 17:58:26 UTC; first deadline18:43:26 UTC.
I096 and I097 are preserved as rejected experiments with their original fixed
results. This branch starts from qualified master `1e7ed371`; none of their
storage, binding-format or flex-eligibility changes belong to this candidate.
At iteration start the worktree executable belonged to I097 and was not a valid
baseline; comparison uses the verified retained I095 image instead.

Finish one bounded positive change: avoid rebuilding identical constraint-source
identities and unneeded scope/line indexes. Review I087 source `42c0567f` against
current APIs before applying it. Its original .897%31-pair CPU gain is established;
the investigator's1% rejection floor is superseded, not retroactively passed.
Keep source identity bytes and query invalidation semantics unchanged. Reuse its
minimal identity/line-index regression before implementation, then compare to the
current qualified compiler, including memory/disk costs. Required<=120 s uncached,
clean parity and affected integration remain mandatory. Finish promptly if it
still gives a small clear benefit; do not extend it into another framework.
One45-minute extension only for concrete progress. The full P01–P24 goal stays
active and incomplete.

I098 build/focused verification completed at18:09 UTC. The reviewed patch needs
a current-API adjustment: IncrementalInferenceProducts already owns statistics,
and rootCheckpoint must remain in the hooks interface. Keep both rather than
replacing the newer callback. All three work-item producers provide distinct
stable element/reference/rule-line targets. The baseline fails the new work
counter assertion; current image
`afab27efde4f2e4ab52bd8ad4d8f363a69251df0c592932d6fa0f38ae85a2f3c`
passes locality plus edit/error recovery. Six independent baseline/current builds
preserve exact identity strings for repeated declarations, inline locals and file
number rules. Required334/0/0 passes uncached in102.849 s. A new fixed five-pair
body/lives first/repeated comparison against the current qualified base is running;
it does not replace or retry the old I087 experiment. No C++/header changes are
made during this comparison.

I098 fixed comparison completes at18:18 UTC:64 builds,30 exact native/diagnostic
pairs and four clean diagnostic controls pass with unchanged compiler images.
Rendering first/repeated paired CPU ratios are .99556 [.98553,1.04678] and
.99985 [.94584,1.08735], both inconclusive. Gameplay first is .99284
[.96149,1.01418], also inconclusive. Gameplay repeated CPU medians1.515021 ->
1.471971 s give paired ratio.97158 [.92283,.99013], an established2.84% gain.
Cold CPU controls are inconclusive. Peak-RSS paired ratios range.98701–.99451;
allocated and logical cache bytes are identical in every pair. Keep the small
candidate if correctness qualification finishes: no new persisted format,
unchanged source identity bytes, fewer computations and lower measured memory.
There is no percentage floor and no retry to make other cases significant.
Full integration is running. Complete-IR/fast controls and shared qualification
remain pending. This source change is about60 net production lines; do not fold
any rejected storage/binding mechanisms back into it.

I098 owner qualification completes at18:35 UTC. Integration462/0/0 passes in
741.503 s. Twelve complete LLVM graph builds match baseline/candidate and
clean/incremental output; all images remain unchanged. Fifteen paired comment
and wording controls each retain frontend reuse, zero new objects and zero link
time, with exact executable/diagnostic parity. Their CPU medians increase
26.850 ->27.284 ms and31.642 ->32.190 ms; paired ratios1.01616
[1.00342,1.03307] and1.03145 [1.00858,1.04361]. The small fast-path cost is
real in this experiment; its cause is not established and no retry is used to
erase it. The combined tradeoff remains favorable against the roughly43 ms
repeated-gameplay saving, lower peak RSS and identical disk use. The final change
adds53 net production lines. Shared-master rebuild/required qualification and
publication remain pending. The full compiler-wide goal remains incomplete.

I098 shared qualification completes at18:42 UTC. Implementation `a54360f3` is
fast-forwarded to local master and rebuilt serially under the established
1792 MiB resident/1024 MiB swap/64-task cap. The rebuilt image is
`58edf0de87f5139ba6772c782155e51dcf27bb878788007f11379b53b4f3fa9e`;
all449 C++ source inputs match the frozen owner image's inputs. Shared required
334/0/0 passes uncached in118.445 s, inside the120-second post-build budget.
Owner required334/102.849 s and integration462/741.503 s remain the independent
qualification results. Publish this record, dispose the merged worktree and
retain one verified owner image as the next comparison baseline. No broader
compiler-wide completion is claimed.

## Rejected combined iteration: I097 independent body demands

Started 17 November 2026 at 17:33:12 UTC; first deadline 18:18:12 UTC.
Previous goal turn is progress: it published I095, and I096 changes authoritative
state/evidence with a passing locality regression and a fixed failed whole-build
comparison. Shared master/origin remains qualified `1e7ed371`; I096 is isolated
at `9b5696fc`, not accepted. This distinct combined candidate starts from that
experimental parent and will be compared to the published compiler, not to the
slower rejected image.

Complete one missing producer/consumer boundary: independent flex source/resolution
and lexical binding products. Reuse reviewed source/tests from I085 (`ad1878a1`),
whose standalone matrix showed no gain. Couple it with exact-index lazy matcher
retention only if complete-build reads actually disappear. Do not drop lexical
ancestor dependencies, typed constraints, storage origins, overload/demotion or
error-repair behavior. Add its minimal regressions before applying the code;
preserve stronger demand/work assertions and all independent cache-eviction checks.
Verify current API/ownership changes rather than treating an old patch as qualified.

Use one fixed comparison of the final combined image against `1e7ed371`: fresh
first and repeated body/lives edits on frozen real Breakout, with peak RSS/cache
size, complete artifact/diagnostic parity and actual full-build read counters.
Keep I096's failed result unchanged. The user ranks speed, memory, disk, code and
permits combined gains; no percentage floor applies. If coupling still yields
no net benefit, reject both mechanisms and move to the next stage. Required
uncached <=120 s and full affected integration/LLVM/fast controls precede sharing.
One further45-minute iteration is allowed only for concrete implementation progress.

At17:41 UTC the coupled image builds and passes the focused flex binding/demand
checks atO0/O2 plus indexed-retention, partial-eviction, corruption and missing-
provider recovery. The baseline failed the unmaterialized-flex demand assertion.
Required334/0/0 passes uncached in106.324 s. Current image
`c251de9d01c48431cb320cb2ba3bbfd378cad44d8cbfd7a2575f15ad339ec544`.
The storage counter now subtracts entries when a later consumer loads their payload,
and the final statistics copy happens after late snapshot/graph consumers. This
prevents a publication-time skip from being advertised as a whole-build skip.
The final fixed five-pair body/lives first/repeated comparison is running; no
performance acceptance is claimed. Full affected integration/IR/fast controls
remain pending if the combined result justifies retaining it.

I097 closes at17:49 UTC without performance acceptance. Fixed final-image
comparison:64 builds,30 exact native/diagnostic pairs and four clean controls all
pass; compiler images stay unchanged. Required334/0/0 had passed in106.324 s.
Full integration/IR/fast qualification was not run after the failed comparison.

| Edit | Baseline CPU median | Candidate CPU median | Paired ratio, 95% interval |
|---|---:|---:|---|
| Rendering first |1.129333 s|1.136114 s|1.00600 [.99047,1.02718]|
| Rendering repeated |1.080371 s|1.126569 s|1.02842 [.97891,1.06097]|
| Gameplay first |1.474077 s|1.490036 s|1.00872 [.97510,1.02650]|
| Gameplay repeated |1.419177 s|1.461761 s|1.02758 [1.01287,1.04565]|

Peak RSS is only0.3–0.6% lower; cold allocated cache bytes rise3.46%. Retained
source/resolution owners rise to542 and only7 demand, but1465 references still
restore and1936 cached results are read. Those reads touch every result bucket:
all320 payloads load, with9.28 MB even on repeated rendering edits, and zero
published entries remain unread. Thus the combined variant does not eliminate
the broad physical read. Reject it; shared master stays `1e7ed371`.

Stop this area after the two bounded attempts. A future alternative would need
record-granular storage or source-owner payloads, not another metadata/result
bucket rearrangement. Do not implement that larger storage mechanism now. Move
to the already established I087 constraint-identity duplication: its old31-pair
comparison proved a .897% gain but was rejected solely by a1% floor, superseded
by the user's tradeoff policy. Reassess its small source change on the current
qualified base, with memory/disk evidence and all required correctness gates.

## Rejected storage-only iteration: I096 resolution setup locality

Started 17 November 2026 at 16:54:17 UTC; first deadline 17:39:17 UTC.
Shared snapshot `1e7ed371` is pushed and passes required 334/0/0 uncached in
98.912 s; I095 integration passes 462/0/0. Its merged worktree was disposed and
this branch recreated through the project script; verified build inputs/artifacts
were retained without duplicating the build directory.

Complete one goal: reduce resolution setup by skipping unchanged producer work.
The current real-program first-edit medians are 105.940 ms setup, including
37.038 ms cached-body selection. Attribute the remaining phases before choosing
an implementation: source/class preparation, definition restoration/indexing,
reference tokenization, initial counts and worklist setup. Use bounded debugger
CPU-clock probes rather than changing a central header merely to time phases.
Add a minimal locality/cascade regression before the selected implementation.
Reuse existing dependency/output proofs; preserve last-parameter-use changes,
new/removed overloads, failed lookups, source diagnostics and clean output parity.

Judge CPU, memory, disk and code together in the user's order. Keep small clear
benefits promptly; prioritize the largest measured avoidable work. No percentage
floor or retry-to-pass gate. One further 45-minute iteration is allowed only for
concrete progress. If the chosen boundary stalls, move to the next measured item
instead of extending the same attribution loop. Full required <=120 s uncached
and affected integration/parity checks remain necessary. P01–P24 stay in scope.

I096 first attribution completed at 16:57 UTC: three successful bounded GDB
runs use the inferior process's POSIX CPU clock, read externally via
clock_getcpuclockid/clock_gettime. Debugger sampling time is excluded. Optimized
source lines399/400 share a stop, so data/class preparation cannot be separated
by those markers. Medians: combined preparation9.616 ms; source-outline selection
plus initial collection40.477; definition restoration4.942; reference publication/
tokenization1.559; initial counts7.168; cached-body selection38.574; remaining
setup0.019. The ordinary full-build timer independently records source-owner
selection41.956 ms, versus only2.032 ms creating source-outline nodes.

Prior I003 evidence already warns that digest-prefix matcher buckets mix metadata
and result kinds; simply splitting them can move loading to a later consumer.
Current retained source owners avoid materializing many bodies, so reassess that
boundary using complete-build load/retention work. First split the40 ms owner
selection between InitialParameterCounts preparation, segment loading and reference
metadata restoration, then choose the avoided-work change. Do not duplicate the
body fingerprint cache or treat a moved timer as a gain. No I096 C++ edit yet.

I096 storage-only outcome (17:29 UTC): exact key membership, kind-partitioned
payloads and opaque retention pass the new no-library locality/provider-deletion/
repair check plus existing partial eviction and corruption checks. The unchanged
baseline fails the new locality assertion. Source image
`0f50578336f8c16c89ea9acd469400850f2dea132521da51e4f23350d9d7971d`.
All64 fixed real-program builds pass, including30 exact native/diagnostic pairs
and four clean diagnostic controls. Full required/integration/IR qualification was
not run after the performance rejection; do not describe this as qualified.

| Body edit | Baseline CPU median | Candidate CPU median | Paired ratio, 95% interval |
|---|---:|---:|---|
| Rendering first |1.132343 s|1.113886 s|.98799 [.97691,.99927]|
| Rendering repeated |1.080977 s|1.119464 s|1.03405 [1.02513,1.04379]|
| Gameplay first |1.492254 s|1.494930 s|1.00282 [.98109,1.02388]|
| Gameplay repeated |1.462266 s|1.443652 s|1.00621 [.93760,1.01940]|

Peak RSS falls about1–2%, but repeated-edit allocated cache bytes rise1.6–2.2%
(cold +2.84%). The real build still loads318–320 payloads, reading9.2–10.1 MB;
only31–64 entries remain in wholly unread payloads on repeated edits. Setup improves
but downstream demands/pruning consume almost all payloads. A cold/warm record
census shows1175 failed match records (~1.59 MB) disappear, spreading first-edit
pruning through every result bucket. Do not mistake moved I/O for incrementality.
Reject the storage-only variant: its +3.4% repeated-rendering cost outweighs the
small first-edit and memory gains. Preserve `i096-first-edit-comparison.json` and
`i096-record-kind-census.json`; no retry replaces this outcome.

Next goal is the actual body consumer boundary. Prior I085 source `ad1878a1`
already implements independent flex source/resolution and lexical binding products,
passes138 focused builds and required333/0/0, but alone had no whole-build gain.
Assess a distinct combined variant against published `1e7ed371`: complete that
body-demand boundary and test whether payloads can then stay unread. This is not
acceptance of either rejected image. Keep the previous failures and measurements;
if the coupled variant has no net benefit, drop it and move on. Shared production
remains `1e7ed371` until a complete qualified change is measured.

## Completed iteration: I095 graph snapshot and persistence

Started 17 November 2026 at 15:43:48 UTC, after I094 publication; first deadline
16:28:48 UTC. Master/origin `3ffcc606` passes shared required 334/0/0 uncached
in 108.627 s. The prior worktree was disposed and this branch recreated through
the project script, retaining verified unchanged build inputs/artifacts.

Target the measured broad snapshot/persistence work described in the priority
section below. First distinguish graph transformation from CBOR serialization
using bounded debugger probes; do not infer that their combined timer is all
avoidable conversion cost. Then implement reuse of unchanged graph records (or
a simpler representation if measurements support it), with a minimal regression
before the implementation. Preserve exact decoded dependency information,
add/remove/negative dependency cascades and bounded cache storage. Compare full
build speed, peak RSS, disk and code complexity using the user's ranking and
combined-effect criterion. Finish a clearly positive bounded change and move on;
allow one extension only for concrete implementation/measurement progress.

The user's explicit tradeoff order is speed, memory, disk, code size; judge their
combined effect. There is no fixed percentage floor. Keep bounded, clearly positive
changes; larger mechanisms need proportionate gains. The prior investigator-chosen
5%-in-every-case gate is superseded, while its inconclusive evidence stays recorded.

At 16:00 UTC the debugger probe confirmed both compact conversion and CBOR are
material work, but its wall measurements are affected by debugger memory pressure
and are not acceptance benchmarks. A standalone codec comparison and full-build
pairs will provide CPU/resource evidence. The candidate preserves canonical shared
dictionary numbering and compares encoded inference rows against current tables,
retaining identical rows in place. It adds no persisted fields or append-only table.
Regression written first: unchanged source fails the row-reuse assertion. Cover
roundtrip fidelity, source versions, value observations, key insertion/removal,
null-version match dependencies and changed path identities.

At 16:15:56 UTC, required passes 334/0/0 uncached in 101.136 s. Candidate image
`af4c633d9fac9e840e43259c5bd5ce0be28e373fdfb4925be6ea845ddb52ce01`.
The isolated graph conversion check measures 16.3915 -> 14.0130 ms CPU (40 samples
after one warmup), with identical serialized graph bytes. A smaller borrowed-string
variant is 16.6025 ms and is not retained. These are isolated measurements, not a
claimed whole-build speedup. Full paired CPU/RSS/cache-size comparisons are running.

Row retention does not complete P19/P20: inference fields and current shared tables
are still visited, capture remains broad, and CBOR writes the complete payload.
Do not equate 8,438 reused rows (many empty) with 8,438 expensive computations saved.
The candidate adds roughly 100 production lines, no dependency or persisted field.
Finish the bounded comparison rather than constructing another storage layer here.

I095 fixed full-build comparison completed at 16:20:55 UTC: 64 builds, five
fresh-cache paired samples per body/lives case, first and immediate second edits,
and four clean diagnostic controls. All 30 native artifact/diagnostic pairs match
exactly; compiler images remain unchanged. Evidence: `i095-first-edit-comparison.json`.

| First edit | Baseline CPU median | Candidate CPU median | Paired CPU ratio, 95% interval | Paired peak-RSS ratio |
|---|---:|---:|---|---:|
| Rendering body | 1.210075 s | 1.206188 s | .98817 [.96423, 1.05698] | .98176 |
| Gameplay lives | 1.552062 s | 1.553940 s | .99766 [.95219, 1.05670] | .98224 |

Whole-build speed is inconclusive; do not claim those paired estimates as gains.
Peak RSS is consistently lower: first-edit medians 270212 ->265912 KiB and
293528 ->287744 KiB; cold/second-edit ratios are also below parity. The GNU-time
metric includes inherited child maximum, not summed simultaneous process-tree RSS.
Allocated and logical cache bytes are identical for every paired build. Cold CPU
controls are also inconclusive. Retain the bounded implementation if correctness
qualification finishes: isolated conversion is clearly faster, peak RSS improves,
and no format/storage cost is added. Do not re-sample to manufacture a speed claim.
Full integration started 16:21 UTC; complete-IR and fast-path controls still remain.

The first deadline is reached at 16:28:48 UTC with concrete implementation,
passing required and full paired/resource measurements. Use the single 45-minute
verification extension (deadline 17:13:48 UTC) for the running integration suite,
complete-IR/fast controls, shared-master verification and publication. No further
optimization variant or statistical re-sampling belongs to this iteration.

Full integration passes 462/0/0 in 749.739 s. The service journal records a
1.4 GiB peak under the 1.5 GiB/no-swap/64-task cap. Intermediate cgroup event
captures show no denials/OOM events; do not present a partial capture as an
end-of-run event audit. All 12 complete LLVM graph comparisons also pass against
the baseline and clean builds. The C++ regression now uses the producer's actual
object-shaped lookup/global-input fields and its direct helper recheck passes.
The Python codec wrapper now includes the C++ subprocess's real stdout/stderr
on failure; the earlier wrapper exposed only a generic CalledProcessError.
Fast controls and shared-master qualification/publication remain pending.

I095 fast controls finish with 68 successful builds: all measured comment and
wording edits reuse the frontend with zero objects/links, and native/diagnostic
parity passes. Preserve the original timing result: comment CPU26.018 ->26.770 ms
(paired ratio1.03710, interval[1.02051,1.03865]); wording30.609 ->31.563 ms
(1.03143,[1.02301,1.03884]). Driver totals are nearly unchanged for comments and
+0.23 ms for wording, so most extra time lies outside that timer.

One bounded diagnostic follow-up tests --version (64 pairs) and comment builds
with/without statistics (31 pairs each). Bare startup is +0.2265 ms paired median;
loader relocation counts are unchanged. Normal comment CPU medians27.748 ->27.653 ms
(ratio.98923,[.98320,1.00164]); statistics27.956 ->27.963 ms
(.99327,[.98828,1.00425]). This follow-up changes only the edited chip-file mtime,
whereas the original harness refreshes five source files: it is diagnostic evidence,
not a replacement for the original controls. The original extra time has no
established precise cause; do not claim it disproved or attribute it to LLVM.

Finish and retain the bounded implementation under the user's combined-effect
criterion: conversion -2.38 ms CPU, first-edit peak RSS around -2%, unchanged disk,
no new persisted state, about115 production lines. Whole-build CPU remains
inconclusive and the original fast-path timing difference stays visible. Do not
spend another iteration chasing a sub-millisecond attribution. Shared-master
verification/publication and the larger resolution-setup goal are next.

Final shared-master qualification at 16:51:17 UTC: required 334/0/0 uncached in
98.912 s after the compiler build. Shared compiler image
`07df1db6634b160605a96a9c7cac249562948406e4dfecebdbbb2f90628008b9`;
implementation `3728eb66`, cost/error-reporting record `a5eca16e`. Code and codec
regressions pass; complete native/LLVM parity and the original inconclusive or
regressed timing controls remain recorded above. Publish the qualified snapshot,
dispose its worktree and keep the complete compiler-wide goal active.

Next bounded goal: reduce resolution setup by reusing unchanged setup products.
The current first-body-edit medians are 105.940 ms setup, including 37.038 ms
cached-body selection. First attribute the remaining work between definition
restoration/indexing, reference tokenization, initial counts and source/class
preparation, then implement the largest justified dependency boundary. Retain
last-parameter-use, overload-add/remove, failed-lookup and clean-diagnostic parity.
Do not extend this iteration into a second serialization-attribution loop.

The full cross-process incremental goal remains incomplete. Do not mark this
snapshot or one optimized stage as completion of P01–P24.

## Completed iteration: I093 recursive flex argument-pack crash

Started 17 November 2026 at 13:20:48 UTC; first deadline 14:05:48 UTC.
The user explicitly prioritizes confirmed compiler problems before further
performance work. First reduce the no-cache stack overflow from I092 to a
no-import reproduction, identify why the existing recursive-expansion identity
does not detect the repeated call, and fix that producer/consumer contract.
Do not add a recursion-depth threshold, syntax special case, or exception mask.
Add the minimal regression before the implementation; preserve valid finite
compile-time recursion and nested section-body execution. Verify focused cases,
the uncached required suite within 120 seconds after building, and affected
integration coverage. Builds and tests run serially in bounded memory groups.

One further 45-minute iteration is available only after concrete progress.
After the crash fix is qualified, resume P03/P06 dependency-output boundaries:
the first edited build changes a lookup input key despite byte-identical match
outputs, invalidating unchanged inference/global products. Keep every other
P01–P24 boundary in scope and let the fixed correctness/performance gates decide
which implementation to retain. The complete incremental objective is unfinished.

I093 evidence at 17 November 2026 13:45 UTC:

- The original 376-line no-cache overflow reduces to six lines with no imports:
  extract a pack element into a local reference, then forward that reference to
  the same variadic replacement. The unchanged compiler exceeds a three-second
  watchdog. GDB shows identical definitions/types/value arguments but a newly
  wrapped capture and growing binding stack on every expansion.
- `inferArgumentPack` forwards existing packs but did not forward existing
  `ExpressionCapture` values. Preserving that reference fixes its identity and
  caller scope. The minimal regression now reports the existing recursive-flex
  diagnostic in 14 ms; the original reproducer reports it in 37 ms.
- Required qualification: the first candidate run hit the unchanged 120-second
  watchdog. A control with the unchanged compiler passed 333 checks in 116.550 s.
  The candidate rerun passed all 334 checks uncached in 106.181 s. This is a
  correctness qualification, not evidence of a suite performance improvement.
- The affected full integration suite is running. No snapshot is qualified for
  merging until it passes. Compilation remains serial and memory bounded.
- A separate, unevaluated direct-extraction recursion also exceeds the watchdog;
  forwarding known references does not resolve that case. Keep it open rather
  than claiming a general termination proof for lazy source transformations.
- The first-edit lookup probe finds identical source text, pattern elements,
  keyframes, numeric spellings and ancestor scopes. Compare the complete key
  buffer next; do not discard input dependencies without proving the output
  boundary and testing changed overloads and failed lookups.

At 13:59 UTC the first full integration run finished 461/1/0 in 760.126 s.
The only failure was the Vulkan runtime, caused by this investigation's
`TasksMax=32` service limit, not the compiler patch. Both unchanged and candidate
compilers pass its isolated check with 64 tasks and segfault with 32. The
unchanged-compiler control records three denied creations in `pids.events` and a
32-task peak. Preserve memory limits but allow 64 tasks for test processes.
The incremental overload-addition/removal recovery check also passes against
clean builds. Full corrected-environment integration and the final required run
(including that additional regression) remain to be recorded before publishing.

Final qualification completed at 14:16:27 UTC: corrected integration passed
462/0/0 in 761.497 s, and required passed 334/0/0 uncached in 112.326 s, including
the added overload recovery check. The qualified compiler image is
`864cbe7dd756a3014af474d68d671267f11a043d1a6558f5e7131d45eb8fd2cd`;
the implementation commit is `bf4b74c1`. The service recorded no task denials,
memory-limit hits, OOM events or swap use. The earlier timed-out required run
and incorrectly task-limited integration failure remain recorded above.

The sole qualification extension is justified by the implemented crash fix,
passing required/recovery tests and verified test-service thread-limit failure.
Its final deadline is 14:50:48 UTC. Do not publish the snapshot before corrected
integration and required results exist.

The complete 1,810-byte lookup fingerprints now differ only at bytes 16–79:
the candidate-tree digest. Source representation is not the cause. Rematching
correctly revalidates candidate changes, but downstream inference consumes raw
lookup input versions despite unchanged selected results. A 14-line, no-import
regression adding a nonmatching overload reproduces a changed constructor source
input (`i094-red-lookup-results.log`). The next implementation should share the
existing resolved-match identity machinery across source-owner units, retaining
raw reference keys for lookup validation and replay. Set a separate bounded
performance gate before retaining that optimization.

## Completed implementation: I094 source lookup output boundaries

Started 17 November 2026 at 14:19:27 UTC; first deadline 15:04:27 UTC.
I093 passed both complete suites and was merged into local master `aefbd3a5`.
Its worktree was disposed and recreated through the project script; verified
unchanged build-input timestamps retain its existing build artifacts. Shared
master qualification/push is in progress; no I094 compiler change is published.
Shared I093 qualification subsequently passed 334/0/0 uncached in 112.003 s;
master and origin now both point to `aefbd3a5`.

The first I094 C++ build hit its private memory limit at 14:26:54 UTC. Kernel
logs identify `CONSTRAINT_MEMCG`, the `dynlex-i094-build.service` cgroup and
Clang PID 3508568: 2 GiB RAM/128 MiB swap was insufficient for the incremental
compiler translation unit. This was a job-local kill, not a host OOM. After
checking 3.6 GiB available, retry serially with a 2.25 GiB RAM/512 MiB swap cap;
do not remove the resource limit or start another heavy job.
The no-import regression is `check_source_lookup_results`; it initially failed
because a nonmatching overload changes an existing constructor query's source
input. Preserve clean/incremental execution and diagnostic parity when the
overload is added and removed. Reuse `resolvedFunctionMatches`' ordered semantic
identities for source-owner reference units, including flex/class/lifecycle
owners. Keep initial/final input keys for matcher validation and replay, and
retain semantic output products across deferred source owners. Migrate every
projection producer and consumer; do not introduce a cache-format fallback.

Before retaining the optimization, require the real frozen LEXI8 program's first
body and lives edits to restore all four unchanged global type products and
reuse entry inference. Measure seven paired baseline/candidate samples per edit,
each starting from a freshly populated cold cache. The fixed gain gate is at
least 5% median summed CPU improvement in both first-edit cases, with a paired
95% bootstrap interval excluding no improvement. Record elapsed time too.
The cold populations and immediate second edits serve as controls: reject a
measured regression exceeding 5%. Preserve comment/wording fast paths and run
the required/integration correctness gates before publication. Use one heavy
job, bounded memory and one live cache per compiler/sample; remove disposable
payloads after preserving raw measurements.

If that window cannot produce a qualifying implementation, allow one extension
only for concrete code/measurement progress. Otherwise record the result and
move to a different P01–P24 boundary rather than starting another attribution
iteration on the same first-edit issue.

I094 measurements completed at 14:42 UTC (88 compilations, unchanged compiler
images). The minimal regression now passes all six builds and clean execution
comparisons. Every one of the 14 candidate first edits restores all four global
type products and reuses entry inference; both baseline cases restore only two.
All 42 baseline/candidate cold/first/second native artifact and diagnostic pairs
match exactly. Four separate clean builds match diagnostics; native clean and
incremental code use different optimization pipelines, so raw executable equality
is not their correctness oracle. Complete LLVM-graph parity is being checked.

| First edit | Baseline median CPU | Candidate median CPU | Paired median gain | Paired ratio 95% interval |
|---|---:|---:|---:|---|
| Rendering body | 1.497493 s | 1.309383 s | 11.32% | 0.88043–0.89831 |
| Initial lives | 1.754988 s | 1.718758 s | 4.86% | 0.94014–1.03367 |

Cold-population paired CPU changes are +0.91%/+1.26%; immediate second-edit
changes are -3.34%/+2.82%. No control's measured median regression exceeds 5%.
The originally declared both-case gain gate is **not met**: lives is below 5%
and its interval includes no improvement. Do not present that gate as passed.
The user questioned discarding gains below 5%; the proposed replacement is a
reliably measured benefit that justifies its complexity without material
regression, rather than a minimum percentage in every case. Nothing has been
discarded or published while that decision and correctness validation remain open.

User acceptance update (17 November, after these measurements): keep a change
when it undoubtedly gives more than it takes; a large new mechanism for a 2%
gain is not justified. This supersedes the investigator's minimum-percentage
gate. Assess the implemented mechanism's complexity and measured benefit, retain
all raw results (including inconclusive cases), and require correctness and
control checks. I094 shares existing per-reference identity code and adds a
small source-owner result mapping, with a verified 11.32% rendering-case gain
and no measured material control regression. The lives estimate is inconclusive;
do not claim a proved improvement there. Retention is pending the remaining
parity, fast-path and suite checks, rather than a 5% floor in each case.

The user further directs that a small but undoubtedly positive change should be
finished promptly, then work should move on; prioritize the largest gains first.
Finish I094's running correctness gates without more lives-case sampling or
percentage tuning. Select the next P01–P24 target from measured avoidable whole-
build cost, not from a small inconclusive timing difference.

User tradeoff priority: **speed, memory, disk space, code size**, in that order,
with combined effects considered. Doubling speed can justify doubling code, but
the added code still needs explanation; 10% more memory for 5% more speed is
questionable; 50% less disk for a 3% slowdown can be worthwhile. Assess measured
resource cost rather than using a percentage floor or LOC count as the decision.
Before publishing I094, add a bounded peak-RSS/cache-size comparison because its
timing samples currently account for CPU/elapsed time but not per-build RSS.

Further I094 checks: all 12 complete-LLVM-graph builds/comparisons pass for
baseline/candidate and clean/incremental body/lives edits. All 92 comment/wording
control builds pass, including 42 paired native-artifact comparisons and clean
diagnostic checks. Every measured fast-path run reuses the frontend, builds zero
objects and spends zero time linking. Candidate median CPU is 29.383 ms for
comments and 34.704 ms for wording. Comment paired CPU ratio is 0.96511
(95% interval 0.96236–0.98976); wording is 0.97551 (0.94447–1.03294), so retain
the latter as inconclusive. Both scan the same 3,164 cache entries as baseline.
The complete required/integration suites are now running against the unchanged
`52f21249dc3999e483bf79650cce125cb110f8cbaa67916551faa32b46a184a7` candidate.

Required passed 334/0/0 uncached in 117.635 s. The first integration run finished
461/1/0 in 828.353 s; only `unary_arithmetic_types` exceeded its unchanged
20-second limit. Its 250-process batch subsequently passes unchanged in an
isolated comparison: baseline 17.82 s (5.82 user/11.84 system CPU), candidate
17.90 s (5.89/11.93). This does not reproduce a material candidate slowdown;
do not claim a precise cause for the full-run timeout from those controls alone.
Keep the test and timeout unchanged. A full integration recheck is running with
1.5 GiB memory headroom and 64 tasks; capture cgroup pressure/denial counters.

Resource comparison is complete: 24 builds, three fresh-cache paired samples
per edit, fixed compiler images, identical frozen program. Native compile peak
RSS and allocated cache bytes are:

| Stage | Baseline median RSS | Candidate median RSS | Baseline allocated cache | Candidate allocated cache |
|---|---:|---:|---:|---:|
| Cold, rendering case | 384,272 KiB | 386,584 KiB | 39,723,008 B | 39,927,808 B |
| First rendering edit | 274,864 KiB | 264,936 KiB | 57,806,848 B | 57,266,176 B |
| Cold, lives case | 384,308 KiB | 386,240 KiB | 39,723,008 B | 39,927,808 B |
| First lives edit | 290,636 KiB | 289,480 KiB | 57,622,528 B | 58,056,704 B |

The rendering edit's paired RSS ratio is 0.96627 and allocated-disk ratio
0.99065. Lives ratios are 0.99874 and 1.00753. Cold ratios are about 1.005 for
both RSS and allocated disk. Production C++ changes add 40/remove 12 lines,
sharing existing machinery and adding no dependency. These measured costs
support the user-approved tradeoff; complete integration qualification remains
required before publication.

Final owner qualification completed at 15:30 UTC: the unchanged candidate passes
all 462 integration checks in 773.972 s, including the original unary-arithmetic
test and 20-second timeout. Its service recorded zero memory/task denials, zero
OOM events and zero swap use. Together with the 334-check uncached required pass,
the graph/artifact parity checks and resource comparison, I094 is qualified for
shared-master verification and publication under the user's stated tradeoff.
The earlier integration timeout remains recorded; no test or limit was relaxed.

Use the sole extension, ending 15:49:27 UTC, to finish complete LLVM parity,
comment/wording controls and required/integration validation of this implemented
boundary. The concrete progress is the passing minimal regression, verified
stopped cascade and measured rendering-case gain. Do not repeat samples simply
to cross a threshold or start another attribution loop on the same issue.

## Next priority after I094: P19/P20 graph snapshot and persistence

Use the largest measured avoidable common cost, following the user's ranking.
In the current candidate's seven first-rendering-edit samples, snapshot plus
cache writing has a 238.839 ms median. Those scopes are sequential, although
their sub-timers overlap: graph construction is about 66 ms, and graph compacting
plus CBOR serialization is 34.465 ms. One object changes, but the snapshot visits
4,219 sections and encodes 9,878 records (7 columns rebuilt, 4 retained).
This is a larger combined target than another small timing variation in I094.

After publishing I094, start one bounded implementation iteration on retaining
unchanged graph records through snapshot/serialization. Inspect producer dirty
sets and dictionary-ID stability first; preserve changed/deleted edges, query
variants, negative reads, global-type products and cache cleanup. Keep storage
bounded and remove obsolete records. Reuse existing mechanisms; do not introduce
a general storage engine speculatively. Measure complete-build time, peak RSS,
logical/allocated cache bytes and code cost together. A simpler encoding is also
eligible if its speed/memory benefits justify measured disk cost. Use the same
real program and full correctness gates; keep the full P01–P24 goal intact.

Resolution setup (about 118 ms, including about 42 ms of cached-body selection)
is the next measured frontend candidate if graph persistence cannot produce a
positive tradeoff within its bounded iteration. Do not spend another iteration
trying to force the inconclusive lives percentage over a threshold.

## Full-objective status

The complete goal is still active. A qualified optimization is a snapshot, not
proof that every stage is incremental. The P01-P24 inventory below retains the
complete scope. Snapshot publication requires shared-root qualification and an
origin push, separately from owner test passes; the iteration record above
tracks those gates. Current combined image0d28feda passes focused regressions,
all391 required checks, native LSP, the actual rebuilt browser LSP and the
five-case native parity matrix. Its full integration and shared-root results
are recorded above when complete. Grouping execution, constant-observation,
browser-build, matcher literal-priority and reviewed portability corrections
must all survive integration. Current evidence and remaining
boundaries are:

| Requirement | Verified coverage | Remaining work |
|---|---|---|
| Source/config edits | File preparation, preprocessing chunks, source observations and outlines have retained products. | Semantic section/declaration objects still reconstruct broadly after active edits. |
| Parameter classification | Focused tests cover removing the last parameter use, dependent reclassification and recovery. | Broad eligibility/index consumers still run; prove locality across every declaration and scope change. |
| Positive/failed pattern lookups | Persistent match, binding and constraint reads retain successful and failed dependencies. | Some matcher catalogs, validation and persistence still process broad program state. |
| Type inference | Ordinary concrete bodies, constraint results, diagnostics and validated global products can replay. Focused grouping tests now preserve first-execution state, negative observations, recursive completion and constant results. | Current first body edit still infers32 bodies versus the baseline's1; successive edits fall to11 and1. Its first matched inference stage is934ms versus262ms, while source preparation is essentially unchanged. Resolve this actual inference work before parser micro-optimizations. Statement/block/CFG products and complete locality across source/configuration changes remain unfinished. |
| Diagnostics | All62 wording benchmark invocations update existing warnings with zero objects/linking; focused tests cover error removal and diagnostic movement. | Complete locality across all early-error and configuration changes remains unproven. |
| LLVM IR | Native/debug function and entry closures reuse; LLVM text has an incremental driver. I094 passes12 complete-graph builds/comparisons and42 native artifact pairs; required/integration checks include debug edit/parity coverage. | Whole-module work remains, and tracing excludes major paths. |
| Objects/link | Unchanged objects reuse; ordinary body benchmark rebuilds one object; fast paths skip linking. | Necessary executable changes use a conventional full link. Other output drivers still need shared products. |
| Tooling/targets | Native/LLVM reuse; editor source syntax and matcher products share current source identities. I107 improves measured editor updates about7%. | Ordinary editor type inference still runs; object-only, WebAssembly and SPIR-V do not share the complete persistent pipeline. |
| Required budget | Current combined compiler391/0/0 uncached in67.745s. No fixtures or assertions were removed; native checks and fixture workers remain independent. | Verify the updated shared-root suite with master1a9f9a90 and preserve the120s post-build budget. |

## Iteration I116: reuse frontend and IR for native object output

Object mode now uses the existing incremental frontend and per-function IR.
A single output object has one artifact key. An unchanged destination can keep
its bytes; a moved or damaged destination restores the object, and an evicted
object is produced from saved IR. A changed object still lowers the whole
module. No linker is involved. Emission to a pipe previously aborted because
LLVM needed a seekable stream. Both pipelines now lower into a temporary
seekable file when they produce a new object for stdout.

The candidate 9178ec67 passes focused object and LLVM parity at O0 and
debug O2, including artifact eviction, output recovery and warning-only
configuration changes. The existing native C ABI fixture passes as well.
On three paired samples per case using the 45-file LEXI8 Breakout source,
median object compilation CPU time is 6.792 to 4.011 seconds for a body edit,
5.743 to 0.042 seconds for a comment edit, and 5.989 to 0.039 seconds for a
warning wording edit. The edited builds reuse 367 function IR products.
Comment and wording edits perform zero optimization, object lowering or
linking. Each edited object matches the clean object bytes and diagnostics.
The three cache-disabled controls are insufficient to establish a cold speedup.
The focused comparison is complete; shared-branch suite qualification follows.

## Prior iteration: I092 first-edit global-product attribution

Started17 November2026 at04:42:41 UTC; first deadline05:27:41 UTC. The previous
goal turn made progress: it qualified, merged and pushed I091, verified the shared
compiler and disposed its merged worktree. Revalidate current master d03a8e42 and
the published7238ae26 image before this new investigation. The full goal remains
active and incomplete.

Inspect why the first edit restores two global
type products while later edits restore four. Identify the failed input proof on
the frozen real program, minimize it without library imports, then choose a fixed
implementation/performance gate. Do not relax eligibility from saved output types
or repeat a completed failed variant. This does not replace the other boundaries.
Use one heavy job and project-wrapper debugger probes. Stop attribution when the
actual missing producer/consumer contract is identified; choose implementation
only from that evidence. One45-minute extension requires concrete progress.

## Completed iteration: I091 demand source anchors by owner

Started17 November2026 at03:00:33 UTC; first deadline03:45:33 UTC. Use the sole
progress-based extension through04:30:33 UTC for the repaired frozen candidate's
verification. No second implementation extension is available. The previous
question-only turn made no goal progress; this continuation checked terminal
build/test evidence before starting further work. The full objective is incomplete.

The first frozen candidate passes23 focused and189 broader builds at each of
O0/O2, plus333 required checks uncached in115.205s. Its31-pair native body case
improves median CPU1.459551 ->1.310088s, paired ratio0.894489 with95% interval
[0.889053,0.909445]. Native controls and cache-disabled cold show no established
regression; the lives case improves about8.6%. Those results belong to image
ee23232d, which remains unqualified: its debug control fails exact output parity.

An11-line no-import reproduction identifies the source of that failure. Entry
import reconstructed the compile unit's global debug list from name-sorted LLVM
symbols instead of its saved declaration order. It also emitted an empty debug
type table when no records existed. Repair both producers; the rebuilt image
1678b655 passes full LLVM/executable parity for scalar/class reproductions and
33 focused O0 builds. Run the complete original gates on this distinct image.

The old frozen benchmark also forced the LLVM baseline cold. That LLVM timing
cannot establish incremental improvement against the current qualified compiler.
Preserve it as invalid comparison evidence and use equal incremental modes in
the repaired candidate's frozen harness. Native comparisons were unaffected.

The next implementation target is the repeated whole-program source-anchor
producer identified by I090. Make body/diagnostic source mapping index only the
semantic owners whose anchors are requested. Preserve the exact existing portable
identities: owner, text digest and duplicate occurrence. Keep generated physical
origins, source slices, inline shorthand, repeated imports, duplicate lines and
late binding publication correct. Reuse one hashing/identity algorithm with debug
metadata; do not introduce a second anchor format or compatibility path.

Add minimal no-library tests proving unrelated owners are not indexed, plus
current-source diagnostic/debug locations after inserted/deleted/moved lines.
Expose actual indexed-owner/line work counters. Use the isolated I089 prerequisite
and compare the combined frozen candidate with qualified d95dda4b once:31 real
body pairs need>=5% paired median CPU gain with95% interval below parity. Retain
the original controls, full clean IR/diagnostic parity, affected integration and
uncached required<=120s. Start its45-minute clock when implementation begins;
one extension remains conditional on concrete progress. First-edit entry reuse
and every other unfinished stage remain part of the full goal.

### I091 repaired-image performance gates passed

Image1678b655 is unchanged across all473 C++/header/inline and22 C inputs.
The fixed primary and all nine controls pass. The corrected LLVM comparison uses
incremental mode for both compilers and verifies their complete emitted graphs.
Native/debug comparisons require identical executable bytes and diagnostics.

| Edit/output | Pairs | Qualified baseline CPU median | Candidate CPU median | Paired change |
|---|---:|---:|---:|---:|
| Active function body / native |31|1.463039s|1.303654s|10.7% faster|
| Game behaviour / native |15|1.853551s|1.675546s|9.5% faster|
| Active function body / LLVM |7|2.246058s|2.077739s|7.3% faster|
| Active function body / debug |7|1.747953s|1.575317s|9.9% faster|
| Unused ordinary function |15|0.537764s|0.533635s|No established change|
| Unused flex definition |15|0.524050s|0.528402s|No established change|
| Unchanged |31|0.028870s|0.028747s|No established change|
| Comment only |31|0.029203s|0.029138s|No established change|
| Warning wording only |31|0.035371s|0.034567s|No established change|
| Cache-disabled cold |11|5.309385s|5.294398s|No established change|

Percentages use median paired CPU ratios, not ratios of the marginal medians.
Every claimed gain has its95% paired interval below parity; no control has an
established regression. All186 measured fast-path builds reuse the frontend and
build zero objects with zero link time. Each wording sample updates existing
warning text. Required333/0/0 passes uncached in115.275s, and again in110.016s
after extracting the existing corruption test to keep code files below1,000 lines.

The primary edit normally infers one body, reuses367 LLVM function bodies and
builds one object. Anchor readers index166 lines across50 owner requests; these
are actual construction counts, including requests by separate readers. Entry
inference reuses30/31 edits. On the first edit only two of four global type
products restore; subsequent edits restore all four. That is a measured remaining
gap, not an identified root cause or a completed incremental boundary.

Full integration462/0/0 passes in795.912s. All80 clean/incremental IR builds pass
in native, native debug, LLVM text and LLVM text debug modes, plus16 exact fresh/
incremental native/debug executable comparisons. Retained native/debug bitcode
and complete LLVM artifacts match their report hashes. DWARF verification passes.
The frozen candidate is qualified; local-master rebuild and post-disposal proof
are next. Do not repeat completed timing comparisons. No new implementation
variant belongs to this iteration's verification window. Evidence:
`i091-qualification-audit.json`, `i091-fresh-native-output-audit.json`,
`i091-performance-audit.json`,
`i091-fast-path-audit.json`, `i091-debug-fixed-stage-analysis.json`,
`i091-first-edit-gap.json` and the ten `i091-debug-fixed-performance-*.json` reports.

### I091 shared publication proof

Candidate qualification closes17 November2026 at04:26:11 UTC, before the sole
extension deadline04:30:33. No subsequent implementation variant or repeated
timing comparison is introduced. Local master merges the qualified snapshot at
a75a8992. Its serial58-step C++ rebuild passes in632.514s; that build is outside
the required suite's post-build budget.

Shared compiler7238ae26 passes333 required checks uncached in115.499s, all190
focused anchor/entry/global builds across O0/O2, LLVM ordering/output/debug checks
and DWARF verification. Its473 C++/header/inline and22 C source inputs match the
qualified candidate exactly. Retain the one qualified worktree build with input
hashes and mtimes, dispose the merged worktree/branch, then verify15 actual native,
debug and LLVM builds through the shared compiler. Those post-disposal builds
pass, including two code edits, fast comment/wording/unchanged paths and LLVM
output restoration. The shared executable is archived and hash verified.

Keep the measured optimization. This is a published snapshot of the active full
goal, not its completion. Semantic section construction, broad query consumers,
entry statement/block products, first-edit global products, whole-module work,
tracing and other drivers remain in scope. Next perform the first-edit attribution
specified above. Evidence: `i091-root-qualification-audit.json`,
`i091-root-build.json`, `i091-root-focused-audit.json`,
`i091-root-disposed-replay-final/report.json` and `i091-implementation-close.json`.

## Completed iteration: I090 entry-replay consumer attribution

Started17 November2026 at02:49:48 UTC; first deadline03:34:48 UTC. I089 is
preserved at its original failed5% gate. Use its frozen image only for read-only
attribution; shared compiler d95dda4b remains unchanged.

Goal: locate the extra work inside preinference and the combined exposed-function/
managed-lifecycle stage before choosing another optimization. The latter includes
activation/restoration of replacement bodies; it is not solely exposed-function
validation. Its40.375ms and the33.898ms body-load timer can overlap.

Run two bounded project-wrapper debugger probes on successive edits of the same
frozen real application. Measure controller setup/requests, producer validation,
changed-unit inference, class/entry restoration, replacement activation and managed
validation as inclusive scopes. Record instrumentation overhead and do not use
these debugger timings as CPU acceptance data. Retain first-edit cold-cache reuse
as a separate incomplete requirement. Choose a subsequent implementation only
after identifying an actual repeated producer or unnecessarily demanded product.
No retry of either rejected source/image performance comparison is authorized by
this attribution task. The complete incremental objective remains active.

### I090 verified outcome

The first method-name debugger probe is invalid: optimized inline locations
triggered repeated entry breakpoints and broke its finish-stack accounting. Retain
that observer failure; it supplies no usable timings. Two corrected one-shot
source-boundary probes complete on successive edits. Root diagnostic restoration
takes29.598/31.071ms. Replacement activation contains33.812/33.150ms of body
setup/restoration; the remaining managed walk after activation is0.175/0.165ms.
The inner body-reader entry was optimized out, so these probes do not separate
its setup from decoding. These are inclusive debugger timings, not CPU acceptance.

Two additional bounded probes observe four calls per edited build to the actual
`sourceLineIdentities` producer. Every call hashes all7,514 program lines:
changed-unit product capture, root diagnostic restoration, replacement-body
restoration and final snapshot capture. Each producer call takes9.319-12.248ms
under the debugger. `InferenceBodySources` then rebuilds both encoded/decoded
line maps and all declaration mappings for each consumer. Its constructor invokes
that complete producer unconditionally. This is verified repeated work, rather
than an inference from overlapping parent stage times.

Select demand-driven source anchors as the next boundary. Shared compiler/source
remain unchanged. Evidence: `i090-entry-work-profile/boundary-report.json`,
`boundary-probe-1.json`, `boundary-probe-2.json`, `line-report.json`,
`line-probe-1.json`, `line-probe-2.json`. I090 changes documentation/evidence only;
it does not qualify either prerequisite compiler variant or complete the goal.

## Rejected iteration: I089 complete entry-input products

Started17 November2026 at02:25 UTC; first deadline03:10 UTC. I088 is closed at
its fixed CPU rejection and remains preserved. This iteration addresses the
separate constraint/class output boundaries identified by that verification.
Retain I088 only as an isolated prerequisite; sharing the combined change still
requires the complete original performance/correctness gates.

Goal: an interface-stable body edit in the frozen real application reuses entry
inference instead of moving that work into speculative inference. Persist and
restore ready number-rule results through the same constraint-value product as
other constraint targets. Join compatible class-instance prefixes through the
existing transactional class-restoration code, preserving indices, requests,
cycles and rollback. Do not relax the entry's constraint-result requirement.

Before implementation, add no-import regressions for a number rule, edits to its
provider, repeated body edits and a class with a later second instantiation.
Require clean output/diagnostic parity and entry-reuse counters. Benchmark the
combined frozen candidate once:31 body pairs need>=5% median paired CPU gain
with95% interval below parity; retain I088's original controls and full IR checks.
Required remains uncached<=120s, one heavy job,45 minutes plus at most one
progress-based extension. The full cross-stage goal remains incomplete.

### I089 fixed outcome

Source1d495e30 / image9e1dbba3 passes24 focused and166 broader builds at each of
O0/O2, plus all333 required checks uncached in114.192s. Two real-program body
edits match independently empty-cache builds in exact executable bytes, complete
normalized LLVM and diagnostics. An earlier proof incorrectly compared cached
native output with cache-disabled native output, which use different code-emission
paths; retain that observation separately. The corrected proof uses the same
native output mode and verifies fresh-stage counters.

The fixed31-pair CPU comparison measures medians1.463844 ->1.456312s, paired
ratio0.992343,95% interval[0.982974,0.997339]. This establishes a small gain but
misses the predeclared5% requirement. Reject the combined candidate without
retrying it. Remaining controls, full integration and80-build IR matrix are not
run after that gate failure. Preserve both I088 and I089 source/image evidence;
no compiler code from either candidate is shared yet.

Entry inference is reused in30/31 measured edits. Its median stage falls
180.820 ->0.001ms, but preinference rises69.646 ->204.106ms and exposed-function
and managed-lifecycle validation rises5.843 ->40.375ms. Total inference changes318.519 ->313.950ms.
One changed body is restored in both compilers, but restoration rises
3.688 ->33.898ms. Scoped stage medians overlap and must not be added to other
reported parent timings. Attribute the actual consumers before selecting another
optimization. First-edit cold-cache entry reuse also remains an explicit gap;
later matching source proofs alone do not explain all of its invalidation.

The implementation stores every successful constraint result at its producer,
including number rules. Rule publication consumes the already validated result;
fresh inference validates the intrinsic and records warnings, while replay emits
the retained diagnostic product once. Compatible class prefixes use the existing
transactional restoration path, including cross-class reservation, request keys,
overlapping layout checks and rollback. The12-line two-instantiation reproduction
now reuses entry inference. These are verified functional products, but their
combined performance qualification failed. The full incremental goal is incomplete.

## Rejected iteration: I088 global type-producing queries

Started17 November2026 at00:28:36 UTC; first deadline01:13:36 UTC.
I087 completed its original fixed comparison and missed its1% threshold; retain
its source/image separately. The previous question-only turn explained test-suite
selection; this continuation revalidated the terminal benchmark and published its
evidence. Shared compiler d95dda4b remains unchanged; the full goal is incomplete.

Capture each global type output at a completed root statement/control-flow join,
with the cumulative entry-prefix source, lookup, concrete-call, value and global
input reads that actually produced it. Persist those as independently checkable
inference units. During an entry-stable edit, validate producer units before
temporarily installing their type outputs, then preinfer affected global readers.
Commit only after normal whole-entry dependency/interface checks pass. Otherwise
restore the original global types before ordinary entry inference. Never seed a
global merely because its old type is known. Newly observed speculative global
inputs must not justify skipping earlier ordinary demand.

First add minimal no-import tests for a stable global reader, edited initializer
provider, changed type/effect, introduced early global read, forward dependency,
class output and a second edit after entry replay. Require clean runtime/error
parity and counters proving producer checks and root work skipped. The initial
source proof may conservatively include the whole unchanged entry source; finer
statement source keys remain part of the full goal, not claimed implemented here.

Fixed acceptance:31 paired real-program body edits must improve median CPU by at
least5% with95% interval below parity. Controls:15 unused/unused-flex/lives,
31 unchanged/comment/wording,11 cache-disabled cold and7 debug/LLVM body pairs.
Require exact executable/diagnostic parity, full normalized clean LLVM parity,
required uncached<=120s and affected integration before sharing. One heavy job;
one45-minute extension only for concrete progress. If this boundary cannot be
completed in that window, preserve evidence and move to another planned boundary.

### I088 concrete progress and sole extension

The minimal scalar reader passes two successive edits with one preinferred body,
one independently validated global output and entry reuse. The two-global case
also restores both outputs and reuses entry inference with clean runtime parity.
The first cross-process test identified an omitted graph-codec field; the encoder,
decoder and test reader now carry the output index explicitly. A project-wrapper
debugger trace confirms entry reuse reached replacement codegen with empty source
storage maps. Cached global declarations are now bound before replacement emission;
managed initialization flags retain their actual backend identities separately.

The previous shared compiler also crashes on a read before initialization. A
second minimized debugger trace confirms the same missing diagnostic for a local
variable, whose global flag is false and whose expression/variable types remain
unresolved. Apply the existing configured error to unresolved value reads,
preserving lvalue inference. Tests retain both cases. Correct two fixture mistakes:
block-only declarations need indented bodies, and the first chained value was
local until the fixture actually declared/read both globals. Those fixture failures
did not reach timing; no completed performance comparison has been rerun.

Use the sole progress-based extension, ending01:58:36 UTC17 November. Current
source adds prefix-time class facts, managed storage state and a producer/reader
worklist for interleaved dependencies; those additions still require the frozen
correctness gates and original performance matrix. Do not claim full qualification
or compiler-wide completion from the passing scalar and chained cases.

### I088 frozen verification and correctness repairs

The implementation window closed at01:56:20 UTC. Subsequent work is the frozen
candidate's correctness qualification, not another optimization iteration. Preserve
each failing source/image and its evidence; no completed performance comparison
has been retried.

Image5af98bd5 passed34 focused and138 broader builds at each of O0/O2, plus all333
required checks in112.220s. Real-application cache population then failed before
any measured pair: a captured expression had become an integer, but its retained
argument-inference observation still supplied an unresolved-read diagnostic.
Argument reuse now includes the expression's type as an input alongside execution
state. A22-line no-import conditional/loop capture regression covers this case.

Imagef6a8a16c passed38 focused and142 broader builds at each of O0/O2. Required
finished in112.706s with332 passes and one failure: replacement recursion exceeded
the compatibility stack budget. The direct existing regression reproduces the
crash; a project-wrapper debugger run with a reduced stack stops in recursive
argument inference. The new retained input occupies704 bytes, and constructing it
as a temporary adds stack pressure on that recursive path. Construct its map entry
in place and rerun the existing regression, complete correctness gates and original
fixed performance matrix on a new frozen image. Neither this repair nor the global
producer candidate is qualified for sharing yet.

### I088 fixed outcome and verified next boundary

Final sourcebc312215 / image8ba81fdf passes38 focused and142 broader builds at each
of O0/O2, the existing compatibility stack regression and all333 required checks
uncached in113.417s. Its fixed31-pair native body comparison fails: CPU medians
1.470378 ->1.482319s, paired ratio1.010800,95% interval[1.002125,1.017050]. Exact
executables and diagnostics match. Do not retry that source/image comparison or
run the remaining controls/integration after the primary rejection. Preserve the
candidate source/image; shared compiler d95dda4b remains qualified and unchanged.

The candidate restores four global type outputs and preinfers one changed reader,
but entry reuse remains false in every sample. Preinference medians rise
67.917 ->162.835ms while ordinary entry inference falls180.411 ->91.988ms.
Total inference rises315.328 ->323.108ms. The work has moved between phases.

An observed root dependency query succeeds. A later debugger stop records740
reused constraint values against743 total queries. All three missing values are
ready `numbers are 32 bit` queries. Their probes are retained, but the commit path
always reinfers them and stores no value. Entry eligibility compares reused values
with all queries, so it rejects this real application before class preparation.
The earlier cold/warm class-source fingerprint differences do not establish the
immediate rejection cause; current root source proofs all match on a later edit.

A separate12-line no-import class case demonstrates a second boundary: one class
instantiation permits entry reuse; adding a later floating-point instantiation
after the global producer prevents it. Both edited and clean outputs match.
Existing class preparation accepts only equal instance tables or completely empty
ones. Compatible partial prefix facts need an explicit validated join, preserving
their instance identities and requests. These are constraint/class output-boundary
requirements, rather than another global-producer performance variant. Evidence:
`i088-stack-performance-body.json`, `i088-fixed-stage-analysis.json`,
`i088-class-preparation-debug.log`, `i088-minimal-class-join-report.json`.

## Latest rejected iteration: I087 declared-constraint input work

Started16 November2026 at23:55:59 UTC; first deadline00:40:59 UTC17 November.
Previous goal turn identified and minimized the global-input/root-replay boundary,
verified two real body edits and six minimal builds, and published documentation
only. Qualified compiler d95dda4b remains unchanged; the full goal is incomplete.

Profile the existing51ms declared-constraint stage on two current real-program
body edits. Separate parameter materialization, query identity/collection/alignment,
syntax retention, signature initialization and result publication. The existing
740 expression/value hits and zero fresh probes do not imply those producers are
skipped. Use current counters and the project GDB wrapper, including scoped
observation overhead. Do not guess a representation or duplicate a global index.

Choose one actual repeated producer, add a minimal no-import counter/parity test,
and record its fixed real-program CPU gate before implementing. Preserve constraint
changes, late source demands, class layouts, diagnostics and error repair.
Required remains uncached<=120s, one heavy job and at most one progress-based
45-minute extension. No prior failed source/image comparison may be retried.
Global type producers and root statement/block products remain explicit unfinished
requirements, not replaced by this independent bounded constraint investigation.

### I087 selected implementation and fixed acceptance

The initial fine-grained debugger probe exceeds its120s observation limit; its
nested breakpoint setup perturbs execution and yields no usable timing. Preserve
that failure. The replacement uses only parent-scope boundaries. Move its entry
timestamp after debugger FinishBreakpoint setup; otherwise that observer setup
pollutes the first interval. Two corrected traces put collection15.347/18.794ms,
query construction14.039/10.582ms, worklist6.886/9.011ms and final publication
9.150/13.943ms. These include debugger overhead and are not acceptance timings.

Each constraint identity currently serializes twice; a variable/rule query also
indexes every program line, and paths are eagerly built for every source scope.
Memoize immutable identities by their actual query target, build sibling identity
components only when a requested ancestor needs them, and index line occurrences
only in the queried line's owning section. Preserve exact existing IDs, duplicate
occurrence rules, source URI/columns and generated identities. Expose computation,
hit, scope and line counts through the existing inference product statistics hook.
No cache schema or language behavior changes.

Before code, add a minimal three-query fixture with repeated declaration text,
unused code and two unrelated edits. It requires exactly three identity builds,
at least three memo hits, only four owner lines indexed and stable unique query
IDs. Keep only if31 fixed ordinary body-edit pairs establish at least1% paired
median CPU gain with95% interval excluding parity. Controls:15 unused/unused-flex/
lives,31 unchanged/comment/wording,11 cache-disabled cold and7 debug/LLVM body pairs.
Require exact executable/diagnostic parity, original-vs-current source identity
parity on the fixture, complete clean IR parity, required uncached<=120s and
affected integration before sharing. No retry replaces a failed fixed variant.

### I087 closed: less constraint work, below the fixed gain threshold

Source42c0567f / image3b9621ca passes87 focused builds at O0 and87 at O2,
including the new three-query identity regression. The five-build identity fixture
also preserves the exact old query IDs and declaration paths. Required333/0/0
passes uncached in111.113s (111.966s observer). Compiler and494 source inputs stay
unchanged throughout qualification.

The original31-pair ordinary body comparison completes with exact executable and
diagnostic parity. CPU medians1.466325 ->1.447942s; paired ratio .991030290,
95% interval [.986114834,.994805902]. That is an established .897% paired gain,
below the predeclared1% threshold. Preserve this rejected image and branch without
rerunning the comparison. Remaining controls, full IR qualification and integration
were not run after the primary failure; no full qualification is claimed.

Declared-constraint stage medians fall47.802 ->30.088ms. The candidate computes745
identities, reuses740 and indexes only3 source lines, while1758 section identities
are still needed. Preinference69.570 ->72.344ms and root inference181.104 ->183.017ms
remain substantial. These are separate scoped medians, not additive CPU savings.
Shared compiler d95dda4b and the full objective remain unchanged. Next: implement
the missing validated global type-producing query boundary identified by I086,
so a changed global reader can be inferred before deciding entry replay.

## Latest attribution: I086 root-execution reuse boundary

Started16 November2026 at23:43:44 UTC; first deadline00:28:44 UTC17 November.
Previous goal turn made progress: coupled flex source/binding products were tested
and rejected by their original CPU gate, with complete work/cost evidence and
qualified shared I082 unchanged. The full compiler-wide goal remains active.

The qualified real-program body comparison already exposes six inference stages.
Their separate medians are constraints51.139ms, conflicts9.352, preinference69.757,
root execution179.545, exposed/lifecycle5.728 and final validation .238ms. They
are per-scope medians, not the sum of one sample. Only one function body normally
infers,367 IR bodies reuse, but root execution is not reused and no changed
instantiation is speculatively inferred. Attribute the exact root-input/query/
constraint/global-layout blocker before changing eligibility. Use the project
GDB wrapper on two distinct current body edits and preserve source/image evidence.

If a missing per-unit proof is identified, write a minimal no-import clean-parity
and skipped-work regression before implementation. Retain changed signatures,
constraints, globals, class layouts, overloads, failed lookups and diagnostic
ordering; no blanket relaxation or native-IR-only shortcut. State a fixed gain
and unrelated-control gate before implementing. Required uncached remains<=120s,
affected integration precedes sharing, one heavy job and at most one concrete-
progress extension. Do not retry failed I084/I085 candidates.

### I086 attribution complete: global readers block entry replay

Qualified image d95dda4b is unchanged across494 C/C++ inputs. Two distinct real
body edits have equal declaration, initial/final matcher, root-binding, scope-rule
and semantic source-view inputs. The second debugger trace confirms the root-input
branch is entered, but root eligibility is false before reusableRootExecution;
its constraint/class-layout safety check is not reached. The optimized local
rootInputsUnchanged is unavailable in DWARF, so no boolean observation is claimed
from that missing local. All debugger inferiors exit normally.

A seven-line no-import global-reader program prints1 before a literal body edit
and2 after, matching clean output, but does not preinfer the changed function or
reuse entry execution. The equivalent four-line parameter-reader program prints
the same values, preinfers one changed function and reuses entry execution.
Stored concrete products record one global type input versus zero. The selector
explicitly excludes both declared-global readers and nonempty global-input units.
This is a verified missing dependency boundary, not an error in existing output.

Correct support needs type-producing global source/query products independent of
the whole root execution unit. Saved global types or class layouts cannot be made
valid merely by removing those guards: current producer and call/value inputs
must first be checked, including changed return types and class construction.
Root statement/block products are a related larger boundary. Keep both incomplete;
do not claim this read-only attribution implements root reuse or a speedup.
No compiler behavior or tests changed, so no new full-suite pass is claimed.
Evidence: i086-attribution-audit.json, two full projection/debugger captures and
i086-minimal-root-globals/report.json. The next independent measured scope is the
existing declared-constraint stage (~51ms despite740 reused expression/value
products), to identify which unchanged producers still run before changing code.

## Latest rejected iteration: I085 classification and flex demand

Started16 November2026 at23:19:48 UTC; first deadline00:04:48 UTC17 November.
Previous goal turn made progress: measured section production, verified an eager
declaration consumer with a minimal test, then rejected I084's fixed variant for
missing its1% gain gate. Qualified I082 remains the compiler baseline.

Attribute current classification dependency preparation on two consecutive real
Breakout body edits, distinguishing edge selection, reverse/frontier construction,
SCC traversal and eligibility checks. Use the project debugger wrapper for coarse
work boundaries and ordinary compiler counters for timing evidence. Read prior
I031/I081 results; do not implement topology retention if eligibility is still
the actual cost. Do not retry I084 or change the original all-stage objective.

The goal is one proved missing incremental boundary and, if its measured cost
justifies implementation, a minimal test and changed-unit producer/consumer path.
Any optimization must retain implicit-parameter cascades, failed and positive
lookups, overload addition/removal and error repair. Require exact clean parity,
actual skipped work, a predeclared fixed CPU gain gate and no unrelated regression,
required uncached<=120s and affected integration before publication. One heavy job;
one45-minute extension only for concrete implementation progress.

### I085 measured consumer and coupled implementation gate

Two current debugger traces attribute edge selection .522/.796ms, reverse/frontier
work2.334/1.706ms, SCC grouping1.649/1.347ms and eligibility12.746/8.987ms.
All256 matcher segments are loaded before this scope. Eligibility decodes323
additional reference tables (343 ->666). Do not cache graph topology on this
result: downstream consumers still need those reference tables.

Complete the missing whole-body consumer boundary for flex functions. Retain
unchanged source/resolution products, partition binding products by independent
body ownership, and demand bodies whose free names connect them to an ancestor.
Preserve lexical lookup semantics and all typed-constraint demands. Use the
published binding-domain reference index for late requests, avoiding I067's
repeated whole-program reference scans. Keep global joins rooted first and
publish late references through the existing index. This differs from rejected
I067: its broad selected-domain scans are already eliminated by qualified I076.

Before implementation, reuse its minimal variable-storage, independent/shared
names, typed parameter/local, activation, overload, demotion and error-repair tests.
The fixed primary is31 paired unused-flex edits, with at least2% paired median CPU
gain and95% interval excluding parity. Controls:15 body/lives/unused,31 unchanged/
comment/wording,11 cache-disabled cold and7 debug-body/LLVM-body pairs. Preserve
exact executable/diagnostic parity and complete current clean IR equality.
Required uncached remains<=120s; all affected integration must pass before sharing.
No completed failed source/image comparison may be rerun or replace its gate.
The existing deadline and at most one concrete-progress extension still apply.

### I085 closed: more deferred bodies, no whole-build gain

Sourcead1878a1 / image5f42235e passes69 new builds at O0 and69 at O2, plus
required333/0/0 uncached in113.950s. The initial regression fails on qualified
I082 because unused flex source/resolution products are materialized. The new
candidate preserves lexical ownership, demand on new ancestor consumers, typed
parameter/local constraints, overload activation, parameter demotion and error
repair. Its format carries binding ownership units and source free-name summaries;
all current producers/consumers use the new shape without a compatibility path.

The original31-pair unused-flex primary completes with exact executable and
diagnostic parity. CPU medians .531337 ->.534113s have paired median ratio
1.001776450,95% interval[.992777137,1.022903082]. This establishes neither gain
nor regression and fails the predeclared2% improvement. Stop the remaining
controls; do not run full integration/IR/shared qualification or retry this image.

Retained owners increase343 ->549, restored outline nodes1877 ->944, materialized
binding groups162 ->78 and binding-reference visits794 ->585. All671 reference
tables still decode. Frontend wall medians298.105 ->293.560ms and resolution
264.937 ->261.630ms improve modestly; matcher projection43.747 ->45.484ms and
cache writing26.637 ->28.042ms increase. Whole internal wall472.312 ->477.989ms
also shows no saving. These scopes overlap; they are not additive CPU attribution.
In particular, binding input22.414 ->6.195ms moves first-use scope-index work to
its new earlier preparation boundary: complete binding24.595 ->23.687ms falls
only .908ms. Do not advertise the nested input reduction as a16ms compiler gain.

A separate read-only debug probe uses the same image and two new body edits. It
makes33/30 indexed domain selections, with767/619 reference visits. That confirms
the index serves actual late demands, but is not a paired acceptance comparison
and does not replace the failed primary. Keep its full report separately.

Reject the variant, preserve source/tests/image/first comparison and retain one
identified build. Dispose only the owned worktree; qualified shared I082 code
stays unchanged. No extension was used. The full incremental goal is active.
Next profile the larger ordinary inference scope (~315ms), including preinference
(~68ms), query validation (~34ms) and dependency publication (~70ms), before
choosing a concrete changed-unit boundary. These existing measurements identify
nested costs, not an additive decomposition or a new optimization claim.
Evidence: i085-stage-analysis.json, i085-performance-progress.json,
i085-observed-required.json/log and i085-debug-binding-profile/report.json.

## Previous rejected iteration: I084 declaration proof at its early consumer

Started16 November2026 at23:07:42 UTC; first deadline23:52:42 UTC.
I083 completes read-only attribution on qualified image d95dda4b. All2360 headers,
4174 body dispatches and2360 finalizations still run; parse8.45–13.00ms is small
relative to resolution270.18–281.74ms. Do not recreate I050's per-line replay layer.
The profiler identifies a different consumer gap: unused edits audit all1750
resolved declarations, taking10.95/12.41ms, despite zero changed fingerprints.
Active edits audit only one. Project-wrapper GDB attributes the eager aggregate
to canReuseInactiveBodies' declaration callback, before captureAllFunctionProjection
can run its source/tree/binding retention proof. A four-line no-import test
fails on three audited declarations where only the changed owner needs auditing.
Evidence: i083-attribution-audit.json, section producer counts, original debugger
logs and the preserved minimal failing run. No compiler behavior changed in I083.

Run the existing declaration-retention proof at that earlier demand boundary,
only after the inactive source projection has matched. Preserve source/config,
initial/final matcher trees, scope rules, root bindings, declaration membership
and changed-owner signature checks. Do not weaken or duplicate the proof.
An intervening failed frontend must not confuse previous executable and source
projection identities. Existing failure, syntax, provider and parameter-demotion
checks remain required. Keep all source/global pattern visibility semantics.

Before implementation the minimal three-function test requires one audit after
each of two inactive body edits and clean runtime/diagnostic parity. Keep only if
31 fixed real-program unused-edit pairs show at least1% paired median CPU gain
with95% interval excluding parity. Controls:15 pairs body/lives/unused-flex,
31 unchanged/comment/wording,11 cache-disabled cold, plus7 LLVM-body and7 debug-body
pairs. Preserve exact native bytes, diagnostics and complete clean LLVM graph
parity. No retry replaces a completed failed variant. Required remains uncached
<=120s; affected integration and shared qualification precede publication.
One heavy job; one45-minute extension only after concrete implementation progress.
The full cross-stage objective, including semantic section fragments, is incomplete.

### I084 closed: changed declarations isolated, fixed gain gate missed

Source8396d506 / image7c6824d8 passes the four-line regression,95 focused builds
at O0 and95 at O2, and required333/0/0 uncached in111.571s. The original31-pair
unused-edit comparison preserves exact executable and diagnostic parity. CPU
medians .536615 ->.527605s have paired median ratio .991744735,95% interval
[.977913028,.999638849]. The paired median gain is0.826%, below the predeclared1%
minimum. Reject this variant; stop the remaining controls and do not run full
integration/IR/shared qualification or retry the same source/image.

Declaration input audits fall1750 ->1 and their nested wall scope12.765091 ->
.011883ms. The complete inactive projection falls17.720176 ->8.840389ms; it also
computes current body digests, scope-rule bodies and root-binding identities for
the existing proof. Whole internal wall scope477.309901 ->469.799815ms is not an
additive CPU explanation. The exact saved audit work therefore exceeds the
end-to-end saving. No cold or unrelated-control qualification is claimed.

Preserve the source/test branch, frozen image, full first comparison and required
results. Keep qualified shared I082 compiler code unchanged. Retain one identified
build and dispose only the owned worktree. The full incremental goal is active;
semantic section fragments still do not skip their program-wide producer.
The next boundary must couple section/source products to a consumer that avoids
substantial downstream work. Profile current classification dependency preparation
(about16–23ms here) separately from source-outline selection before choosing that
skip proof; do not revive I031 topology retention without identifying a newly
material consumer. Evidence: i083-attribution-audit.json, i084-stage-analysis.json,
i084-performance-progress.json and i084-observed-required.json/log.

## Completed attribution: I083 section-production attribution

Started16 November2026 at23:00:04 UTC; first deadline23:45:04 UTC.
Previous goal turn made progress: qualified/published I082 as6b01f00d, required
333/0/0 uncached109.295s, and verified the shared build after worktree disposal.
The full incremental goal remains active and incomplete.

Use the qualified d95dda4b image without compiler behavior changes to measure
section-production work on two consecutive real-program body edits and an
unused-definition edit. Attribute headers, body dispatch, finalization, locality
and deferred syntax separately; count the actual section producers through the
project debugger wrapper. Read current producer/consumer boundaries and prior
I050 failure before choosing a persisted fragment representation.

This bounded attribution goal completes with a current identified cost, retained
source/image/input evidence, and one concrete missing skip proof suitable for a
minimal regression test. Do not implement another per-line replay layer or retry
I050's failed execution-region codec. If the measured boundary cannot justify its
restore/persistence cost, record that result and select the next relevant consumer.
Any subsequent implementation requires its own fixed real-program gain gate and
clean-build parity; a work-counter decrease alone does not prove a speedup.
Required remains uncached<=120s; no new full-suite claim is made by read-only probes.

## Previous qualified iteration: I082 LLVM output; full goal incomplete

Started16 November2026 at21:36:17 UTC, first deadline22:21:17 UTC. The preceding
turn made progress: I081 was implemented/tested, rejected by its fixed CPU gate,
and its persistence regression explained; qualified I080 remained unchanged.

Share persistent semantic and per-function IR products with LLVM text output.
Use existing output producer/restore boundaries directly, rather than adding the
I073 native-output wrapper layers. Keep mode-specific cache identity and LLVM
artifacts, current output failure reporting, stdout/no-main behavior and automatic
cache use. Remove compiler-private debug products before either backend consumes
IR. Imported debug locations and global/class metadata must match clean output.

The pre-existing LLVM artifact tests fail on the qualified compiler's cache-option
rejection. Add debug metadata parity/capture exclusion tests and migrate default
cache checks before testing. Require source/config changes to retain unaffected
units, output repair without frontend work, full LLVM/diagnostic parity, a fixed
real-program LLVM speedup and no established native-control regression. Required
uncached remains<=120s. One heavy job;45 minutes plus at most one extension for
concrete progress. I073 and I081 stay isolated; do not retry identical failures.

### I082 candidate and fixed comparison protocol

Compiler3918fcd7 passes LLVM artifact tests at O0/O2, complete minimal debug LLVM
parity with moved class/global metadata, and required333/0/0 uncached in110.237s.
The new output producer consumes private debug products before output, and a
cached-text restore avoids initializing LLVM. Mode identity keeps native and LLVM
results separate; all output-identity producers/consumers use compiler_output.

Freeze this exact source/image before timing. The LLVM body primary uses7 pairs:
qualified I080 has no incremental LLVM path, so its LLVM calls are cache-disabled;
the candidate uses its incremental path. Require a CPU gain and complete LLVM
parser/graph equality after symbol ordering and local-name alpha normalization.
Types, globals, function names, attributes and metadata remain checked. Inspectors
run outside the timed child-CPU interval. A7-pair cache-disabled LLVM control and
the original eight native controls must establish no regression; native comparisons
retain exact executable bytes. Do not retry a completed failed variant. Affected
integration and full native/debug/LLVM parity follow only if this gate passes.

### I082 verified ordering repair; one qualification extension

The first real-program graph comparison stops before completing any CPU case:
load/store alignment on one global is8 in fresh output and4 in incremental
output. Saved pre-optimization graphs are identical after symbol ordering, but
raw declaration order differs. The incremental snapshot canonicalizes symbols
before optimization; ordinary LLVM output did so only afterward. A debugger
probe canonicalizing the fresh input produces exactly the cached graph.

A22-line no-import DynLex regression reproduces this with a global record,
an exposed mutator and an exposed aggregate reader. It fails before the repair.
Make ordinary LLVM output use the same pre-optimization order. Corrected-image
checks pass the minimized regression and O0/O2 artifact/debug parity. Recheck
required and full real-program parity before timing. This is a source correction,
not a retry of a completed failed CPU comparison.

LLVM speedup/correctness now compares the corrected compiler's incremental and
clean paths, using separate executable copies with identical hashes. The ordering
repair intentionally changes optimization annotations relative to old I080, so
historical LLVM cold timings retain separate per-version graph identities;
current incremental output must match its own complete clean graph. Native
controls still compare qualified I080 against the candidate with exact executable
bytes. No alignment normalization or comparison waiver is used for incremental
correctness. Use the sole extension, ending23:06:17 UTC, for fixed comparison,
integration/full graph checks and shared publication if qualified.

The corrected7ecd998d image passes required333/0/0 uncached in109.523s, the
22-line ordering regression, O0/O2 artifact tests and complete minimal debug LLVM
parity. A fresh full Breakout compile now matches its same-image cached compile
with zero complete normalized LLVM graph differences. Freeze this corrected source
before its distinct fixed matrix. The earlier3918fcd7 comparison remains invalid
for timing; its saved mismatch and debugger proofs are preserved separately.

### I082 qualified candidate; shared publication in progress

Corrected source9334759f / image7ecd998d passes the fixed11-case performance
matrix. On the real Breakout program, LLVM body-edit CPU medians fall
4.885286 ->2.248144s; paired ratio .466704,95% interval[.458220,.478002].
One body is inferred,367 functions reuse IR and705 classification/resolution
units are retained. Whole-module LLVM optimization still costs976.651ms;
frontend306.853ms, inference312.038ms and snapshot153.450ms also remain.
This does not complete the every-stage incremental objective.

Native controls and the historical cache-disabled LLVM cold control establish
no regression. Native cold medians5.190817 ->5.287781s have paired interval
[.992173,1.037022], so no cold gain is claimed. First empty-cache population
includes persistence overhead; warm-edit gains do not describe that first build.

Qualification passes required333/0/0 uncached in109.523s and integration462/0/0
in793.204s, with unchanged source/image identities. Eighty real-program native,
debug-native, LLVM and debug-LLVM compilations pass complete same-version clean
IR/diagnostic parity. Complete native DWARF checks pass. Shared rebuild, required,
focused, DWARF and post-disposal replay remain before publication.
Evidence: i082-fixed-performance-analysis.json, i082-fixed-llvm-stage-analysis.json,
i082-fixed-qualification-audit.json and the four i082-real-*-ir/report.json files.

### I082 shared qualification completed

Shared merge fe1416d8 / image d95dda4b passes all333 required
checks uncached in109.295s, focused LLVM ordering/artifact/debug graph checks,
complete DWARF verification, and15 real-program native/debug/LLVM builds after
worktree disposal. Source identity checks cover494 C/C++ inputs; external checks
cover105 library/application data files and the installed runtime. The shared
compiler archive verifies both compressed and executable hashes. The merged
worktree and branch are removed; rejected I073/I081 branches remain isolated.

The LLVM primary's empty-cache population costs7.717735 CPU seconds versus
4.807605 for its cache-disabled population. The4.885286 ->2.248144s warm-edit
comparison therefore does not claim a first-build improvement. Shared native
behavior retains its existing reuse and all fixed controls establish no regression.
Evidence: i082-root-qualification-audit.json and i082-root-disposed-replay-final/report.json.

Close I082 as a qualified LLVM-output path, not completion of the whole goal.
Next selected gap is P03: semantic section production remains program-wide after
an edit. Attribute actual remaining section/declaration work before implementing
fragment retention, preserving current source, indentation, scope and configuration
dependencies. Do not repeat I081's coarse product repacking or complete-index
reconstruction. The next iteration gets its own start time and fixed acceptance
case; unchanged setup work must actually disappear, not move into cache decoding.

## Prior rejected iteration: I081 matcher layout

The bounded declaration-preparation investigation found mixed matcher-product
loading, not declaration decoding, dominated early setup. Its resulting isolated
layout passed required333/0/0 in107.861s and cache eviction/corruption checks, but
failed the first fixed CPU comparison. Production compiler code stays I080.

### I081 closed: early read savings do not repay persistence overhead

The fixed31-pair active-body primary rejects source68c316f9 / compiler491670dd:
CPU medians1.469013 ->1.492362s, paired ratio1.016424, interval[1.008217,1.021076].
Exact executable/diagnostic parity passes. Stop the remaining eight cases; do not
retry this variant, run integration/LLVM qualification, or merge its compiler code.
Required333/0/0 in107.861s and the focused eviction/corruption checks remain valid
correctness evidence, not performance qualification.

Source-owner selection improves44.727 ->25.955ms, but full-build matcher reads fall
only9.124 ->8.828MB. Changed segments load retained entries again during capture.
The layout changed256 digest buckets to16 buckets per kind, making update units
coarser:28 newly encoded segments total6.341MB, versus39 segments /2.000MB before.
The new complete-key inventory also parses/allocates all keys at restoration:
that scope rises2.752 ->15.496ms; snapshot156.941 ->169.942ms. These timing scopes
overlap their parents and are not an additive explanation of total CPU.

Preserve the branch and archived image, retain one identified build, dispose the
worktree, and leave qualified shared I080 unchanged. No deadline extension was
used. The next storage attempt must keep producer update granularity and avoid a
whole-index reconstruction; metadata isolation alone does not satisfy that rule.
Continue with an independent missing compiler path after closing this experiment.
Evidence: i081-body-regression-analysis.json and i081-final-state.json.

## Last qualified snapshot: I080 debug IR reuse

I080 implements native debug IR reuse with per-function source-location inputs.
Unchanged semantic inputs alone do not admit old debug locations: declarations,
statements, variables, classes, members and referenced globals must still match.
Imported metadata belongs to the current compilation unit. Compiler-private type
products are removed before backend partitioning and optimization. Native builds
hydrate no debug payload; the existing six-payload regression remains unchanged.

The fixed 31-pair debug edit comparison improves 3.764891 -> 1.748279 CPU seconds,
a 53.6% reduction (paired ratio interval [.462290, .467659]). It restores one typed
body instead of 387 and reuses 367 functions' IR after 9,045 location checks.
Inference wall medians fall 2146.816 -> 379.739ms; body loading 1477.159 -> 4.647ms.
All eight native controls establish no regression. Cache-disabled native cold
medians are 5.224184 -> 5.261480 CPU seconds, interval [.992555, 1.017783]; this
iteration establishes no cold-build gain. The 186 measured unchanged/comment/
wording builds perform zero object builds and zero linking.

Source 2a8f1dfc / compiler cba27e0a passes required 333/0/0 uncached in 108.576s,
596 focused O0/O2 builds, complete DWARF, integration 462/0/0 in 761.727s and
40 complete real-program native/debug LLVM comparisons. Candidate-vs-clean
comparisons retain all debug metadata. The verified source-origin bug fixes
change old I077 debug records; old-vs-new debug timing therefore uses semantic
LLVM equality, while all native controls retain exact executable equality.

Shared source 44f9b51c / compiler b29d658c matches all 469 C++ and 22 C inputs.
It passes required 333/0/0 uncached in 108.925s, another 596 focused builds,
complete DWARF and nine native/debug real-program builds after worktree disposal.
Both merged branches and the worktree are removed. One exact build is retained,
and both compiler images are archived with verified hashes. Work began at
20:08:43 UTC on 16 November; the sole extension ends at 21:38:43 UTC.

The full goal remains active. Semantic section construction, broader persistence,
tracing, tooling and non-native output paths still require work. The next screen
records current debug-edit medians: parse 10.364ms, resolution setup 117.353ms,
classification 54.003ms, snapshot 250.524ms. File preparation already reuses 44
files / 6,870 lines; section dispatch still visits 2,360 headers / 4,174 body lines.
Select a reusable declaration/section producer and its downstream consumer before
adding storage; merely replaying construction has already failed prior gates.
Keep the same benchmark-driven acceptance and one-goal iteration boundaries.

Evidence under structured-20261113: i080-performance-analysis.json,
i080-debug-stage-analysis.json, i080-qualification-audit.json,
i080-root-qualification-audit.json and i080-next-frontend-screen.json.

### Preceding I079 and I078 experiments

I079 passes its fixed debug primary (53.2%CPU gain;3.769174->1.761183s,
ratio interval[.464736,.470245]) and all eight native controls without established
regression. Its required suite then finds one efficiency failure: a native edit
loads7 payloads instead of6 because debug_ir_inputs is hydrated unconditionally.
Keep I079 f6b3d760/c77a0a1f unqualified and preserve its measurements separately.

Repair the cache consumer: hydrate debug inputs only in debug mode, and express
that product's absence in the non-debug IR importer. Keep the six-payload test;
no schema fallback, dummy product or test-expectation increase. The targeted
three-line fixture now passes; required passes333/0/0 uncached in108.576s,
plus596 focused O0/O2 builds and complete DWARF. Freeze the revised executable,
benchmark it once against qualified I077, then run affected integration and full
native/debug LLVM comparisons. Reuse already completed checks only for the exact
same executable/source image. One heavy job,45 minutes plus at most one extension
after concrete progress; do not retry a failed completed fixed variant.

I079 starts 16 November 2026 at 18:50:58 UTC, first deadline 19:35:58 UTC.
Persist per-function dependencies on authored source locations separately from
runtime IR inputs. Share the existing logical line identity producer with typed
bodies; metadata queries cover declarations, statements, variables, classes,
members and debug records, including referenced globals. Validate those queries
before admitting an IR closure. Reattach imported subprograms/globals to the
current compilation unit. Minimal unrelated-edit/moved-location tests precede
production changes, then require full LLVM/DWARF parity and the fixed CPU gate.
Native controls must establish no regression; required uncached must stay <=120s.
One heavy job, 45 minutes plus at most one extension after concrete progress.
No completed failed matrix is retried. I078 remains isolated and unchanged.

The first prototype passes 582 focused builds and complete DWARF, then exposes
an existing metadata-origin bug in the real program: a local declaration's line
584 is paired with the current function's 62-line file. LLVM inspection and a
nine-line, two-file optional-binding repro prove the wrong file scope. The new
metadata assertion fails on shared I077. Emit a lexical file scope for local
locations, shared with ordinary statement-location emission; keep the source
anchor invariant. The minimal regression passes after the fix. Also correct the
existing global-debug call passing an expression pointer in the isDefined slot.
Re-run affected correctness on the corrected image before its fixed CPU matrix.

Resolve debug type graphs at the metadata producer boundary, map concrete class
products during import, and attach globals/subprograms to the finalized current
compilation unit. A named cache-product table initially reached optimizer units:
cold optimization rose3.6->23s. Remove those records after the unoptimized cache
snapshot, before backend partitioning/fingerprinting; ordinary noncached emission
does not produce them. Optimization returns to3.54s and cold builds to13.17s.
The final image passes596 focused compilations, full DWARF including imported
optional declarations, class rename/recursion, and20 real-program complete LLVM
comparisons. Freeze it before timing. The verified source-origin repair changes
debug sections relative to I077; debug comparisons therefore require LLVM-stripped
semantic IR parity and diagnostics, while all native controls retain executable
byte equality. Candidate-vs-clean LLVM/DWARF checks retain all metadata.

I078 ran 16 November 2026 from 18:18:42 UTC with a 19:03:42 UTC first deadline.
Scoped probes attribute the debug edit's 1,476ms body loading to payload loading
and decoding (676ms), typed-tree restoration (588ms), classes (113ms), source
restoration (73ms) and commit (26ms). Those timings do not separate payload I/O,
hashing and parsing. There are 57,418 restored expressions. Test-only inspection
and production storage were migrated together to the existing CBOR codec.

The fixed debug primary establishes a 6.3% gain across 31 pairs: CPU medians
3.749052 -> 3.508071s, paired ratio interval [.933189, .939867]. Body-load wall
medians fall 1465.174 -> 1241.049ms with identical work counts. The 392 body
payloads shrink 29,766,040 -> 14,648,175 bytes. Focused O0/O2 parity passes 552
compilations plus complete DWARF checks. Native body and unused-flex controls
establish no regression, but the lives control fails: paired ratio 1.009578,
interval [1.002455, 1.014302], medians 1.842822 -> 1.846313 CPU seconds.
Paired ratios differ from ratios of medians. Work counters are identical, and
wall differences spread across optimization, codegen/link and snapshots; no
source-level cause for this CPU regression is established.

Reject the fixed candidate without retrying it. The remaining five controls,
full suites and complete LLVM parity are not run after gate failure. Source
45388172 / compiler 374d0a7e and attribution probes remain isolated on
codex/incremental-body-restoration-i078 (final documentation 3c2a6fc1). One exact
build is retained, the worktree disposed, and compiler/evidence images preserved.
Master's qualified I077 C++ and cf8248da compiler remain unchanged. Evidence under
structured-20261113: i078-body-phase-analysis.json, i078-debug-stage-analysis.json,
i078-performance-progress.json and i078-lives-regression-analysis.json.

I078 identified the next boundary: debug IR still regenerated all functions. Its metadata
reads function declarations, statements, variables, classes and member source
locations. Runtime semantic IR dependencies alone do not prove those locations or
compilation-unit ownership reusable. Audit and persist those producer products
before enabling native debug IR reuse. Semantic section construction, persistence,
tracing, LSP and other output modes also leave the full objective incomplete.

I077 starts 16 November 2026 at 17:22:04 UTC, first deadline 18:07:04 UTC.
Separate debug semantic replay from native IR import: unchanged inference facts
and typed bodies may be restored with current source anchors, while debug IR
regenerates. Preserve trace exclusions and all existing query/dependency proofs.
Test independent body reuse, changed bodies, moved lines and failures/repair before
the implementation. Require a real debug-edit CPU benefit, no established
regression in the original native controls, required uncached <=120s, affected
integration and full native/debug LLVM/DWARF parity. One heavy job, 45 minutes
plus at most one extension after concrete progress; no failed matrix retries.

The fixed CPU matrix passes: debug body edits improve 3.804354 -> 3.745819
seconds across 31 pairs, paired ratio interval [.978535, .987254], about 1.5%.
None of the eight original native controls establishes a regression. The real
edit infers one function body instead of 552, reusing 399 inference facts and
loading 387 typed bodies. Native debug IR still regenerates from current source
anchors. This is a semantic reuse improvement, not incremental debug IR.

Typed-body restoration costs 1459.974ms. Consequently the inference phase itself
increases 1755.009 -> 2129.145ms, while snapshot work falls 735.156 -> 256.372ms.
Do not describe the inference-pass reduction as an inference-time reduction.
An initial debug-cache population pair costs 12.195745 -> 13.166386 CPU seconds,
with snapshot work 824.812 -> 2047.778ms. That single observation exposes the
additional cold-cache cost; it does not establish its general distribution.
The cache-disabled native cold control establishes no regression.

Source d8949810 / compiler 8476ee20 passes 230 focused O0/O2 compilations,
complete DWARF checks, twenty complete real-program debug LLVM comparisons and
required 333/0/0 uncached in 101.340s. Integration passes 462/0/0 in 755.897s; twenty native LLVM comparisons also
pass against the same frozen 485 C/C++ inputs. Shared compiler cf8248da / source
e469b81c verifies those inputs and passes required 333/0/0 in 107.500s, another
230 focused compilations, DWARF and six builds after worktree/branch disposal. The sole
extension ends at 18:52:04 UTC. Next profile actual body-restoration and snapshot
producers before choosing a change; no source-level cause is established yet.

I076 starts 16 November 2026 at 16:10:48 UTC, first deadline 16:55:48 UTC.
Read-only debugger counts on the published I075 compiler confirm native body
edits make 3 binding calls / 216 name visits / 2,444 reference visits, while
debug edits make 187 calls / 29,026 names / 292,946 references. Of the latter,
186 request one domain. Index those requests from reference publication/removal;
build only after deferred-body reparenting has finished. Preserve whole-program
global-domain joins and the existing persisted binding queries. Tests precede
the implementation. Require real debug-edit CPU improvement and no established
regression in the original eight native controls, required uncached <=120s,
affected integration and complete native/debug parity. One heavy job, 45 minutes
plus at most one extension after concrete progress; do not retry failed matrices.

The fixed matrix passes: debug body edits improve 3.892643 -> 3.811400 CPU
seconds over 31 pairs, paired ratio interval [.969064, .983547]. None of the
original eight native controls establishes a regression. Source cbfdda74 /
compiler 0125c5bc matches all 463 C++ and 22 C inputs and passes 194 focused
O0/O2 compilations, required 333/0/0 uncached in 100.409s, integration 462/0/0
in 754.829s, and forty complete real-program native/debug LLVM comparisons.
Shared compiler 10e8ea7c / source 11bae61c matches the same 485 inputs and passes
required 333/0/0 uncached in 105.962s, another 194 focused compilations, complete
DWARF checks and six real-program builds after worktree/branch disposal. Both
compiler images are archived and verified; one build is retained with exact
source hashes/mtimes. The sole extension ends at 17:40:48 UTC; the full objective
remains active and incomplete.

The stage audit proves the only binding-domain consumer runs after
sourceSyntaxResolutionComplete, while the sole source reparenting operation runs
earlier in the first resolution pass. Build the index at that stable boundary;
no structural-invalidation callback or defensive rebuild is necessary. Both
reference registrations and the one removal producer maintain the live index.

The preceding I075 snapshot enabled validated source-owner, parameter-classification and whole-body
matcher reuse in native debug builds. Source 50f532a3 / compiler 804e1c59 matches
all 462 C++ and 22 C inputs. It passes 116 focused O0/O2 compilations, required
333/0/0 uncached in 99.860s, integration 462/0/0 in 767.108s, complete DWARF
comparisons after moved source lines, and forty real-program native/debug LLVM
comparisons. The merged shared compiler cbd31826 / source 1469075e verifies the
same 484 inputs, passes required 333/0/0 uncached in 111.404s, another 116 focused
compilations and the complete DWARF checks. Six real-program builds pass after
the merged worktree/branch are removed. Both compiler images are archived and
verified; one build is retained with exact source hashes/mtimes for the next goal.

The frozen LEXI8 debug body-edit comparison establishes a 1.2% whole-process CPU
gain: 3.939730 -> 3.892195s over 31 pairs, ratio interval [.983047, .991137].
All original eight native controls establish no regression. Debug inference and
IR still regenerate; this closes the source-resolution boundary only.

I074's transitive parameter-classification and concurrent folded-before-runtime
dependency repairs remain in the shared source. Its archived db174898 baseline
and qualification evidence remain available; I075 builds on that qualified source.

I069's qualified graph-column reuse and measured gain remain. I072's mapping
candidate is rejected after neither required gain case establishes a benefit.
I073 implements LLVM text incremental reuse and passes focused/required correctness
checks, but its original native cold control regresses 5.230720 -> 5.325425 CPU
seconds; it remains isolated and unqualified. Its complete graphs match clean
LLVM modulo nonsemantic local names. The source-level cause of its 95ms cold cost
is not established, and no failed matrix is retried. Production still supports
native executable incremental output; the LLVM prototype has not been merged.
I063/I067 timing-rejected and I065 unqualified branches also remain isolated.

I076 avoids repeated global scans for selected binding domains. I077 separates
semantic replay from native debug IR import and restores current source anchors.
Cold resolution is about 2.38s in retained stage samples.
Semantic section construction, some
inference validation, persistence, debug IR, tracing, LSP and other output
modes retain broad or missing incremental paths.
The complete objective covers every .dl/configuration edit and every compiler
stage; it remains active and incomplete.

## I077: separate debug semantic replay from native IR reuse

Started 16 November 2026 at 17:22:04 UTC. After 230 focused compilations,
complete debug LLVM parity and the fixed CPU gate passed, use the sole extension
through 18:52:04 UTC to finish shared qualification and publication.

Permit the existing semantic inference controller in debug builds, preserving its
query/dependency proofs and trace exclusion. Pass no previous native IR to that
controller under debug: generated metadata must use current locations. Successful
debug snapshots retain runtime typed bodies, including side-effecting void bodies,
because those bodies cannot be replaced by imported native IR. The initial
candidate exposed this producer mismatch: void-body summaries contained no body
and repeated inference. Retaining the required body product fixes its cause.
Minimal tests verify independent bodies, edits, moved locations and failed lookup
repair at O0/O2. No language rule or cache compatibility fallback is introduced.

Debug body edits improve 3.804354 -> 3.745819 CPU seconds over 31 fixed pairs,
ratio interval [.978535, .987254]. All eight native controls establish no
regression. Actual work falls from 552 inference passes to one, with 399 facts
and 387 typed bodies reused. Body restoration costs 1459.974ms, so inference
itself increases 1755.009 -> 2129.145ms; snapshot work falls 735.156 -> 256.372ms.
The net measured gain is 1.5%, not the inference-pass reduction percentage.
One initial debug-cache population pair increases 12.195745 -> 13.166386 CPU
seconds, including snapshot 824.812 -> 2047.778ms. This cost needs attribution;
no repeated cold-debug matrix or general cold-debug performance claim is made.

Frozen source d8949810 / compiler 8476ee20 passes 230 focused compilations,
required 333/0/0 uncached in 101.340s, integration 462/0/0 in 755.897s,
complete DWARF verification and forty native/debug real-program LLVM comparisons.
All 463 C++ and 22 C inputs remain unchanged. Evidence under structured-20261113:
i077-performance-analysis.json, i077-debug-stage-analysis.json,
i077-qualification-audit.json and i077-next-body-restoration-boundary.json.
The next bounded optimization profiles actual restoration/snapshot producers;
debug IR, semantic section construction, tracing, tooling and other output modes
still leave the full goal incomplete.

### I077 shared qualification completed

Local-master merge e469b81c / compiler cf8248da verifies identical 463 C++ and
22 C inputs. Required passes 333/0/0 uncached in 107.500s, with another 230
focused O0/O2 compilations and complete DWARF checks. Six real-program builds
pass after worktree/branch disposal: prime, two body edits, comment, diagnostic
wording and unchanged. Fast cases preserve executable bytes/mtime with zero
object builds or linking. The compiler archive, 105 external source files and
shared runtime are verified. One matching build is retained for the next bounded
goal. Evidence: i077-root-qualification-audit.json. The full objective remains
active and incomplete. Next profile the 1459.974ms typed-body restoration and
cold snapshot producer before selecting an optimization; fresh debug IR remains
an independent unresolved stage boundary.

## I076: index binding domains at their reference producers

Started 16 November 2026 at 16:10:48 UTC, first deadline 16:55:48 UTC.
Use the single extension through 17:40:48 UTC after 194 focused compilations
and the complete fixed CPU gate pass. Finish shared qualification/publication;
do not repeat a failed completed matrix or redefine the full all-stage goal.

Read-only debugger attribution on I075 shows native edits make 3 binding calls,
216 name visits and 2,444 reference visits; debug edits make 187 calls, 29,026
name visits and 292,946 reference visits. Of those debug calls, 186 select one
domain. The helper was scanning all unresolved names and references before
filtering that domain. This is the measured producer/consumer boundary.

Build a domain index once on the first selected request, after source resolution
and deferred-section reparenting. Route both reference registrations and the one
removal producer through a shared publication API. Later source demands update
only their own domain. Whole-program binding preserves its original global-name
joins and persisted query products; no new user declaration/configuration or
cache format is introduced. Parameter-only owners can have an empty domain.
Copy the selected view before binding, so publishing another source body cannot
invalidate an active iteration. The scope-stage audit makes a general structural
invalidation/rebuild hook unnecessary.

Statistics expose actual consumer name/reference visits, index builds, initial
index reference visits, selections and publication updates. The real debug edit
now visits 2,613 references plus 794 once for index construction, with 186
selections and 1,819 updates. Both images still materialize 528 binding groups,
rebind 2,613 references, reuse 1,051 binding units and infer 552 bodies. Binding
wall medians fall 111.322 -> 30.447ms and inference 1846.623 -> 1761.584ms.
These wall observations are not independent CPU attribution. Whole-process CPU
improves 3.892643 -> 3.811400s over 31 fixed pairs; paired ratio median .976321,
interval [.969064, .983547]. All eight native controls establish no regression,
including cache-disabled cold 5.289689 -> 5.310939s, interval [.998391, 1.020243].
No general non-debug speedup is claimed. All 186 measured fast builds reuse the
frontend with zero object builds or linking.

The new minimal three-function test verifies bounded binding scans after a code
edit under debug inference. Existing global, nested-capture, failure/repair,
parameter-classification and moved-source checks pass at O0/O2: 194 focused
compilations. Required passes 333/0/0 uncached in 100.409s; integration passes
462/0/0 in 754.829s. Twenty native and twenty debug builds match independent
fresh LLVM/diagnostic outputs across four real-program cases and two edits each.
All 485 C/C++ inputs and the executable remain unchanged. The frozen image is
archived. Evidence: i076-binding-baseline/report.json, i076-binding-stage-analysis.json,
i076-performance-analysis.json, i076-qualification-audit.json and
i076-next-debug-inference-boundary.json under structured-20261113.

### I076 shared qualification completed

Local-master merge 11bae61c / compiler 10e8ea7c verifies identical 463 C++ and
22 C inputs. Required passes 333/0/0 uncached in 105.962s, plus 194 focused
O0/O2 compilations and complete DWARF checks. Six frozen real-program builds
succeed after the merged worktree/branch are removed: prime, two body edits,
comment, wording and unchanged. Fast paths preserve executable bytes and mtime
with zero object builds/linking. Body edits retain the established two/one
inference passes and 366/367 reused IR functions, with one object each. The
executable archive is verified. Evidence: i076-root-qualification-audit.json.
The full objective remains active; the next bounded goal is separating debug
semantic inference replay from native IR import, with current source anchors
and complete fresh debug-output parity. Semantic section construction,
persistence, tracing, tooling and other output modes also remain incomplete.

## I075: reuse source-resolution products in debug native builds

Started 16 November 2026 at 15:13:02 UTC, first deadline 15:58:02 UTC.
Use the single extension through 16:43:02 UTC after the fixed CPU gate and both
suites pass; finish immutable-image parity, shared verification and publication.
No failed performance matrix is repeated. Keep trace eligibility and debug
inference/IR guards until their own producer/consumer work is implemented.

Remove the debug exclusions from source-owner retention, parameter-classification
selection/propagation and whole-body resolution retention. Their existing source,
matcher and dependency proofs still apply. Restored references use current source
ranges; ordinary inference and code generation produce current debug metadata.
Tests cover edits, shifted lines, failed lookups, parameter demotion and transitive
failure/repair at O0/O2, without application-library imports. Complete DWARF agrees
with independently empty-cache native builds after each edit. Those builds use
the same object-partition strategy; cache-disabled builds also supply runtime and
diagnostic parity. The initial comparison against monolithic ordinary output
correctly exposed different physical compilation-unit layouts, not stale locations.

On the frozen real application, classification reuse changes 0 -> 705 units and
early reference visits skipped change 0 -> 7,299. Resolver medians are
404.851 -> 275.475ms. Inference medians rise 1727.569 -> 1849.827ms, including
binding 31.026 -> 110.854ms; 552 body inference passes remain in both images.
These stage wall timings are not independent CPU attribution. The complete
paired CPU result, 3.939730 -> 3.892195s, is the accepted benefit. The binding
source audit supplies a concrete next boundary, not a claim that all its cost is
already attributed. All eight native controls pass, including cache-disabled
cold 5.236415 -> 5.269093s, interval [.986628, 1.024738]. All 186 measured fast
unchanged/comment/wording builds reuse the frontend with zero object builds/linking.

Worktree qualification verifies all 484 C/C++ inputs throughout both suites and
twenty native plus twenty debug complete LLVM/diagnostic builds over four real
edit cases, with two edits per case. Source 50f532a3 / executable 804e1c59 is
archived and its identity is checked. The full compiler-wide objective remains
active and incomplete. Evidence: i075-performance-analysis.json,
i075-debug-stage-work.json, i075-qualification-audit.json and
i075-next-binding-boundary.json under the structured-20261113 evidence directory.

### I075 shared verification completed

The local-master merge is 1469075e and its compiler cbd31826 has identical C/C++
inputs to the qualified worktree. Required passes 333/0/0 uncached in 111.404s;
116 focused compilations and full DWARF checks also pass. Six frozen real-program
builds succeed after worktree disposal: prime, two body edits, comment, wording
and unchanged. Fast builds preserve executable bytes and mtime with no objects
or linking. The body edits match the original fixed benchmark's work counts:
two inference passes/366 reused IR functions on the first edit, then one/367,
with one object built each time. The first smoke assertion incorrectly expected
one pass immediately; both baseline and candidate already prove the two-pass
initial behavior. Its incomplete report is preserved and no performance matrix
is repeated. The shared executable's archive and all 484 input hashes are verified
by i075-root-qualification-audit.json. The full objective remains incomplete.

## I074: qualify the concurrent folded-call repair on shared master

Started 16 November 2026 at 14:37:35 UTC; first deadline
15:22:35 UTC. I073 closes unqualified before its final deadline,
with the source branch preserved and its owned worktree removed. A push of the
report is rejected because another agent published ae127948 / merge 3efb5854.
Inspect and merge those changes; do not overwrite remote work or claim the old
I071 executable represents the new source.

The concurrent repair keeps folded calls out of runtime-body visitation, so a
later real call can collect dependencies from a restored instantiation. It also
explicitly constructs the optional storage definition. Its intrinsic-only test
covers failure/repair and folded-before-runtime uses at O0 and O2. This is the
other agent's repair, not an accepted optimization from I073. Rebuild one job,
verify the complete current C/C++ input identity, run required uncached <=120s
and the affected integration suite, then push the combined shared baseline and
reports. No old failed performance matrix is retried. The full compiler-wide
incremental goal remains active and incomplete; resolver work remains the next
optimization boundary after synchronization.

### I074 qualification completed on the combined baseline

The ROOT build and all source inputs stay immutable through eight focused
compilations, required 333/0/0 in 103.844s, integration 462/0/0 in 763.404s and
four real-program cases with two edits each (twenty complete LLVM/diagnostic
builds). The native comparison removes only the first ModuleID comment; it does
not use the prototype's local-name normalization. The concurrent repair is kept,
the executable is archived, and the combined source/reports are published without
overwriting the other agent's commits. The full goal remains incomplete.

## I073: share incremental products with LLVM text output

Started 16 November 2026 at 13:05:05 UTC; first deadline
13:50:05 UTC. Production is I071 compiler 03e6cf76; I072 is
rejected after both mandatory gain cases fail to establish a CPU benefit, with
its source branch isolated and worktree removed. No remaining interrupted sample
is used as evidence. The full all-stage goal stays active and incomplete.

The entry-point audit verifies that LLVM, object, WASM, SPIR-V and LSP modes bypass
the incremental driver. Its emitter callback currently owns native executable
emission, so removing a CLI guard alone would emit the wrong artifact. Implement
one explicit host LLVM-output strategy sharing existing source, matcher, inference,
diagnostic and function-IR products. Persist the emitted artifact independently of
semantic state; distinguish output modes in keys. Keep native behavior and all
existing dependency proofs. Expose output/reuse work and preserve explicit I/O errors.

First capture the current minimal cache-option rejection and real-program LLVM cold/
body-edit baseline. Add minimal cross-process edit, error/repair, wording-config,
output-deletion and mode-isolation checks before code changes. Require complete
LLVM/diagnostic parity and real-program fast-path/body-edit gains, plus no established
regression in the original eight native controls. Required uncached <=120s, affected
integration and full parity precede publication. One heavy job, 45 minutes plus at
most one extension after concrete progress. Whole-module LLVM optimization remains
a separately measured limitation until its own producer reuse is implemented.

A current source audit also corrects the stale P07 description: frontend diagnostic
barriers already use definition identities, including enclosing owners and notes
(commit 13a3f819), with focused warning/failure/repair tests. Do not add a duplicate
file-level diagnostic mechanism. The historical inventory below describes its start
state; the stage documentation is corrected to describe present behavior.

### I073 closed unqualified: cold native control fails

The isolated LLVM prototype is implemented at ed35da01 / compiler a1479211. It
passes 66 focused compiler invocations and two runtime checks, and all 333 required
checks uncached in 104.474s. This covers imported code edits, comments, error/repair,
artifact deletion/corruption/eviction, stdout, mode separation, definition-only
output and wording-only configuration updates. Old default-cache expectations were
migrated to test LLVM support while preserving object-output rejection.

The real-program preflight first reports only temporary LLVM-name differences.
The complete LLVM graphs are identical after removing argument/instruction/block
names, preserving all instructions, operands, block order, attributes, metadata and
global/function/type names. The original unstripped failure and graph-equivalence
proof remain. No compiler workaround was added for cosmetic names.

The first original eight native controls complete. Seven establish no regression;
cache-disabled cold regresses 5.230720 -> 5.325425 CPU seconds, ratio interval
[1.002775, 1.033884]. The gate fails. Do not merge or repeat that matrix. No full
LLVM acceptance matrix, integration qualification or ROOT qualification is claimed.
The source branch codex/incremental-llvm-output-i073 remains isolated, its clean
worktree is removed, and one verified build is retained. Production stays I071.

Read-only debugger attribution, not acceptance timing, captures three paired full
stage observations. Resolution dominates at about 2.38s; both compilers infer 552
bodies. Inference medians are 1.16 -> 1.19s and total codegen 1.52 -> 1.54s;
optimization is 1.00s in both. These samples do not identify a source-level cause
for the full 95ms increase. Do not claim code layout or additional compiler work
as a verified cause. The broad initial debugger breakpoint also matched a standard
library lambda; its failed probe is preserved and the complete scoped probe replaces
it. One extension through 14:35:05 UTC was used; close before its deadline.

Next select a measured resolver producer/consumer boundary, with actual work counts
and a minimal test before changing it. Do not restart the failed LLVM variant or
add a speculative fix for its unattributed cold cost. All source/configuration edits
and every compiler stage remain in the full objective, which is active and incomplete.

## I072: qualify shared classification vertex mapping after baseline repair

Started 16 November 2026 at 12:46:29 UTC; first deadline
13:31:29 UTC. I071 is published at 642f81e1, with ROOT required
333/0/0 in 103.798s, merged worktree/branch removed and six independent archived
replay builds passing after disposal. Its final receipt verifies all 484 C/C++
inputs and the remote head. The preceding direct-question turn answered the user
without implementation; this continuation completed I071 qualification/publication.
The complete all-stage goal remains active and incomplete.

Resume the isolated I070 mapping candidate, whose first fixed CPU matrix has never
run. Native attribution found 11,876 reused-edge path lookups plus a separately
constructed 1,749-entry old-to-current map. Share that map with both consumers;
preserve deleted vertices for reverse invalidation and current reordered IDs.
Run the existing minimal insertion/removal/classification tests at O0 and O2,
including the newly repaired failed-classification cascade. Keep only with the
original independent-image eight-case CPU gate: established active-body or unused-
flex gain and no unrelated established regression. A failed completed matrix is
final; do not retry it or waive the gate. Required uncached <=120s, affected full
integration and complete LLVM/diagnostic parity remain publication requirements.
One heavy job, 45 minutes plus at most one extension after concrete progress.

### I072 rejected after both mandatory gain cases completed

Source d4a9ea12 / compiler c74126b4 passes 124 focused compilations and 333 required
checks uncached in 104.454s. Both immutable-image gain cases pass diagnostic and
executable parity, but neither establishes a gain: body 31 pairs 1.463372 ->
1.472969 CPU seconds, ratio interval [.998171, 1.015463]; unused-flex 15 pairs
.528396 -> .527662s, interval [.981202, 1.004864]. The real application replaces
11,876 repeated edge path lookups with one 1,749-entry vertex map, but the required
whole-process benefit is absent. The acceptance gate therefore cannot pass.

Reject the candidate without retry. Stop the remaining matrix in its top-level
case through its verified dedicated process group; that incomplete case supplies
no timing claim. No full eight-case matrix, integration/LLVM qualification or
optimization acceptance is claimed. Keep branch codex/incremental-classification-
mapping-i072 and its immutable source/ELF/data, remove only its clean owned worktree,
and retain one build for the next iteration. Production remains qualified I071.
The full compiler-wide goal is active and incomplete.

## I071: propagate classification changes before unresolved diagnostics

Started 16 November 2026 at 11:45:48 UTC; first deadline 12:30:48 UTC.
I070 closes unqualified before its deadline. Its optimization and native attribution
probes stay on codex/incremental-classification-setup-i070, with immutable source/ELF
and compiler data archived. Start this repair from published I069 master, excluding
all I070 compiler changes. The full compiler-wide objective remains incomplete.

A seven-line program with a provider and two cached forwarding getters, followed
by deletion of the two-line provider, reproduces missing diagnostic notes in the
published compiler. GDB shows the middle parameter demoted to a literal while the
outer parameter stays promoted; the clean build demotes both. The resolver checks
classification reactivation only behind a global all-references-resolved barrier,
which a missing provider prevents. Propagate committed producer changes through
cached consumers before final unresolved diagnostics, without globally discarding
unaffected products. Verify error, repeated error, repair, deeper chains and
unrelated-error cases against clean builds, including diagnostic records and notes.

This is a mandatory isolated correctness repair. Measure the original fixed
independent-image eight-case CPU matrix to report its cost; no I070 optimization
is accepted by waiving its gate. Require required uncached <=120s, full affected
integration, complete LLVM/diagnostic parity after two edits in four real-program
cases, and root qualification before publication. One heavy job and at most one
45-minute extension after concrete progress. Keep the full all-stage objective.

### I071 candidate and measured repair cost

Frozen source f29a2002 / compiler 7d9989b8 passes 45 focused compilations each at
O0 and O2. The resolver now propagates completed classification exports after
each resolution pass, independently of unrelated unresolved calls. Definition
exports wait for their own definition boundary; visible-name environments wait
for definition completion, matching declaration-name indexing rather than call
execution. Deeper removal/repair chains retain unrelated classifications, and
named-declaration edits agree with clean builds. The named-declaration case also
passed baseline; it covers the readiness boundary, not a newly discovered bug.

All 333 required checks pass uncached in 105.933s. The original eight-case
independent-image CPU cost matrix is complete, with executable/diagnostic parity.
No case establishes a gain or regression: body 1.467350 -> 1.474279s, unused-flex
.532457 -> .525427s, top-level 1.850979 -> 1.853460s, unused .533905 -> .533772s,
unchanged .028668 -> .028591s, comment .029166 -> .029282s, wording
.034690 -> .034540s, cache-disabled cold 5.302783 -> 5.223361s. The paired 95%
ratio interval contains one in every case. All 186 measured unchanged/comment/
wording builds perform zero object builds, code generation and linking.

One extension through 13:15:48 UTC is recorded for full integration, complete
LLVM parity, root qualification and publication. No new variant or acceptance
timing retry is authorized by this extension. I070 remains isolated and unqualified;
the full compiler-wide goal remains incomplete.

Full qualification completes at 12:39 UTC: all 462 integration checks pass in
764.083s, and all twenty real-program builds pass complete LLVM text and diagnostic
parity after two edits in each of four cases. The source/ELF audit verifies all
462 C++ and 22 C inputs, excludes I070 changes and checks that transient graph
encoding provenance is absent from persisted data. Root qualification and publication
remain; this does not complete the full objective.

Root qualification completes at 12:44 UTC: compiler 03e6cf76 matches all 484
qualified C/C++ inputs, passes 45 focused compilations at each optimization level,
and passes all 333 required checks uncached in 103.798s. The ROOT executable is
archived and verified before publication. This is a necessary correctness repair;
no optimization gate is waived and the full goal remains incomplete.

## I070: attribute and reduce classification setup work

Started 16 November 2026 at 11:13:14 UTC; first deadline 11:58:14 UTC.
I069 is published at a50c115b, with worktree/branch disposal and six independent
archived replay compilations passing. Its complete fixed CPU gate and all source,
required, integration and LLVM parity checks pass. Full goal remains incomplete.

The next source boundary is classification selection inside resolution setup:
42.885ms of the 117.377ms parent median on active-body edits. Attribute input,
query/provider product loading, changed-target checks, edge reuse and propagation
using actual work counts on the frozen real LEXI8 program after two distinct edits.
Do not assume candidate matching dominates the existing dependency sub-timer.
Select one measured repeated operation, add focused cascade/work checks before its
implementation, and keep the change only with the original fixed eight-case CPU
gate (gain on body or unused-flex, no established unrelated regression). Required
uncached <=120s, full affected integration, LLVM/diagnostic parity and root checks
remain mandatory. One heavy job, at most one 45-minute extension after concrete
progress. Close with actionable attribution and move to another producer if this
boundary has no worthwhile candidate; no repeated failed matrix or speculative
cache architecture. The full objective still covers every source/config edit and
all compiler stages and output modes.

### I070 attribution completed; optimization remains unqualified

Native probes on two real-program edits split classification dependencies into
7.6-7.8ms edge preparation, 8.1-9.2ms reuse eligibility, <1ms reverse mapping and
<0.6ms components. There are 491 candidate checks, 11,876 prior edge mappings,
24,030 reverse-edge visits, 1,749 eligibility checks and 13,638 match-key checks.
The candidate shares the existing old-to-current vertex map before both consumers.
It is not merged: the focused removal case finds a production bug before timing.
The first fixture's invalid inline getter spelling was corrected before compiler
assessment; both red attempts remain. The diagnostic discrepancy also occurs on
published master. GDB and the minimized source are retained. No timing matrix is
run and no performance claim is made. Source/ELF and data are archived under I070;
the source branch remains isolated while I071 repairs the baseline.

## I069: attribute dependency persistence and remove repeated publication work

Started 16 November 2026 at 10:10:44 UTC; first deadline 10:55:44 UTC.
I068 is qualified, pushed at fe501029, and disposed with six archived replay
compilations passing. The previous goal turn made publication/build progress;
the intervening direct question was answered without implementation work.

Use the frozen real LEXI8 application and two distinct active-body edits. Inspect
encoded and semantic dependency graph field sizes and changed owner/variant counts;
separate capture, encoding and payload-load costs without adding overlapping parent
timings. The existing 31-pair candidate medians are 72.940ms graph capture,
50.866ms graph encoding and 81.370ms total payload loading. IR generation is
40.355ms; the 509.212ms codegen/link parent includes snapshots and publication.

Select one producer/consumer boundary from that evidence. Add focused correctness
and work-counter checks before changing it, then run the original fixed eight-case
independent-image CPU matrix. Keep an optimization only with a paired CPU gain
excluding parity in active-body or unused-flex and no established unrelated
regression. A completed failed matrix is final for this variant. Full affected
integration, LLVM/diagnostic parity and uncached required <=120s precede publication.
One heavy job, at most one 45-minute extension after concrete progress; if attribution
does not identify a worthwhile boundary, close it with evidence and move to another
plan item. The complete all-stage objective remains active and incomplete.

### I069 measured boundary and qualified candidate

Two actual body edits on frozen LEXI8 source d913a7db show that the second edit
changes twelve matched records and one inference unit, while the complete graph
is 1,132,589 bytes. Ordered paths, forward/reverse edges and instantiation lists
remain unchanged. Loading now retains the compact encoding. Capture publishes
actual edge/variant change proofs; exact ordered path equality permits column
reuse, while new/removed paths rebuild all indexed columns. The persisted format
remains unchanged. Internal encoding/provenance fields are not persisted.

The first required run catches an experiment regression: early constraint errors
retain a loaded graph without running capture, so its initial change markers were
absent. A two-line type-provider rename reproduces the extra internal error.
Decoding now initializes unchanged provenance; capture replaces it with observed
changes. Repeated-error/repair tests and all existing I068 constraint checks pass.
The first source/ELF, failed suite and minimized red evidence are retained.

Frozen source e7bce926 / compiler 0b03ef77 passes 20 graph checks at each of O0/O2
and 37 constraint checks at each level, 333 required checks uncached in 103.716s,
462 integration checks in 756.024s, and twenty complete LLVM text/diagnostic
comparisons after two edits in each of four real-program cases. Raw bitcode may
differ in serialization order; only the first LLVM ModuleID comment is normalized.
The fixed matrix establishes active-body CPU improvement: 1.480902 -> 1.464755s,
paired ratio [.982499, .990949]. None of the other seven cases establishes a
regression. Cache-disabled cold 5.262873 -> 5.234221s, interval [.984243, 1.006155],
does not establish a change. No completed timing case is repeated.

Median active-body graph conversion/encoding falls 50.050 -> 36.062ms; payload
loading 80.897 -> 74.710ms. Four compact columns and 13,804 conversion records are
reused, with 9,870 records converted. One body is inferred and 367 IR bodies reused
in both images. All 186 unchanged/comment/wording control builds perform zero
object builds, code generation or linking. Complete-payload serialization remains.

Use the single extension through 11:40:44 UTC, preserving the first 10:55:44
deadline. The full fixed gate and focused/required evidence precede this extension;
it covers integration, LLVM parity and root qualification/publication only. Both
staging guard failures collected zero timing samples. Only completed caches owned
by this task were pruned, with file hash ledgers; reports, source and graph snapshots
remain. Worktree qualification audit passes; pre-merge master matched the
qualified I068 baseline. The full compiler-wide goal remains incomplete.

### I069 master qualification and publication

Merged source e392b7b3 / rebuilt master compiler 5830570b matches all 462 C++ and
22 C inputs. All 114 focused compilations pass, followed by 333 required checks
uncached in 103.055s (103.946s driver), with source/ELF unchanged. The complete
worktree audit, CPU gate and root audit pass. Archive and worktree-disposal replay
receipts are retained in i069-final-state.json after publication. The complete
compiler-wide objective remains active and incomplete.

I069 publication/disposal completed at 11:12:17 UTC, before the extended deadline.
Master and origin match a50c115b; the root compiler and all 484 C++/C inputs remain
verified. Both merged worktree and branch are removed. Six post-disposal archived
compilations pass, with independent executable copies and executable/diagnostic
parity. One qualified build is retained at its exact source root with all 484
input hashes/timestamps. Evidence: i069-final-state.json and replay staging receipt.

## I068: demand recorded declared-constraint source products

Started 16 November 2026 at 08:53:16 UTC; first deadline 09:38:16 UTC.
I067 is closed unqualified and preserved on its branch, with the fixed matrix and
GDB attribution complete. Start from published production 8b15515e; exclude all
lazy-flex eligibility and binding-domain changes. The same worktree path is reused
for build/data-root comparability; its executable still belongs to I067 until rebuilt.

The five-line production ordinary-function repro crashes after integer -> float
where clean reports a type error. Retain a compact per-owner declared-constraint
source summary at the matcher/binding boundary. The actual constraint collector
demands a recorded deferred owner before reading its variable declarations. Include
all typed source references, including direct parameter bindings, and preserve
unvisited summaries through cached and failed builds. Keep existing binding storage
and partitioning. Expose summary reuse/scans and actual consumer-demand counts.

Add minimal local/parameter declaration, inactive edit, provider change, invalid
argument, diagnostic and repair checks first at O0/O2. Require required uncached
<=120s, full affected integration, and complete LLVM/diagnostic parity after two
edits in four real-program cases. Measure the original eight-case independent-image
CPU matrix to report correctness-repair cost; no optimization is accepted by waiving
I067's failed gate. Qualify root before publication. One heavy job; at most one
45-minute extension after concrete progress. The full all-stage incremental goal,
including efficient shared binding products and non-native/tooling paths, is incomplete.

### I068 source demand and dependent inference inputs

The production ROOT compiler reproduces the five-line crash. Per-owner
`constraint_sources` summaries now retain typed source-reference presence under
the existing validated body/matcher product. The constraint collector demands a
recorded owner before reading its variables. Reuse/scanning/reference counters
show that unchanged declaration summaries do not traverse source references.
No flex eligibility, lexical binding partition or binding-record shape changes.

The first candidate repairs source demand but a provider edit exposes a further
verified edge: one constraint probe recomputes the new float constraint, while
inference reuses an old failed getter with zero body passes and an integer error.
Inference unit inputs covered signatures but omitted fixed local constraint values.
Include those portable constraint/type results only in their owning source unit's
body input, using the existing semantic scope traversal and encoding. Callable
signature inputs remain independent. The provider edit now reaches inference.

Compiler a6907d7f passes 37 focused compilations at O0 and 37 at O2: valid/invalid
arguments, local and direct-parameter annotations, provider body edits, missing
providers, unused declaration validation, final-annotation removal and repair.
No full qualification or timing claim is made yet. Required and the original
cost matrix must use frozen source/ELF artifacts. I067 stays unmerged.

### I068 complete required/cost qualification; one extension

Frozen source b7b4e9ee / compiler a6907d7f passes all 333 required checks uncached
in 101.178s (102.092s driver), with every compiled/test input unchanged. All eight
original cost cases complete with independent executable-image receipts and
executable/diagnostic parity. Active-body CPU interval [.996992, 1.008607] and
cache-disabled cold [.998595, 1.024612] establish no change. Unused-body interval
[1.001766, 1.014477] establishes a small cost. Unused-flex improves in this run;
this correctness repair makes no general optimization claim. No timing samples
are repeated. All 186 measured unchanged/comment/wording builds have zero objects
built, code-generation/link time and link time.

Use the single extension through 10:23:16 UTC, keeping the original 09:38:16 UTC
deadline. The two verified stage-consumer repairs, 74 focused compilations,
required pass and original matrix establish concrete progress. Full integration
is running; complete LLVM comparisons and root qualification/publication remain.
This does not qualify a lazy-flex optimization or finish the compiler-wide goal.

### I068 worktree qualified; root verification remains

Frozen source b7b4e9ee / compiler a6907d7f passes the complete affected integration
suite: 462 / 0 / 0 in 757.179s (758.546s driver), with source/ELF unchanged.
Twenty real-program compilations establish complete normalized LLVM text and
diagnostic parity after two edits in each of unused-flex, unused-body, active-body
and game-data cases. Only the first ModuleID comment is removed; raw bitcode may
differ in serialization order. No code changed after either suite or the matrix.

The requirement-by-requirement worktree audit passes, including exclusion of every
I067 lazy-flex/binding-partition change. Production baseline 260f5150 and all its
462 C++ input hashes remain verified before local merge. Root build, focused checks,
required uncached <=120s, publication and post-disposal replay remain. The complete
compiler-wide goal is still active and incomplete.

### I068 publication complete

Master compiler 3ef37cd6 passes 74 focused compilations and 333 required checks
uncached in 102.290s. All 462 C++ input hashes match the qualified worktree.
Published master fe501029 matches origin. The merged worktree and both owned
merged branches are removed; one build is retained with verified input timestamps.
Six post-disposal archive compilations pass, with independent executable copies
and executable/diagnostic parity. The initial staging guard required space cleanup;
only two completed I067 profile caches were removed, with a file hash ledger and
all reports/sources retained. Final closure was 10:10:41 UTC, before the extended
10:23:16 deadline. Evidence: i068-final-state.json, i068-published-root-audit.json,
i068-post-disposal-replay.json and its staging receipt. Full goal incomplete.

## I067: shared lexical binding dependencies for deferred flex bodies

Started 16 November at 07:48 UTC; first deadline 08:33 UTC. I066 is published
and its post-disposal replay passes. The intervening direct question received a
read-only answer; it did not add compiler implementation progress.

Partition binding products by independent source bodies, joining domains only
where real lexical ownership connects them. Preserve existing lexical semantics:
flexes still see applicable ancestor declarations and references. Persist the
names that may bind outside each body; an ancestor consumer demands connected
body products before inference. Late local demand must neither reset unrelated
inferred variables nor overwrite another body's binding products. This completes
the missing producer/consumer boundary from I065, rather than restricting lazy
flexes to variable-free bodies.

Add minimal storage-origin, same-name independent locals, shared ancestor edits,
typed constraints and activation tests before implementation. Retain the I065
parameter-demotion, missing-provider and overload cascade checks. Require clean
runtime/diagnostic/LLVM parity and the uncached required suite <=120s. Keep only
with a paired CPU gain excluding parity on body (31 pairs) or unused-flex (15),
and no established unrelated regression across game-data/unused (15 each),
unchanged/comment/wording (31 each), cold (11). Full affected integration and root
qualification precede publication. One heavy job and at most one 45-minute
extension after concrete progress. A failed full matrix is final for this variant.
The full compiler-wide goal, including broad section/setup and other modes,
remains active and incomplete.

### I067 producer/consumer checks

The initial baseline passes eighteen new behavior compilations and fails the
skipped-work assertion: neither independent flex body remains deferred. The first
implementation's source-record initialization used canonical paths where readers
use compact identities; project-wrapper GDB confirms and the producer is corrected.
The six-line typed-local repro then confirms that the constraint collector must
demand recorded local declarations before collecting its queries. The source
product now retains ancestor import names and local-constraint presence; the
existing query cache still resolves and validates those constraints.

Compiler 0c242add passes all 59 new compilations but required reports 332 passes
and one failure in 49.944s. The existing nested-global repair test exposes a union
ordering error: joining a local domain after its global connection makes an inner
function the representative of top-level references. GDB confirms the resulting
nonancestor input traversal. Join explicit global domains before lexical-owner
connections. Corrected compiler 23e915a9 passes all 59 new compilations and all
21 binding-environment compilations. No assertion is weakened. The precedence
fixture uses explicit captures and literal-only replacements to retain its
original no-body-product premise and all original work assertions. Full required
and the fixed performance matrix remain pending; no optimization is accepted yet.

The corrected required run reaches 332 passes and fails an old eager-work count
in 51.476s. The isolated original fixture records zero restored references, two
encoded bodies, zero matcher visits and identical behavior. Require those exact
stronger reuse counts instead of two restored references/one encoded body. All
33 definition-resolution builds then pass, retaining all runtime/diagnostic,
provider-removal, binding and classification assertions.

Use the single permitted extension through 09:18 UTC, retaining the original
08:33 deadline. Concrete implementation progress is established by the 113
focused compilations and verified producer/consumer repairs. Complete the fixed
matrix and full affected qualification or close unqualified; no new optimization
variant or failed-matrix rerun is authorized. The whole compiler goal is incomplete.

### I067 complete constraint-source inventory

Compiler 23e915a9 passes all 333 required checks uncached in 102.207s. A subsequent
four-line direct-parameter probe still finds a real wrong result: integer -> 2.5
is accepted incrementally while clean rejects it. Bound pattern parameters bypass
ordinary local grouping, so recording constraints only from binding queries omits
their declarations. Capture all typed source variable references at the binding
publication boundary. The constraint consumer remains the existing collector.

A separate five-line production probe confirms the same stage gap already crashes
ordinary functions with a typed local on an integer -> float edit. Keep both
minimal repros and the new negative/repair tests. Compiler b3a42350 passes 69 new
compilations at O0 and 69 at O2, plus 21 binding-environment and 33 definition-unit
compilations. Full required and timing qualification must use this final artifact;
the preceding green required run does not qualify its subsequent correction.

### I067 closed unqualified: measured global binding-scan overhead

The original complete matrix is final. Active-body paired CPU ratio 1.006754,
95% interval [1.002730, 1.014008]; unused-flex 1.024255 [1.007968, 1.045177];
unused body 1.016752 [1.004519, 1.022245]. Game-data 1.015258
[.995123, 1.020251] and cache-disabled cold [.993153, 1.009250] establish no
regression or gain. Unchanged improves; comment and diagnostic-wording intervals
include parity. All independent-image identity, executable and diagnostic checks
pass. No matrix samples are rerun or selected for acceptance.

Active edits keep 519 bodies encoded instead of 341, while both infer one body
and reuse 367 IR bodies. Binding total rises 25.064 -> 30.169ms even as its input
fingerprinting falls 22.856 -> 5.477ms. These scopes overlap later inference.
GDB work attribution confirms three binding passes / 216 name visits / 2,444
reference visits on baseline, versus 31 / 1,861 / 18,775 on candidate. Late local
demand still filters the complete global unresolved-reference index. This extra
work is verified; it does not by itself attribute every whole-process CPU delta.
The initial GDB breakpoint matched inlined lambdas as well; preserve that failed
probe and the corrected exact-address probe. Instrumentation timings are not
acceptance samples. Extracted executable images are verified and pruned.

Full affected integration and whole-LLVM qualification are not run after the failed
CPU gate. Required passes 333 / 0 / 0 in 103.321s on frozen source 8c27e64f /
compiler b3a42350, with 69 O0, 69 O2, 21 binding and 33 definition checks. The root
compiler and all 462 C++ inputs remain unchanged. One extension used; the
experiment closes before its 09:18 UTC limit. Evidence is i067-final-state.json,
i067-performance-analysis.json and i067-binding-work-profile/{report,baseline-work,
candidate-work}.json. Next isolate only the mandatory typed-constraint consumer
repair from production, using recorded declaration metadata and keeping existing
binding partitioning. Broad semantic section/setup, persistence, shared binding
products, debug/trace, tooling and other modes remain incomplete.

## I066: include every source-match producer in inference dependencies

Started 16 November 2026 at 06:15 UTC; first deadline 07:00 UTC. The preceding
iteration made concrete progress by implementing/probing I065 and reducing a
pre-existing wrong-result bug, but the lazy-flex candidate remains unqualified
because its shared lexical binding domain is unfinished. Production is 1a0dc752
with unchanged I062 C++ / compiler 738252dd. Start this independent correctness
repair from production and exclude all I065 eligibility/expansion changes.

Isolate the seven-line regression for an ordinary cached caller through a flex.
Adding a typed overload must change its runtime result from 3 to 0, editing that
overload must produce 4, and removing it must restore 3, matching independent clean
builds. Read existing native/expanded/source reference product identities for every
inference source unit and compiled declaration fingerprints independently of native
emission. Use the same source-match inputs to version persistent matching edges.
No new persistent store or format, lazy-flex binding change, or compatibility path.

Complete when the minimal regression, full required suite uncached <=120s, full
affected integration and complete LLVM/diagnostic comparisons pass on frozen source
and compiler artifacts, followed by root qualification/publication. Measure the
fixed independent-image real-program matrix: body 31 pairs, unused-flex/game-data/
unused-body 15 each, unchanged/comment/wording 31 each, cold 11. Record work reuse
and any cost; correctness cannot preserve a stale executable to win a timing gate.
No optimization is being accepted by this exception. One heavy job; at most one
extra 45-minute iteration after concrete progress. The broad shared binding-domain
and compiler-wide incremental requirements remain active and incomplete.

### I066 implementation

Share the existing native match, expanded reference-table and source override-table
product keys between matching-edge versions and inference source fingerprints.
Each contribution is identified independently; there is no old-format read path.
Read compiled declaration fingerprints whenever the source unit publishes them,
including units without native code. Keep source/binding/expansion eligibility at
the production implementation. The isolated seven-line test fails on production
with cached output 3 versus clean 0. Compiler cfe26d7b then passes seven compilations
at O0 and seven at O2: overload addition produces 0, body edit 4, removal 3, each
matching independent clean output and diagnostics. No lazy-flex work claim is made.
Qualification uses the fixed uncached required run and eight-case timing matrix.

### I066 source-unit boundaries and single qualification extension

The first required run passes 332 checks and fails one incremental work assertion
in 50.932s: changing only a release body rebuilds its caller entry. Snapshot
comparison proves the class declaration fingerprint stays equal, but its broad
source-owner reference record includes that independent cleanup body. Keep every
assertion. Publish `source_match_units` in the existing matcher projection using
the same semantic scope traversal as matching-edge capture: exclude nested
declarations and independently emitted bodies. Classification retains its broader
source-owner inputs. Include semantic units ineligible for body reuse as well.
This verified boundary requires one nested metadata field, revising the initial
no-format-change assumption; there is no new store or compatibility read path.

Extending coverage to the root exposes its lack of an authored definition-body
record. Its match producer now uses a layout of its own source scopes. Matching
dependency owners also stop at lifecycle boundaries. Preserve the failed root-key
attempt and all earlier source snapshots; no performance comparison has run yet.

Final compiler f567005e passes seven O0 and seven O2 overload compilations plus
all seventeen entry-metadata checks. Cleanup edits preserve entry execution and
rebuild only the cleanup body; layout changes still invalidate the entry. Exposed
global diagnostics and repair remain identical to clean builds. These checks
cover the new boundary before the full required run.

Use the single allowed extension through 07:45 UTC for complete required,
fixed-matrix, full integration, LLVM and root qualification. The original deadline
remains 07:00. Concrete correctness progress is established; no further optimization
variant or confirmation timing is planned. The full compiler-wide goal is incomplete.

### I066 complete candidate qualification

Source 45dc0256 / compiler f567005e stays byte-identical throughout all checks.
The corrected seven-line regression passes at O0 and O2; all seventeen managed/
exposed entry checks preserve cleanup interface reuse, layout invalidation and
fresh diagnostics. Required passes 333 / 0 / 0 uncached in 100.699s (101.611s driver).
Full integration passes 462 / 0 / 0 in 778.959s (780.252s driver), with no skips.
Complete normalized LLVM text and diagnostics agree after two distinct edits in
unused-flex, unused-body, active-body and game-data cases: twenty compilations,
eight independent fresh-cache comparisons. Raw bitcode hashes may differ because
of serialization order; only the first ModuleID comment is removed from LLVM text.

| Case | Pairs | Baseline CPU seconds | Candidate CPU seconds | Paired ratio, 95% interval |
| --- | ---: | ---: | ---: | --- |
| Active body | 31 | 1.477610 | 1.477755 | .999546 [.988931, 1.005174] |
| Unused flex | 15 | .521924 | .523241 | 1.004197 [.987367, 1.014384] |
| Game data | 15 | 1.856064 | 1.853802 | 1.003025 [.996439, 1.008917] |
| Unused body | 15 | .532906 | .534592 | .999703 [.985118, 1.003164] |
| Unchanged | 31 | .029004 | .029080 | 1.010337 [.979049, 1.014565] |
| Comment | 31 | .029323 | .029570 | 1.006400 [.985528, 1.045871] |
| Diagnostic wording | 31 | .034399 | .034647 | 1.005409 [.991646, 1.034984] |
| Cold without incremental | 11 | 5.247687 | 5.256788 | .995354 [.986387, 1.011371] |

All matrix executable/diagnostic comparisons and independent-image identity checks
pass. No CPU interval establishes either a gain or a regression; do not claim
equivalence. Active edits retain 2,116 matching records instead of 1,241, capturing
2,103 instead of 2,978. Both infer one function and reuse 367 IR bodies. The observed
85.597ms deferred-query cost is one probe, not a broad batch of unchanged probes.
An initial analysis divided by a skipped inference phase's zero timer; absolute
timings are preserved and that phase's ratio is explicitly unavailable. Timing
samples were not rerun. Evidence is i066-final-performance-analysis.json and
i066-final-qualification-audit.json. The full compiler objective remains incomplete.

### I066 root verification

Local merge c0a220df rebuilds as compiler 260f5150 with all 462 C++ input hashes
matching the qualified worktree. Both seven-build O0/O2 regressions pass at the
root paths. Root required passes 333 / 0 / 0 uncached in 100.488s (101.406s driver),
with source and compiler unchanged. The root binary's different hash reflects its
recorded build/data paths; source equality is checked explicitly.

Next investigate remaining matcher/declaration setup with the existing timers and
actual consumer counts. The 85.597ms deferred-query total is one probe, so do not
treat it as repeated unchanged-query work. First-cache IR observations show about
3.1s frontend, 2.0s inference and 4.4s codegen/link work, but are not a paired cold
performance comparison. Attribute recording versus semantic work before another
optimization. The I065 shared lexical binding-domain product is still required;
do not re-enable lazy flexes through a narrower eligibility rule. Semantic section
construction, broader persistence, debug/trace, tooling and other output modes also
remain in the full objective. Record the next actual start/deadline before work.

## I065: demand unchanged flex-function source and resolution products

Started 16 November 2026 at 05:40 UTC; first deadline 06:25 UTC. The previous
goal turn completed I063's fixed-matrix rejection and I064's consumer attribution,
changing the next action. Production is 78848fc0 with I062 C++ / compiler 738252dd.
The rejected graph codec remains only on its preserved branch; this work starts
from production in the existing worktree. Rebuild normally after the branch switch;
the executable left by the previous branch is not a valid baseline until rebuilt.

I064 identifies 409 early outlines in flex functions. Couple source retention with
encoded initial/final body-resolution products, retaining all existing source,
classification, lexical environment, candidate-tree and scope-rule proofs. Inspect
expansion and type-probe consumers and require demand before body/binding reads.
Do not enable global-access or nested-declaration units without their own proofs.
Add minimal no-library tests before behavior changes: untouched/activated flexes,
provider edits/removal/repair, new specific overloads, parameter demotion, nested
callers, failed lookups and diagnostic repair. Work counters must prove unchanged
products remain unmaterialized while demanded bodies still resolve correctly.

Keep only with a whole-build CPU gain whose 95% paired interval excludes parity
on active body (31 pairs) or unused-flex edits (15), and no established regression
on the other measured cases. Compare game-data and unused-body edits (15 each),
unchanged/comment/wording controls (31 each), and cold (11). Require executable,
diagnostic and full LLVM parity, required uncached <=120s, full affected integration
and root qualification before publication. Use independent normalized compiler
images, a single heavy job and no confirmation reruns of a failed complete matrix.
At most one additional 45-minute iteration after concrete progress. The full
compiler-wide objective remains active and incomplete.

### I065 implementation and required correctness repair

The baseline passes the initial 32 behavior comparisons and fails the intended
work assertion with zero deferred flex units. Enabling source and encoded-body
eligibility also requires final body/header products for flex functions. The actual
expansion consumer now demands before constructing parameter/constraint bindings.
No new store, compatibility format or global-access eligibility is introduced.

The first candidate exposes a four-line internal-stage inconsistency: source-match
dependency versions use only native match identities, which flexes do not publish.
Project-wrapper GDB confirms the failing producer. Source-match versions now read
the native, expanded-reference and pre-expansion reference product identities.

The next test reveals an independent pre-existing correctness bug on production
738252dd: adding a typed overload below an unchanged forwarding flex leaves a cached
native caller printing 3, while a clean build prints 0. The minimal source is seven
lines before adding the two-line overload. Inference inputs gate both match and
compiled-declaration reads on native emission, so flex dependencies omit matching
changes. Share the complete source-match input reader between source-graph versions
and inference fingerprints, and read declaration fingerprints independently.
This correctness repair is required even if the performance optimization is later
rejected; isolate it rather than discarding it with a failed optimization.

Compiler 3abeabc3 passes all 39 focused compilations, including the minimal overload
addition/body-edit/removal regression, runtime and constant nested expansion,
provider removal/repair, parameter demotion and failed lookup repair. An inactive
edit retains three flex source/resolution products, restores zero detailed body
references, and retains them through another edit. Activating the caller demands
all three. Preserve all initial failures and the activation-fixture correction:
the original fixture also deleted a declaration; the focused demand fixture edits
only the caller body. No expected behavior or demand assertion was relaxed.
Evidence includes i065-producer-bug.json, i065-overload-repro/report.json and
i065-input-green/report.json. Required and fixed-matrix qualification are next.

The first required run stops at the precedence-product fixture's zero-deferred-body
premise (332 pass / one fail in 48.516s). The fixture tests global precedence reuse
without concrete replay keys, but its flex bodies contain parameter references.
Make those operator parameters explicit and use literal replacement bodies, keeping
every assertion unchanged. Both production and candidate pass the focused four-build
check; its changed-rule case still requires the precedence product to rebuild.
Preserve the original failed suite as i065-observed-required; the corrected run has
a separate prefix. The frozen C++ artifact remains 3abeabc3.

### I065 closed unqualified: flex binding domains are a required boundary

Closed 16 November at 06:14 UTC before the first deadline; no extension used.
The corrected required run passes 332 checks and fails incremental_compilation
in 49.343s on storage-origin summary restoration. A seven-line no-library source
returning a flex-local address reproduces it after adding one discarded literal.
Project-wrapper GDB catches decodeDefinition's lookup of hidden after its demand
hook returns. The hook asks materializeVariableGroups for unit.section, while that
routine partitions flex references into their enclosing non-flex lexical domain.
No variable group is selected, so the local declaration was never published.
Both routines currently assume deferred owners are ordinary function domains.

Do not enable the optimization with a narrower variable-free eligibility rule,
silently ignore the missing definition, or alter flex binding semantics. Correct
deferred flex bindings need a producer/consumer boundary for the shared lexical
domain before this optimization can qualify. Preserve branch 3578d582, compiler
3abeabc3, all failed runs and i065-origin-minimal/gdb.log. No timing matrix or full
integration/LLVM qualification was run; do not claim a speedup or publish this
candidate. The original precedence assertions remain unchanged and its independent
focused baseline/candidate checks pass.

Next independent required goal, I066: isolate the verified pre-existing typed-
overload dependency repair from the unqualified lazy-flex eligibility changes.
Read existing source/expanded matching products and declaration fingerprints for
all inference source units, independently of native emission. Version source-match
edges from those same products. Keep current eager flex binding behavior, run the
minimal add/edit/remove regression and full affected checks, and measure the fixed
real-program matrix. Correctness cannot retain a known stale caller to win a timing
comparison; publish any measured cost honestly. Required stays uncached <=120s.
The shared binding-domain work and full compiler-wide objective remain unfinished.

## I064: attribute broad frontend setup at its consumers

Started 16 November 2026 at 05:32 UTC; first deadline 06:17 UTC. The previous
iteration completed with a measured rejection, not a published optimization.
Production C++ remains I062; 127be110 publishes only the I063 report. Use the
unchanged production compiler 738252dd for this read-only investigation.

Record two consecutive active body edits in the real LEXI8 application. Capture
ordinary compiler statistics separately from debugger observations. Through the
project GDB wrapper, attribute first matcher-segment loads and actual deferred
source-outline preparation to their calling boundary, owner and source identity.
Count work rather than treating debugger elapsed time as a benchmark. Verify the
two sources differ and that compilation succeeds with the same compiler bytes.
The investigation completes with an identified consumer and concrete next skip
proof, or evidence rejecting that boundary. No compiler behavior changes in this
iteration. Consult I031/I032/I057 results before selecting another hypothesis;
do not recreate their already rejected catalog/topology variants. Full semantic
source/section reuse and the complete compiler-wide objective remain unfinished.

### I064 result: early consumers and remaining eligibility gaps identified

Closed 16 November at 05:39 UTC without changing compiler behavior. Two complete
production GDB traces each attribute 248 first matcher-segment loads to source-owner
eligibility and eight to classification. A second trace, recording owner globals,
observes 249/seven in its different absolute application directory. Both repeat
exactly within their own directory and load all 256 segments. Preserve the research
audit failure which initially assumed source-dependent bucket assignments would be
identical across those directories; no compiler assertion or expectation was changed.

On each edit, 710 source outlines prepare during source-owner selection: 409 belong
to 306 flex functions, 281 to 30 ordinary functions with directly declared globals,
and 20 to two other ordinary functions. Another 23 outlines prepare on later body
demand. These are product calls and authored-header groups, not counts of unique
semantic compiler units. The edited rendering function is among the global users;
many of the other rejected owners are unchanged. Both source-owner and later body
selectors explicitly exclude flex functions and global access. Source inspection
confirms these guards; merely removing them is not a proved implementation.

Separate ordinary compilations of two consecutive edits measure resolution setup
123.583/115.995ms, cached-body selection 45.116/41.139ms, and source-owner selection
47.962/42.810ms. Only 2.935/2.225ms restores 1,937 outline nodes. Therefore avoiding
outline decoding alone cannot explain or eliminate the complete selection cost.
Do not add another metadata catalogue whose downstream consumers still load every
segment. Evidence is i064-attribution-audit.json, both complete six/three-build
driver reports, original GDB stacks and input manifests. Compiler bytes stay
738252dd; the real application is compiled, never launched.

Next implementation goal, I065: couple lazy source products with lazy resolution
products for eligible unchanged flex functions. Existing cloneSectionBody and
ScopedSectionLocalVariableState consumers already call demandBodyResolution before
reading bodies/bindings; verify the remaining expansion/type-probe consumers before
changing eligibility. Preserve current source, classification, lexical environment,
initial/final candidate-tree and scope-rule proofs. Global functions require their
own binding proof and remain a separate boundary. Tests must precede behavior changes
and cover untouched/activated flexes, changed and removed providers, newly specific
overloads, parameter demotion, nested callers, failed lookups and repaired diagnostics.
Keep only with actual avoided downstream work, fresh output/diagnostic/full LLVM
parity and a significant whole-build CPU gain in the fixed real-program matrix,
without established control regressions. Required remains uncached <=120s; full
affected integration and root qualification precede publication. Record the actual
start/deadline before beginning I065, with at most one progress-based extension.

## I063: reuse concrete dependency-record serialization

Started 16 November 2026 at 04:32 UTC; first deadline 05:17 UTC.
The previous turn made concrete progress: I062 was fully qualified, merged,
root-verified and pushed as 237ccdeb. Baseline is that production C++ revision;
the exact-path restored executable 007891a9 has the same qualified inputs as root
738252dd. The full incremental compiler goal remains active and incomplete.

Current active-edit evidence measures 50.439ms encoding the section graph. Its
mutable logical graph remains needed by inference, but unchanged concrete records
need not be compacted and serialized again. Retain the previous compact records
and their exact CBOR byte spans. Rebuild current ID tables, validate reuse against
current IDs and record contents, and copy only proven-equivalent record bytes into
the output. Newly changed records use ordinary encoding. Keep the existing wire
format, corruption behavior and all reader tools; do not add hundreds of artifact
files or move unchanged encoding into the loader. Use an optional generic container
range observer in the existing iterative CBOR reader to retain byte spans without
a second parser. Pass codec state explicitly through graph hydration and snapshot
writers; do not introduce ambient global state.

Before implementation, add independent range-reader tests and minimal cross-process
graph byte-equality/work regressions covering body edits, ID shifts, variant changes,
errors and repair. Require actual record-encoding work avoided, exact fresh graph
payload equality and LLVM/diagnostic/executable parity. Keep only with a CPU gain
whose 95% paired interval excludes parity on real active edits (31 pairs); compare
game-data and unused edits (15 each), unchanged/comment/wording (31 each), and cold
(11). No established unrelated regression. Required remains uncached <=120s; full
affected integration and root qualification are required before publication. One
heavy job, at most one extra 45-minute iteration after concrete progress. If the
producer/consumer boundary adds cost instead, preserve the evidence and move on.

### I063 implementation and qualification extension

The existing iterative CBOR reader now optionally reports selected container spans.
The section graph codec retains those spans and validates old concrete records
against the current dictionaries and logical dependencies before copying their
bytes. Current dictionary ordering is part of that proof: a component regression
remaps source IDs and verifies canonical fresh bytes, catching and fixing an initial
unordered-source comparison. Unit encoding is avoided; dictionary construction,
record validation and copying remain work. No persistent format change is needed.

The codec interoperability tests and twenty minimal cross-process compilations pass.
The body edit copies all three concrete records (33 bytes) without encoding them;
adding a declaration changes IDs and encodes one record while reusing two. Fresh
payload hashes, diagnostics and runtime output agree after body edits, declaration
insertion/removal, a new concrete variant and error repair. Unchanged/comment builds
perform no record encoding or copying. These counters describe serialization work,
not avoided inference. Preserve the failed fixture and component attempts in i063
evidence, including the canonical CBOR numeric-type correction in the range fixture.

Use the single allowed extension through 06:02 UTC for the fixed performance matrix,
uncached required checks, full affected integration, LLVM parity and publication if
qualified. The original deadline remains 05:17. No second optimization variant or
timing resampling is authorized by this extension. The full goal remains incomplete.

### I063 result: reject; encoding work falls without a whole-build gain

Closed 16 November at 05:29 UTC. Candidate bc83a0b8 / compiler eee36ccb passes
codec interoperability and twenty focused compilations. Its fixed complete matrix
copies 548 concrete records / 145,111 bytes on each real active body edit and
encodes zero records. Graph encoding medians fall 50.404 -> 45.362ms; paired ratio
.936782 [.873484, .986666]. Dictionary construction, validation and copying remain.
This is an observed stage gain, not proof that the entire persistence stage or
compilation improves. Other measured phases overlap; do not invent an additive
root cause for the whole-build outcome from their median differences.

| Case | Pairs | Baseline CPU seconds | Candidate CPU seconds | Paired ratio, 95% interval |
| --- | ---: | ---: | ---: | --- |
| Active body | 31 | 1.489938 | 1.487960 | 1.001645 [.997306, 1.008706] |
| Game data | 15 | 1.858447 | 1.868614 | 1.004189 [.985953, 1.007780] |
| Unused body | 15 | .540676 | .537577 | .990622 [.967878, 1.019983] |
| Unchanged | 31 | .028852 | .028974 | 1.009385 [.990480, 1.033663] |
| Comment | 31 | .029679 | .029349 | .991566 [.973698, 1.021798] |
| Diagnostic wording | 31 | .034561 | .034293 | .997013 [.965337, 1.014915] |
| Cold | 11 | 5.225271 | 5.255562 | 1.005055 [.998167, 1.020795] |

The whole-build gate fails: the active-body CPU interval includes parity. All
matrix executable/diagnostic comparisons pass. No control establishes a regression;
that does not establish equality. Required passes 333 / 0 / 0 uncached in 96.490s,
with compiler and source inputs unchanged. Full integration and whole-LLVM checks
are not run after this performance rejection. Preserve all original reports and
independent-copy receipts, i063-rejection-audit.json, frozen compiler/data and the
isolated branch. Do not merge these compiler changes or rerun timings to seek a pass.
Production C++ remains I062 / 237ccdeb. The single extension ends early on rejection.

Next bounded goal: attribute the remaining broad resolution setup before choosing
a coupled frontend producer/consumer change. Current production measurements already
show source-owner selection around 44ms and cached-body selection around 44ms within
roughly 120ms of resolution setup; these nested timings must not be added to their
parent. Identify which unchanged units and products those consumers actually demand
on two consecutive real edits. Reuse the earlier I031/I032 and I057 findings instead
of repeating an already rejected metadata-only or topology hypothesis. Inspect the
remaining semantic source/section boundary, not another serialization variant. Write
the implementation's exact skip proof, minimal cascade cases and fixed acceptance
gate before changing compiler behavior. The compiler-wide objective stays active.

## I062: invalidate only affected inference-query proofs

Started 16 November 2026 at 03:33 UTC; first deadline 04:18 UTC.
The previous goal turn made concrete progress: I061 was fully qualified, merged,
root-verified and pushed as 8f5574d0. Its measured source-only stage improvement
and missed original whole-build target remain recorded. Baseline C++ is that
production revision; the exact-path restored worktree executable ad939de7 has the
same qualified C++ inputs as production 9956166d.

First attribute interfacesChanged calls, proof-set sizes and subsequent work on
two consecutive real active edits using the project debugger wrapper. Debugger
elapsed time is not a benchmark. A whole-set reset is present in source, but its
cost and number of unrelated retained proofs are not established yet. If material,
retain positive query proofs outside the changed query's transitive consumer set.
Both interface calls and observed-value edges, cycles, failures and native/semantic
eligibility changes must participate. Do not replace invalidation with a new global
proof on every query. Add minimal no-library work/cascade regressions before code.

Keep only if actual work counters show unaffected proofs retained and affected
consumers rechecked, fresh diagnostics/executables/full LLVM agree, and the fixed
normalized real-program comparison establishes a CPU reduction with a 95% paired
interval below parity. No arbitrary minimum percentage: code size and avoided work
must justify a measured gain. Compare active body (31 pairs), game-data (15), unused
(15), unchanged/comment/wording (31 each), and cold (11); no established unrelated
regression. Run the required suite uncached within 120s and full affected integration
before publication. One heavy job. At most one additional 45-minute iteration after
concrete progress; otherwise reject/park and move to another measured boundary.
The full compiler-wide goal remains active and incomplete.

### I062 attribution and first implementation

Two consecutive real active edits on baseline ad939de7 each call unchanged 2,800
times and visit 2,158 dependency units. The three clear sites discard 538 semantic
proofs, 538 native proofs, then zero proofs. Exact project-wrapper GDB records are
in i062-proof-work; debugger timing is not used for performance. Probe-only compiler
1a0e66f9 then measures a median 42.597ms checking cost over five real active edits,
with 1,076 proofs invalidated each time. This makes the repeated proof work material.
The probe-only artifact and source 0f3d1857 are preserved before implementation.

Share direct-input and positive state in stable per-query nodes; register consumers
when checking callee-interface and observed-value edges. Acceptance walks only that
recorded reverse closure. Positive cycles still commit only after the whole query
succeeds. No persistent format changes or eager whole-graph reverse index is added.
Twenty minimal compilations pass, including runtime output, mutual recursion,
an unrelated edit, recursive-body edits, provider removal/repair, changing known
pure values and skipped-work counters. The simple edit retains three proofs and
invalidates zero; baseline discarded two. This is work evidence, not a speedup claim.

Preserve the initial authored-fixture failures separately: unsupported inline
value-body syntax, an undiscarded printf result, and an expectation that counted
the executable name as a user argument. The actual compiler implementation and
clean/incremental output establish zero user arguments in these runs; the corrected
fixture also executes both sides of its recursion. No compiler failure was masked
or expected diagnostic changed. Full suite/IR/performance qualification is next.

### I062 fixed matrix result and single verification extension

Source 75171e41 / compiler 007891a9 passes all 333 required checks uncached in
100.928s, with source/compiler hashes unchanged. The fixed complete normalized
matrix passes executable/diagnostic parity. Body CPU is 1.502352 -> 1.484659s,
paired ratio .988326 [.981961, .997919], a 1.17% gain. Game-data ratio is .990097
[.982835, .997699], a 0.99% gain. Inference is 328.535 -> 318.735ms, paired ratio
.969400 [.946321, .981222]. Dependency-unit checks fall 2,158 -> 1,085; inferred
function bodies, inference restorations and retained IR counts are unchanged.
The candidate reports 31.192ms query checks and .004533ms invalidation. Retention
counts are per event, not distinct units; the 1,614 count includes repeated retention.

The unused-edit point estimate is slower: ratio 1.024162 [.999970, 1.034981]. This
interval narrowly includes parity; it does not establish either equality or a
regression. Preserve that uncertainty rather than claiming every control is flat.
Unchanged, comment, wording and eleven-pair cold controls also establish no
regression. The predeclared gate passes; no confirmation timing will replace this
complete first matrix. Evidence is i062-performance-analysis.json and all seven
original reports/independent-copy receipts.

Use the one allowed 45-minute extension through 05:03 UTC for full LLVM parity,
full integration and root qualification/publication. The original deadline remains
04:18. This is verification after concrete implementation and measured progress;
there is no additional optimization variant. The full compiler goal remains active
and incomplete.

### I062 complete qualification

Compiler 007891a9 and all qualified C++ inputs remain unchanged through all runs.
Twenty focused builds pass, including independent fresh output/diagnostic checks,
mutual recursion, provider removal/repair, value-dependent branching and actual
work retention. All 333 required checks pass uncached in 100.928s. All 462
integration checks pass in 773.683s (775.055s including the recording driver),
with no failures or skips. Complete LLVM and diagnostics agree after two consecutive
edits in unused-flex, unused, active-body and game-data cases: 20 compilations and
eight comparisons against independent fresh caches. Each pair of edited sources
has different content hashes; these are actual edits, not unchanged invocations.
Formatting and whitespace checks pass; every modified code file stays below 1,000
lines. i062-qualification-audit.json rechecks hashes and concrete artifacts and
records full_goal_complete=false. Preserve the uncertain unused-edit control.

Next bounded producer/consumer goal is P14's section-graph serialization. Actual
active-edit evidence still shows whole-graph encoding despite nearly unchanged
records (I061 measured 38.427ms). Inspect current encoding/decoding and retained
record identities before choosing the product boundary; avoid another catalogue
whose consumers still demand every detailed record. Aim to preserve unchanged
serialization products and rewrite only changed records, with exact codec/fresh
parity, real-program CPU evidence and the same correctness/budget controls. Record
its start/deadline before new experiments. This remaining broad persistence work,
semantic section construction and tooling/output modes keep the overall goal open.

### I062 publication and closure

Merge ba07227b contains the qualified C++ bytes. Root compiler 738252dd matches
all 436 qualified C++ input hashes and passes 333 required checks uncached in
100.539s (100.684s including its recording driver), with source/compiler unchanged.
The merged worktree and branch are removed. Exact-path build objects are retained
separately under the recorded byte-identical-input contract; both tested root
executables and benchmark artifacts have verified lossless archives. A real
six-compilation archived replay passes after removal and cleans its temporary
compiler data root. See i062-final-state.json and the independent-copy receipt.

Close I062 before the single extended 05:03 UTC deadline. No second optimization
variant or confirmation timing was used. Keep the unused-edit uncertainty in the
published measurements; only body and game-data CPU improvements are established.
The full compiler-wide goal remains active and incomplete. The next stage goal is
the bounded P14 serialization producer/consumer work described above.

## I058: demand class match products at their semantic consumers

Started 15 November 2026 at 23:46 UTC; first deadline 00:31 UTC on 16 November.
Baseline is production source bb0eb48b / compiler 19f9b48d. I056's catalogue and
record-family changes are excluded. Class units already have checked source,
provider, shape and match products, but always replay them before semantic demand.

Allow eligible top-level class match units to remain encoded with their concrete
expression templates. Keep existing input proofs and complete-reference coverage.
Demand their matches before cloning or consuming a pending type-expression root,
and expand newly restored concrete expressions at the completed matcher boundary.
Do not create another storage format or general lazy-pointer wrapper. Add a minimal
class edit/activation/constraint-change regression before the implementation; cover
class layout, typed accessors, errors and provider invalidation using fresh parity.

Require >=2% paired CPU improvement in the real unused edit (15 pairs, 95% interval
below parity). Active body (15), unchanged/comment/wording (31 each) and cold (11)
controls must establish no regression. Keep frozen normalized images and full
executable/diagnostic comparisons. If performance qualifies, run focused/cascade,
complete LLVM parity, required <=120s uncached and affected/full integration before
merging. One heavy job; at most one 45-minute extension after concrete progress.
The compiler-wide incremental goal remains active and incomplete.

### I058 single verification extension

The class work-count regression and 13 activation/provider builds pass. The final
composite-producer build also passes 30 graph, 159 matcher/classification and 220
broader compilations. Parameterized class size changes pass. Lifecycle testing
then exposes overlapping ownership: class reference collectors include retain and
release bodies while matcher projection treats them as separate units. The exact
reproducer and project-wrapper GDB trace locate the missing child product at
InitialMatcherProjection::finish, not in the class layout or runtime.

Align source reference collection, deferred-owner lookup and dependency/query
ownership on the existing resolution-unit boundary, including lifecycle scopes.
Use the one allowed 45-minute extension to complete that migration and qualification;
the extended deadline is 01:16 UTC on 16 November. No second extension is allowed.
The first deadline remains 00:31. The full compiler goal remains incomplete.

### I058 result: reject; classification proof migration is incomplete

Final source f6e1686c / compiler 77e61ce0 passes 494 focused compilations
(71 class/lifecycle, 30 graph, 159 matcher/classification and 234 broader checks).
The complete normalized six-case matrix nevertheless rejects it. Unused CPU is
.537981 -> .583379s, paired ratio 1.087667 [1.067538, 1.107354]; active-body CPU is
1.503137 -> 1.547681s, ratio 1.033764 [1.022176, 1.041029]. Unchanged, comment,
wording and eleven-pair cold controls establish no difference. All executable and
diagnostic comparisons pass. Do not merge this artifact or run a confirmation.
Required, full integration and complete IR qualification are not run for it.

Measured unused body resolution rises 27.217 -> 50.937ms and matcher key work
2.631 -> 11.404ms, although grouped restorations fall 1,923 -> 1,010. Normal
body-resolution visits rise 38 -> 943 and fingerprints 171 -> 1,067. A complete
steady-edit GDB trace counts 2,477 restore requests, matching the measured counter;
class replay is avoided but direct matches and ordinary function replay increase.
A separate trace locates 75 ordinary source-owner materializations in
IncrementalClassification::select. The proof trace observes 705 checks, including
one class with 16 lexical references but three resolution-owned references.
canReuse still compares those different domains after lifecycle ownership was
split. That rejects a valid class proof and broadens classification evaluation.
The failed combined key probe and relocated source-line probes are preserved;
relocated line hits are boundary observations, not rejection counts.

Preserve i058-performance-analysis.json, i058-stage-deltas.json,
i058-steady-restore-unit-demand.json, i058-owner-materialization.json and
i058-classification-domains.json with source/data/image manifests. Production C++
remains I052. The compiler-wide goal remains active and incomplete.

## I059: prove lexical classification from its resolution domains

Started 16 November 2026 at 01:07 UTC; first deadline 01:52 UTC. Baseline remains
production compiler 19f9b48d. I058's frozen failed artifact remains rejected. This
iteration corrects the identified proof-domain migration before assessing a new
artifact; it is not another timing of the rejected artifact.

Classification includes enclosing parameter uses in lifecycle bodies. Validate its
complete lexical reference set against the independent resolution-owner products,
including every lifecycle domain. Keep source, provider and lookup proofs and
missing/corrupt product behavior. Add a minimal no-library regression before the
change, asserting that an unrelated edit does not reject the unchanged class and
its dependent classification units; exercise lifecycle edits and parameter
retraction against fresh builds. Do not merge a partial migration.

Require >=2% paired CPU improvement over production in the real unused edit (15
pairs, 95% interval below parity), with active-body (15), unchanged/comment/wording
(31 each) and cold (11) controls showing no established regression. If qualified,
run focused/cascade, complete LLVM parity, required <=120s uncached and affected
full integration before merging. One heavy job at a time. At most one 45-minute
extension after concrete progress. Full compiler goal remains incomplete.

### I059 result: park; fewer replayed references, no whole-build gain

Source 26eb3ae1 / compiler 1d334efe validates every resolution domain within the
lexical classification owner. The minimal unrelated-edit assertion changes from
one to two reused implicit parameters. On the real program all 342 ordinary
owners remain retained, 456 resolution units remain encoded, and grouped reference
restorations fall 1,923 -> 893. Body resolution falls 21.978 -> 19.975ms.

The complete normalized comparison fails the declared gate. Unused CPU is
.446459 -> .446274s, paired ratio .999564 [.990337, 1.014283]; active-body CPU is
1.240544 -> 1.242862s, ratio 1.005111 [.994875, 1.013372]. Unchanged, comment and
eleven-pair cold controls establish no difference. Wording has a small established
regression, ratio 1.006568 [1.001283, 1.015218]. Matrix executable and diagnostic
comparisons pass. Preserve this artifact and do not run a confirmation or merge it.
The first regression's activation also exposes a pre-existing production crash;
there is no full test pass, required/integration run or IR qualification claim.

The reduced class uses a pattern parameter only in release. Neither production
19f9b48d nor candidate 1d334efe binds that parameter into the runtime lifecycle.
The variable remains Unresolved and reaches finalizedExpressionType through
discard. Direct construction also fails; returning from a factory is unnecessary.
Class requests record argument observations, but concrete layout identity contains
field types, not a runtime parameter capture. A user question is pending on whether
to require explicit fields, capture parameters, or allow only compile-time values.
Keep the new regression and GDB/minimal evidence on the parked branch; do not
change its expectation to claim success. See i059-lifecycle-bug.json and
 i059-lifecycle-minimal/report.json. Production compiler source remains I052.

## I060: attribute dependency-summary publication by real phase cost

Started 16 November 2026 at 01:24 UTC; first deadline 02:09 UTC. The class experiments
are parked, with their artifacts and failing new activation test preserved. While
the lifecycle language decision is pending, use a production-based branch for an
independent remaining stage. Do not carry the rejected class optimization into it.

An active edit in the actual LEXI8 program infers one function but spends about
65ms publishing the dependency graph, within a roughly 132ms snapshot. Add optional
scoped timing and work counters for graph setup, section products, match facts,
reverse edges, specialization-unit capture, normalization, global propagation and
lookup publication. Disabled probes must perform no clock reads. Inspect repeated
real active edits, and a minimal known graph, to identify the actual repeated work;
request counts alone do not establish its cost. Preserve cold and fast-path controls
and distinguish overlapping timings. Verify outputs/diagnostics against production.

This analysis iteration completes only when the dominant subphase is measured and
a concrete producer/consumer optimization is chosen. It is not a compiler-wide
completion claim. If keeping the probes in production, focused checks and required
<=120s uncached must pass. One heavy job, at most one 45-minute extension after
concrete progress. The full incremental compiler objective remains active.

### I060 qualification: measured section publication dominates

Source 1a5e9068 / compiler 10da2be5 adds optional graph phase/work probes and moves
the total timer into the shared successful/failed capture boundary. Seven minimal
cross-process builds pass, including a real type error; the original failed-build
timer omission is reproduced before the fix. Disabled probes perform no clock
reads. All 333 required checks pass uncached in 81.273s (82.112s including the
recording driver), with unchanged compiler/source bytes. The five-fixture native
integration comparison passes fifteen builds in 4.087s. The full unrelated library,
LSP and backend integration suite is not rerun for these instrumentation-only changes.

The complete six-case normalized profile compares native executables and diagnostics
with production: five active-edit pairs and three pairs each of unused, unchanged,
comment, wording and cold builds. All comparisons pass. These short controls qualify
probe behavior and artifacts; they do not establish an optimization speedup.
The first attempt is stopped by the storage guard before samples. Its report is
preserved; two inactive compiler inode groups are hash-verified, archived and all
four owned aliases removed before the separately named complete run.

Active-edit median graph publication is 63.572ms. Disjoint phases: setup 5.899,
sections 32.984, reverse 0.852, units 19.344 and lookup publication 0.544ms; remaining
time includes scope cleanup. Matched facts cost 9.330ms within sections. Within
units, capture costs 9.204, normalization 2.654 and global propagation 1.621ms.
The process visits 4,219 sections, captures 2,978 match-fact records, reuses 1,241,
normalizes 548 specialization units and visits 2,928 global dependency edges.
Request counts alone had overemphasized class replay; its measured saving was 2ms.

A preserved real-program graph sequence finds 4,002 source-only sections. On the
second active edit, zero forward, reverse, instantiation, lookup or section-read
records change; twelve match-fact records and one specialization record change.
These counts identify candidates, not the cost of all 4,002 records. Evidence:
i060-profile-analysis.json, i060-graph-analysis/report.json, the graph archives,
i060-graph-work-green/report.json and suite receipts. The full compiler-wide goal
remains incomplete. The instrumentation snapshot is published and root-verified
as recorded below.

### I060 publication: merged and root-verified before the first deadline

Merge 054a1188 contains the qualified C++ bytes. Root compiler d9098025 passes all
333 required checks uncached in 82.956s (83.806s including the recording driver),
with source/compiler hashes unchanged. The merged worktree and branch are removed.
A real archived-compiler comparison passes after removal and cleans its temporary
data root. Verified C++ build inputs and objects are retained separately for an
identical-path checkout; timestamp reuse requires exact input hashes and ordinary
CMake/Ninja flag/dependency validation. No rejected class compiler code is merged.
I060 is complete as an analysis/instrumentation iteration; the full goal is active.

## I061: reuse unchanged source-only dependency section products

Started 16 November 2026 at 02:07 UTC; first deadline 02:52 UTC.
Baseline is published I060 source 8e6746e7 / root compiler d9098025. Use its probes. Persisted section outputs already exist, but the
current reuse selector covers mostly native/inference-selected owners. Reuse a
source-only section's prior graph record when its freshly validated matched
references produce the same edges and it has no new or retained inferred variants,
reads or callable lookups. Preserve main execution, activation, newly selected
providers, failed inference products, removals and canonical lookup ownership.
Do not add another storage format or infer stability from a missing runtime body.

First add a no-library cross-process work regression and activation/provider/edit
parity cases. Require >=25% median reduction in dependency_graph_sections_ms and
>=1% paired CPU reduction for real active edits (31 alternating pairs, 95% interval
below parity). Unused (15), unchanged/comment/wording (31 each) and cold (11)
controls must establish no regression. Complete artifact/diagnostic and LLVM parity,
required <=120s uncached and full affected integration before keeping it. One heavy
job, a 45-minute first deadline and at most one 45-minute extension after concrete
progress. Park a failed candidate rather than moving its acceptance gate.
The class lifecycle semantics question remains pending; its dependent work and
failing regression remain on the separate parked branch.

### I061 first result and single refinement/verification extension

Source 241248ba / compiler 953114b2 passes 17 focused and 30 dependency-graph
builds. The complete comparison shows active paired CPU ratio .986680
[.983958, .997897], satisfying the 1% gate. Unused, unchanged, comment, wording
and eleven-pair cold controls establish no regression; executable/diagnostic
comparisons pass. Sections fall 33.419 -> 25.414ms (ratio .760459), missing the
25% stage gate. The first artifact is not accepted; preserve its full matrix.

The source-only proof repeats ordered-map searches. The dense graph encoder and
decoder guarantee identical forward/instantiation paths, so a found forward entry
needs one variant lookup. Old read/lookup-owner indexes are rebuilt independently;
when this section contributes no current inferred facts, retaining equal edge and
empty-variant outputs does not require retaining its old read ownership.
Perform one refinement to remove those redundant proof lookups, with the original
31/15/31/31/31/11 comparison and gates unchanged. This is a new source artifact,
not a confirmation of 953114b2. No further refinement is allowed this iteration.
The first deadline remains 02:52 UTC. Use the single allowed 45-minute extension
through 03:37 UTC for this concrete refinement and full qualification if it passes.
The full compiler goal remains active and incomplete.

### I061 refined measurement and engineering disposition

Refined source ef2d8a49 / compiler ad939de7 passes the same 17 focused and 30 graph
builds. Its complete comparison retains 4,002 extra section records. Sections fall
33.455 -> 23.587ms (29.50%); active CPU medians are 1.245937 -> 1.236240s. The
paired CPU ratio is .993318 [.989016, .996411]: a measured 0.67% reduction, with
no established control regression. Artifact/diagnostic comparisons pass.
The 25% stage target passes; the agent-chosen 1% whole-build target does not.
The preserved analysis continues to report gate_passes=false. No further timing
or code refinement will be used to obtain a passing number.

The user's objective prioritizes correct incremental units and a speedup, explicitly
without a fixed minimum percentage. Treating the arbitrary 1% screening target as
a reason to discard this small, proven unit-reuse implementation would work against
that objective. This explicitly changes the engineering disposition, not the
measurement or its failed target: qualify the change against complete correctness,
LLVM parity, required <=120s uncached and full integration before publication.
If those pass, publish it as a 29.5% stage / 0.67% whole-build improvement. This is
not a claim that the original numeric gate passed or that the overall goal is done.

The refined run's first attempt was stopped by the capacity guard before samples.
Its files remain unchanged. Hash-verified inactive images were archived to provide
4.6 GiB free, and the separately named complete matrix then ran once. The original
02:52 deadline and single extension through 03:37 remain recorded; no second
extension or further optimization refinement is allowed this iteration.

### I061 complete qualification

The previous conversational turn answered the user's question about integration
tests; it made no compiler implementation progress. This continuation revalidated
the frozen artifact and completed the outstanding suite instead of restarting work.
Source ef2d8a49 / compiler ad939de7 passes all 333 required checks uncached in
80.172s and all 462 integration checks in 791.276s, without source/compiler changes.
The 17 focused and 30 graph compilations pass. Complete normalized LLVM and
diagnostics agree after two consecutive edits in each of unused-flex, unused,
active-body and game-data cases: 20 compilations, eight edit comparisons. The
normalizer removes only the first ModuleID comment naming the inspection input.
The frozen C++ input hashes are checked again by i061-qualification-audit.json;
its full_goal_complete field is false. Formatting and git diff checks pass.

Also correct the README's stale suite description: numeric parsing and class-layout
unit checks run in required, and integration has no overall 120-second limit.
No test selection, expectation or timeout changes are made in this iteration.

Next screened hypothesis is P10 query-proof invalidation. Current
InferenceUnitCheck::interfacesChanged clears every positive dependency proof after
accepting any query; semantic and native checkers repeat this. Record the call
counts, proof-set sizes and actual cost before implementing selective invalidation
through callee-interface and observed-value consumers, including cycles. The 31-pair
active-edit evidence measures 272.276ms inference, 55.721ms preinference and 73.066ms
deferred probing; these scopes overlap and are not additive. Section construction
is 8.266ms. Graph payload encoding separately costs 38.427ms and remains another
candidate. See i061-next-stage-screen.json. No next optimization is accepted from
these coarse timings alone. The compiler-wide objective remains active/incomplete.

### I061 publication and closure

Merge 3ad95fdb contains the qualified C++ inputs. Root compiler 9956166d matches
all 436 qualified C++ input hashes and passes 333 required checks uncached in
99.640s (100.651s including its recording driver). No source/compiler changes
occurred during that run. The worktree and merged branch are removed; its checked
build objects are retained separately under an explicit byte-identical-input and
identical-source-path contract. A six-compilation archived replay passes after
removal and removes its temporary compiler data root. The independent-copy receipt
and both exact archived images remain usable. See i061-final-state.json.

Close this iteration before its extended 03:37 UTC deadline. Keep the small proven
stage improvement with its missed original numeric target visible; no rejected
class-demand code is merged. The full incremental compiler goal stays active and
incomplete. Next is the bounded P10 query-proof invalidation attribution described
above, with its start/deadline recorded before new experiments. There is no further
I061 refinement or confirmation timing.

## I057: attribute unchanged matcher-outcome demand before another optimization

Started 15 November 2026 at 23:32 UTC; first deadline 00:17 UTC on 16 November.
Production remains I052 / compiler 19f9b48d. I055 and I056 are rejected and their
branches/artifacts are preserved. Reuse the existing worktree/build directory for
future source changes, but do not treat its old 6ed70847 executable as production.

On the real LEXI8 unused-function edit, use the production compiler and project
GDB wrapper to count actual restoreResult requests by compiler boundary, source
owner type, flex/generated ownership and reference purpose. Capture representative
call stacks and headers without mutating compiler code. Debugger timings are not
performance samples. Explain why all outcome buckets are still demanded and select
a concrete producer/consumer boundary from the counts. If optimized debug values
prevent attribution, use a narrowly scoped stats probe rather than guessing.
This iteration completes when the demand is attributed with authoritative evidence
and the next implementation goal is declared; it does not complete the compiler-wide
goal. Do not add another storage format or caching layer without that evidence.

### I057 result: class units dominate eager replay

Two complete GDB observations of production compiler 19f9b48d each count 2,239
restoreResult requests on the real unused edit. The first attributes 1,730 to the
AnalyzedSections boundary, 92 to ResolvedFunctionPatterns and 417 to precedence.
The second observes the actual containing BodyResolutionUnit, not just the lookup
scope: 1,028 requests replay class units, 437 ordinary function units, 195 flex
function units and 19 flex section units; 560 requests are direct matches.
Of the class requests, 868 are type constraints and 160 are other references.

Representative stacks lead through restoreBodyResolutionUnits and
IncrementalBodyDemand::restore. The existing selector rejects every class unit by
kind and requires deferred expression sites, although those class units already
passed source/provider/shape checks. Their concrete type-expression templates lack
the ordinary function source-product demand boundary. Metadata-only storage cannot
remove these consumer requests. Debugger timings are explicitly excluded from
performance claims. Evidence: i057-restore-demand.json, i057-restore-unit-demand.json,
the two source manifests and GDB logs. No production compiler code was changed.

The next implementation targets class resolution products and the real consumers
of their concrete type-expression templates, using existing body-demand APIs.
The broader global-function, flex and tooling boundaries remain unfinished.

## I056: retain matcher products from a lightweight catalogue

Started 15 November 2026 at 23:05 UTC; first deadline 23:50 UTC. Baseline is
production source d9f94175 / compiler 19f9b48d. I055 is closed and rejected;
its compiler changes are excluded. Hypothesis: reference/classification metadata
shares hash buckets with full lookup outcomes, and contains/retain currently load
those outcomes just to prove availability. A retained source owner therefore
loads unrelated semantic products before any consumer demands them.

Separate metadata record families in the existing segmented store and persist a
compact membership catalogue per segment. Availability/retention should use that
catalogue and actual producer-file presence; detailed outcomes load on semantic
demand. Unchanged complete segments can be republished without decoding their
records. Partial builds retain the prior closure, and missing/corrupt demanded
products must preserve existing explicit behavior. Migrate every writer/reader
and inspector together; no old-format fallback. Add a minimal cross-process work
test before implementation and expose actual unloaded outcome-segment reuse.

One complete normalized comparison: 15 alternating pairs each of real unused and
active body edits; require >=3% median paired CPU gain in at least one, with its
95% interval excluding parity. The other edit, unchanged/comment/wording controls
(31 pairs each), and cold control (eleven pairs) must establish no regression.
Require executable/diagnostic parity, the focused matcher/cascade checks, complete
LLVM parity, and required <=120s uncached before keeping the change. Run affected
integration if performance qualifies. Use one heavy job and the corrected disk
guard; preserve every failed attempt. Before any I056 timing, raise the cold
control from three to eleven pairs: three pairs only screen an expensive case
and give a weak small-sample interval. Full incremental goal remains incomplete.

### I056 result: reject; metadata isolation does not remove real outcome demand

Source 525b8134 / compiler 6ed70847 passes the seven-build retention/activation
reproducer and all 166 matcher, eviction, corruption and classification builds.
The minimal case retains two outcome segments without reading them, across two
edits, then demands the activated body correctly. The baseline research probe
observes all four unchanged outcome segments being read despite three deferred
owners. Its portable regression uses actual load counters; access-time observation
is confined to the external Linux research probe.

The complete six-case normalized matrix rejects the candidate. Fifteen-pair unused
CPU is .533620 -> .545914s, ratio 1.025512 [1.013514, 1.043546]; active-body CPU is
1.497784 -> 1.513486s, ratio 1.009700 [1.001571, 1.026784]. The real application
retains zero outcome segments without loading them: later lookup consumers still
load all outcome buckets. Loaded bytes fall only 9.10 -> 8.80 MB, while the unused
edit's snapshot grows 1.42 -> 3.16 MB and snapshot time 5.89 -> 13.98ms. Separating
availability from demand works, but this workload still demands the outcomes and
pays for the new catalogue. Do not keep this optimization or run a confirmation.

The unchanged control also regresses (1.023582 [1.011120, 1.040095]); comment,
wording and eleven-pair cold controls establish no difference. All matrix
executable/diagnostic comparisons pass. Required, full integration and whole-IR
qualification are not run for this rejected candidate. Preserve the branch,
frozen compiler/data and i056-performance-analysis.json; production C++ remains
I052. The full goal is active and incomplete.

## I055: independently qualify owned-input transfer with normalized images

Started 15 November 2026 at 22:38 UTC; first deadline 23:23 UTC. No new compiler
implementation or artifact is introduced: source b0178244 / compiler bc022690
is compared with correctness baseline dbc70f43. I053's complete failed hard-link
matrix remains failed; its conditional confirmation is not run. I054 independently
establishes executable-file state sensitivity using identical bytes and validates
fresh, independent image preparation on a real-program A/A comparison.

This qualification uses that recorded preparation for both archived compilers.
Predeclare one complete comparison: 63 alternating active-body pairs must show
>=1% median paired CPU gain with a 95% interval excluding parity. Unchanged,
comment and wording controls use 31 pairs; cache-disabled cold uses three.
No control may establish a regression. Preserve this matrix under a separate
protocol/iteration name and make no further confirmation attempt if it fails.
Run each case in its own workspace, release owned input images after independent
staging, and compact inactive build objects before the larger comparison.

If the matrix passes, verify focused/matcher/cascade behavior, complete LLVM and
diagnostic parity, required <=120 seconds uncached, affected integration and
merged checks on the unchanged compiler artifact. The checkout's C++ bytes match
b0178244; its benchmark/tooling changes come from current local master. Overall
incremental compiler goal remains active and incomplete.

### I055 capacity failure: preserve the incomplete first attempt

The first normalized body run terminates at sample 55 with an explicit ENOSPC
cache-write error. Its 113 rows include the failed invocation, and the report
remains incomplete. No acceptance statistic is inferred from it. The compiler
keeps each cache within 512 MiB, but two independent caches, two staged compiler
images and transactional writes exceed the old executable-only space guard.
The temporary workspace is removed by the runner; the original report and log
remain unchanged under i055-performance-body.*.

Before a complete attempt, losslessly archive twelve inactive owned compiler
images, verify decompressed hashes and publish their explicit restoration records.
The normalized runner now requires space for three compiler images plus 2 GiB
for caches and transactional writes. Preserve the complete attempt separately as
i055-complete-performance-*. The compiler artifacts, sample counts, preparation,
acceptance threshold and controls remain unchanged. This repairs a verified
capacity failure, not a failed timing gate; there is no extra confirmation after
the completed matrix. No C++ implementation change is introduced.

### I055 result: reject the ownership candidate under the normalized protocol

The complete independent-copy matrix uses the same frozen b0178244/bc022690
candidate and dbc70f43 baseline. Active-body CPU medians are 1.513573 -> 1.491991s;
63-pair ratio .984640 [.978471, .990208] clears the 1% edit gate. Graph capture
falls 83.115 -> 68.519ms, and the candidate moves 4,219 match-input records without
copying them. All executable/diagnostic comparisons in the complete matrix pass.

The 31-pair unchanged and comment controls establish no difference: CPU ratios
.991954 [.975978, 1.025092] and .999624 [.983419, 1.016740]. Wording establishes a
regression: .034260 -> .034862s, paired ratio 1.021611 [1.007316, 1.035619]. Three
cold pairs also regress: 1.014942 [1.002995, 1.019665]. The full declared gate fails.
Do not merge this candidate, run another confirmation, or claim acceptance from
its successful body case. Required/integration suites are not rerun for this
rejected optimization. The original I053 rejection and first I055 capacity-failed
report remain separate and unchanged.

Evidence: i055-complete-performance-{body,unchanged,comment,wording,cold}.json and
staging receipts; i055-complete-performance-analysis.json. Twelve inactive compiler
images were archived losslessly with verified restore records, freeing ~1.47 GiB.
The full goal remains active. The next selected investigation returns to the
retained-source consumer: metadata queries currently share storage segments with
full matcher outcomes, so proving reuse can load unrelated resolution products.

## I054: calibrate startup across identical executable file copies

Started 15 November 2026 at 22:06 UTC; first deadline 22:51 UTC. I053 is rejected
on its preserved branch: its complete matrix establishes unchanged/comment
regressions, so its conditional confirmation is not run. Master remains I052.
This is an independent measurement audit with no compiler changes and no new
acceptance run of I053.

Compare the byte-identical dbc70f43 executable as two hard links to one inode,
as an original versus a fresh copied file, as two independently copied files,
and with original/copy path slots reversed. Use 61 alternating pairs per mode,
check hashes/inodes/permissions, and capture CPU, elapsed time and minor/major
faults. A copied-file difference >=0.5 ms or >=5% with an interval excluding
parity would be relevant to the repeated ~1 ms fast-control gap. Preserve every
mode and do not flush global caches. Bound owned temporary files and memory use.
If copies do not explain the gap, record that negative result instead of blaming
file state. Overall incremental compiler goal remains active and incomplete.

### I054 result: file-state sensitivity established; independent staging validated

The 61-pair A/A audit uses identical dbc70f43 bytes in every mode. Two paths to
one inode give CPU ratio 1.001012 [.973956, 1.026356]. Original versus a fresh
copy gives .922138 [.901112, .971429]; reversing path slots gives .904535
[.879656, .942005]. Two independently copied files give .983156
[.964844, 1.032276]. This establishes a preparation/file-state effect without
any code change, but not its exact kernel mechanism or the whole cause of I053.

The repository benchmark already copies executables. The preserved runner's
hard-link shortcut did not implement that preparation contract. New comparisons
materialize explicit archived inputs, copy both into independent executable files
with identical preparation, and release owned intermediate inputs before caches
are populated. The benchmark now rejects hard-linked staging and verifies the
staged images it actually executes instead of the original input paths.
Three focused filesystem/protocol tests fail before those fixes and pass after;
the check belongs to the tooling integration group, not the required compiler gate.

A real-program A/A calibration of the normalized protocol completes all 31
unchanged pairs with identical output and independent staged inodes. CPU ratio
1.009493 [.990582, 1.016076] establishes no difference. The source archives and
staged images match the same ELF hash; temporary source materializations are
removed. Preserve all old timing matrices and reject decisions. Any I053
requalification must be a separate declared comparison under this validated
preparation, not an overwrite or relabeling of its failed matrix.

Evidence: i054-startup-inodes.json, i054-staging-red.log,
i054-normalized-aa.json/.staging.json, and normalized_archive_benchmark.py.
No C++ compiler source is changed by this measurement repair.

The measurement repair is merged on master. All three staging tests and the
required-routing policy check pass. All 333 required checks pass uncached in
92.190s (93.124s observed), with source/compiler identities unchanged. The compiler
remains byte-identical to 19f9b48d; no C++ rebuild or library behavior change is
claimed. I053's failed comparisons remain rejected and preserved separately.

## Latest iteration: I052 dependency correctness qualified on master

Source-match dependency facts now have body-layout and resolved-match producer
versions, separate from inferred calls and reads. Native and failed constraint
snapshots share the same collector. A repaired build can consume newer failed-stage
matches without restoring unrelated bodies. Section reads retain direct callee
edges instead of flattening trial ancestors differently on fresh and reused paths.

Source 60d457cd / compiler dbc70f43 passes 30 focused, 159 matcher/classification
and 207 broader compilations. All 333 required checks pass uncached in 96.449s
(98.197s observed); four complete fresh/incremental LLVM and diagnostic comparisons
agree. The exact corrected compiler passes all 461 library/runtime/tooling checks
in 774.665 runner seconds (775.789 observed), with source/compiler hashes unchanged.
The fix is merged to local master; its rebuilt compiler is 19f9b48d. All 333
required checks pass uncached there in 97.466 runner seconds. Thirty focused
compilations pass on that executable. The merged worktree and branch are removed;
another 30 focused compilations pass using the frozen compiler and archived data
after removal, and the replay cleans up its temporary data root.

This necessary correctness repair has measured costs; it is not an optimization
acceptance. The frozen real application comparison gives these CPU medians:

| Case | Production seconds | Corrected seconds | Median paired ratio, 95% interval |
| --- | ---: | ---: | --- |
| Active body edit, 15 pairs | 1.427087 | 1.508617 | 1.059222 [1.049230, 1.075357] |
| Unchanged, 31 pairs | 0.029215 | 0.030222 | 1.048158 [1.029151, 1.071753] |
| Comment, 31 pairs | 0.029526 | 0.030868 | 1.045451 [1.016039, 1.060121] |
| Wording, 31 pairs | 0.034853 | 0.036156 | 1.034086 [1.014315, 1.050711] |
| Cache-disabled cold, 3 pairs | 5.333248 | 5.344466 | 0.993667 [0.963765, 1.032428] |

All executable and diagnostic comparisons pass. Active-edit graph capture grows
46.701 -> 83.259 ms; graph encoding grows 32.065 -> 48.647 ms. Payload loading
grows 62.382 -> 82.212 ms. These nested scopes are not additive with their parent
stages. Both artifacts lower one native function and reuse 367 functions.
The representation currently constructs records even for unversioned producers;
sharing or omitting those non-reusable products is a measured next opportunity.

A separate 61-pair compilation-free version probe establishes a startup component:
4.391 -> 5.253 ms CPU, paired ratio 1.192209 [1.159777, 1.240166], minor faults
867 -> 1004. The ELF dependencies, relocation-section size, writable segments and
initializer-array size are unchanged; text grows 12,096 bytes and later segments
move three pages. The exact loader/kernel/layout cause is not established. This
probe is not an acceptance retry or a waiver of the existing optimization gates.

The full goal remains active. Semantic section construction, broad resolution
setup, finer source/configuration dependencies, persistence, tooling and other
output paths still need dependency-driven reuse. Evidence: i052-cost-analysis.json,
i052-complete-{focused,contracts,regressions,ir-parity}/report.json,
i052-complete-observed-required.json and i052-startup-profile.json.

## Previous experiment: I050 execution products remain isolated

Parsed ordinary execution regions now retain topology and lazy syntax products.
The real active edit leaves 2,642 detailed source products unallocated. A verified
source-complete retention defect is fixed, including repeated inactive/active
edits and removed-owner pruning. Corrected source f2c4a0c0 passes all 333 required
checks in 105.195 seconds and all 461 integration checks in
690.694 seconds. Four complete LLVM/diagnostic comparisons agree.

Both measured performance variants fail their original gates; the lifecycle
correction has no new acceptance benchmark. No compiler optimization is merged.
Production remains I040; the full incremental goal is incomplete. The next
measured target is selective graph persistence, currently encoding unchanged
entries, alongside a separately scoped startup-cost investigation.

## I040 checkpoint: unread-flex inference verified on master

Started 15 November at 10:02:10 UTC, first deadline 10:47:10. Production
source remains byte-identical to 7b75ab49 and frozen compiler 2e567cdc after
merging the independently verified I039 runner. No new compiler optimization
or timing retry is part of this qualification. The fixed artifact's 31-pair
unread-flex result is a 50.02% median paired CPU reduction; all eight controls
establish no regression, and four complete LLVM/diagnostic comparisons agree.
The 16 focused and 207 broader regression builds remain valid for that artifact.

With the final runner, all 333 required checks pass uncached within 120 seconds,
and all 461 integration checks pass in 806.603 seconds (808.098 observed), with
source/compiler identities unchanged. I038's two failed required attempts and
I039's initial worker-status failure remain preserved. Local master 5773d151 /
compiler be68739a rebuilds in 143.699 seconds with one job, peak RSS 2,284,356 KiB.
All 333 required checks pass uncached in 102.778 seconds (103.758 observed);
source/compiler hashes remain unchanged. The snapshot is pushed. Its merged
worktree and three owned branches are removed; all 16 focused builds pass after
removal using the frozen compiler and archived data, and the temporary data root
is removed afterward. Derived build objects are retained for the next iteration.

Completed historical artifacts caused the disk-capacity failure. Thirty-seven
inactive compiler executables are now losslessly compressed, recovering
4,943,092,979 bytes. Every archive was decompressed and hashed before removing
its original. An explicit restore tool recreates the exact paths expected by
preserved benchmark manifests; one historical compiler was restored, verified
against its original hash and executed. Current artifacts, source, reports and
failure evidence remain available. The archive and restore manifest is
`compiler-artifact-archive.json`; replay preparation is
`python3 restore_compiler_artifacts.py --manifest COMPILER_DATA_MANIFEST.json -- ORIGINAL_REPLAY_COMMAND`.

The full compiler goal remains incomplete; the next-opportunity audit is
i040-next-opportunity-audit.json, an unimplemented source/resolution-product
hypothesis.

## I038 unread flex inference dependencies: parked at closure, qualified in I040

Started 15 November at 08:33:57 UTC; first deadline 09:18:57. The single
evidence-backed extension ends at 10:03:57. Published baseline remains I037.
An unread flex body must not invalidate executable inference merely because it
can expand at compile time. Actual inference reads, constraint probes, callable
queries and the dependency closure of retained IR must still protect consumers.
Generated bodies absent from the logical source stream cannot mask source input.

Candidate `7b75ab49` / compiler `2e567cdc` passed 16 focused and 207 broader
incremental-versus-clean builds. Qualification was incomplete at I038 closure. The first candidate
halved the target's CPU cost but established a 2.9% ordinary-edit regression;
those measurements do not qualify the corrected candidate. Its first 31-pair
ordinary control is inconclusive. The independent 63-pair confirmation stopped
after 101 rows on an explicit disk-full cache-write error. Preserve that failed
report. After losslessly archiving completed historical cache states, the same
artifact and workload complete all 63 pairs: CPU ratio 1.006978, 95% interval
[0.991847, 1.011317]. No ordinary-edit change is established. The incomplete
disk-full attempt supplies no timing result.

The corrected target completes 31 alternating pairs on the frozen real LEXI8
application. CPU medians are 1.274596 to 0.631553 seconds; median paired ratio
0.499821, 95% interval [0.490039, 0.508746]. Elapsed medians are 1.303100 to
0.645313 seconds; paired ratio 0.498825 [0.480363, 0.515351]. Inference processing
falls from 255.768 ms to effectively zero, and code generation processing from
324.413 ms to zero. The baseline already performs zero function-body inference
passes and builds zero objects: the saved work includes validation/restoration
of cached inference products, their dependency snapshot and IR processing.
Candidate inference definitions indexed are zero instead of 2,145. Both variants
skip linking. Source/pattern frontend work still runs, about 348 ms in the
candidate; this does not establish complete frontend incrementality.

The predeclared target gate (at least 10% median paired CPU gain, 95% interval
excluding parity) passes. All eight common/cold/fast controls establish no
regression, and four full LLVM/diagnostic comparisons agree with clean builds.
Two complete required attempts hit the unchanged 120-second limit. The full
incremental driver independently passes in 107.845 seconds with profiling, but
the second required attempt reaches only the subsequent pattern-precedence
check before timing out. The runner serializes the long incremental auxiliary
check with the other independent auxiliary checks. Concurrent external web
tests are observed; their exact contribution to elapsed time is unproven.
Preserve both failures and park I038 without merging it. Integration has not run.

I039 is a separate test-runner goal: overlap that independent long check with
the other auxiliary checks, preserving every assertion, result, failure, fixture
cache policy and the 120-second post-build budget. Prove worker concurrency and
failure propagation before running the complete uncached required suite. This
change must qualify independently on master before I038 resumes qualification;
it is not another extension of the compiler optimization.
Complete semantic section products and tooling/other output modes remain
separate unfinished parts of the full goal.


## Previously published I037 declaration retention

I037 removes native-body eligibility from resolved declaration retention. A flex
body can change without changing its declaration; after checking the changed
signature and the existing source/syntax/matcher/scope/binding proofs, unrelated
declaration fingerprints now stay unexamined. Native-body reuse eligibility is
unchanged, and body-dependent expansion, inference and IR retain their own inputs.

The import-free regression reduces four declaration audits to one, observes the
changed active flex output, and still detects removal of the last implicit
parameter use. On the frozen real LEXI8 application, an imported unused key-constant
edit reduces 1,750 declaration audits to one, about 13 ms to 0.014 ms of input work.
The first 31 pairs are inconclusive. The single independent 63-pair confirmation
meets the unchanged 1% acceptance gate: CPU medians 1.306061 to 1.282953 seconds,
paired ratio 0.981015, 95% interval [0.968829, 0.993757]. Wall ratio is 0.984064,
interval [0.966413, 0.997543]. Do not substitute ratios of unrelated medians for
these paired results. Eight common/cold/fast controls establish no regression;
cold-build improvement remains unproven.

Candidate bbc66c69 / executable ebd0d7f6 passes 137 broader regression builds,
four complete normalized LLVM/diagnostic comparisons, all 333 required checks
uncached in 112.841 seconds, and all 461 integration checks in 794.781 seconds.
An initial controls attempt stopped before shared-type compilation because the
wrapper omitted that allowed case. Its failed evidence is retained; after fixing
the wrapper, completed measurements were preserved and remaining cases resumed.
Merged master 6ba8ae03 / executable 0ca41928 passes all 333 required checks
uncached in 107.152 seconds (108.107 seconds observed), with source/compiler
identities unchanged. The merged worktree and branch are removed; the frozen
compiler passes five focused declaration/cascade builds after removal, using its
verified archived library/runtime data and cleaning the temporary data root.
Derived build objects are retained separately. This closes I037 within its one
recorded extension; it does not complete the full compiler goal.

The full goal remains incomplete. The next measured gap is inference after an
unread flex edit: about 260 ms still runs although 368 emitted functions reuse IR
and no objects are built. Audit actual inference reads before replacing the blanket
flex exclusion from inactive-body eligibility. The minimal read graph correctly
records root -> report -> active label; type providers, rejected trials, callbacks,
compile-time-only callees and generated property owners need the same proof.
Complete semantic section products and tooling/other output modes also remain open.

### I036 required unit coverage published

I036 moves the focused numeric-literal and class-layout C++ checks into required.
The routing regression fails in both directions before the fix and passes afterward;
the two checks no longer run only under integration. The compiler rebuild is
byte-identical to the accepted corrected baseline (926a3eeb). After removing both
test executables and their four dedicated objects, all 333 required checks pass in
112.015 seconds (113.319 seconds observed), including their compilation. Required
DynLex fixtures still disable incremental caching and the 120-second limit is
unchanged. Merged master 42665c68 / executable a900532f also passes all 333 checks
in 102.379 seconds (103.665 seconds observed). The source tree and compiler remain
unchanged during both runs. The merged worktree and branch are removed; validated
build objects are retained separately. This fixes two known misclassifications;
the broader compiler-versus-library test boundary still needs auditing.

### I033 correctness repair published

I033 fixes a pre-existing incremental diagnostic-order mismatch. A four-line
provider-removal edit mixed a live edited body with an earlier reactivated cached
body; error emission exposed scheduler-queue order instead of source order.
Unresolved definitions, body references (including waiting references), and global
references now each report in source order within their existing phases.
The correctness-only candidate 6d9b70d9 / executable 926a3eeb passes the new order
and repair regression, 132 broader regression builds, three complete LLVM/diagnostic
comparisons, 331 required checks uncached in 106.938 seconds, and all 463 integration
checks in 800.626 seconds. Merged master faf35f5a / executable a900532f passes
all 331 required checks uncached in 104.703 seconds (105.931 seconds observed).
Compiler and source identities remained unchanged during verification. The merged
worktree and branch are removed; its derived build objects are retained separately.
The frozen compiler also passes five order/repair builds after worktree removal,
using the verified archived source/runtime data. No speedup is claimed.

I032's lookup optimization remains unqualified and unmerged on its preserved
branch. Its original 31-pair CPU result improves 2.49%, but broader qualification
uncovered this ordering defect, also reproduced on the published baseline.
The combined fixed artifact has not passed the complete final qualification matrix.
Do not describe the parked optimization as published or accepted.

I034 independently requalified the parked lookup change against the corrected
baseline on identical frozen application and compiler-data inputs. It fails the
predeclared 2% gate: 31 pairs give CPU ratio 0.990556, 95% interval
[0.977194, 1.003183], and wall ratio 1.003687, interval [0.988755, 1.035332].
CPU medians are 0.586047 and 0.585912 seconds. All executable/diagnostic comparisons
pass, but the improvement is unproven. Reject this optimization for publication;
preserve the isolated branch and evidence without running unnecessary full suites.
I035 tested P03 ordinary declaration/custom header descriptions independently.
The prototype reuses all 1,283 eligible descriptions after a body edit, including
unchanged headers within the edited file. Fifteen focused builds cover header
renaming, configuration-only invalidation, diagnostics and recovery with fresh
parity. However, header factory work increases from 2.362516 to 3.628028 ms.
Across 31 pairs, CPU ratio is 1.004471, interval [0.993178, 1.007916]; wall ratio
is 1.004605, interval [0.971648, 1.015455]. The predeclared 0.25% gain excluding
parity is not established. Reject this separate cache, preserving the prototype
and measured evidence without adding it to master or running full suites for it.
P03 needs larger semantic section products shared with their consumers; caching
keyword parsing alone adds bookkeeping without a demonstrated compilation gain.

Next: investigate semantic owner products shared with resolution, rather than
adding another header-only cache. In the I034 real-program baseline, median
resolution is 301.919 ms of 542.060 ms total, while section/header preparation is
a small fraction. An owner product must carry its source/scope layout, declaration
metadata and stable input identity into its consumers, so they can reuse that
result without rebuilding it or independently fingerprinting the same body.
The minimal regression must change a body in the same physical file, prove other
owners skip section/declaration preparation, and cover last-parameter removal,
new/removed overloads, syntax changes and diagnostic recovery with clean parity.
Measure the combined producer/consumer cost and persistence before accepting it.
The current required gate must not be described as all compiler regression coverage.

### Previously published I030

The I029 source-owner candidate is qualified and merged into local master in
I030. I029 itself closed incomplete at its 04:22:54 UTC extended deadline.
Its performance and LLVM checks passed, but the required retry had not run.
The original failed gate is retained; acceptance is not backdated.

On unchanged eligible ordinary function owners, concrete reference and variable
outlines now stay unallocated until a semantic consumer demands them. The real
unused-edit case retains 342 owners and 5,419 references: created references fall
from 6,308 to 889. Across 31 pairs against I028, median paired CPU improves 2.54%
(ratio 0.974586, 95% interval [0.967525, 0.987269]). Active-body edits improve
1.89% across seven pairs; game-data, cold and fast controls are consistent with parity.
These are paired comparisons, not comparisons of unrelated historical medians.

Candidate 8d4c1a5b / executable 0e3135e3 passes 13 focused rows, 127 broader
regression rows, precedence replay and three full normalized LLVM/diagnostic
comparisons. In I030 all 331 required checks pass uncached in 108.322 seconds,
the relevant native-cache integration passes, and both C++ ownership/provenance
executables rebuild and pass. Merged source 1d40cf7f / executable dcf46435 passes
all 331 required checks uncached in 113.238 seconds and all 463 integration
checks in 801.393 seconds, with unchanged compiler/source identities. The merged
worktree/branch are removed and actual six-build archived replay passes afterward.
An expanded 63-pair comment control has CPU ratio 1.01814 [0.99128, 1.03116]; it
establishes neither a regression nor an improvement. The full goal remains
incomplete: section objects, broad classification/matching/binding and other
output modes still require finer dependency-driven reuse.

### Previously published I028

I028 eliminates obsolete projection-field copies without changing cache formats
or invalidation. On the frozen LEXI8 program, 31 unused-edit pairs improve median
paired CPU time by 4.3%; 31 active-body pairs are consistent with parity. Common,
cold and fast controls show no established regression. All 331 required checks
pass uncached in 105.689 seconds, relevant native-cache integration passes, and
three complete normalized LLVM/diagnostic comparisons agree with fresh builds.
Local master is rebuilt and verified: all 331 required checks pass uncached in
105.088 seconds (106.041 seconds observed), with unchanged source and compiler
hashes. The published executable is 27d4d98e; source changes are merged.

I027 is unaccepted: source-owner retention skips most reference allocations,
but the 31-pair unused-edit CPU comparison failed its >=2% improvement gate.
Its correctness checks pass; its compiler code remains isolated and unmerged.
I028 is based independently on qualified I026; it does not merge the rejected
source-owner code or waive that experiment's failed performance gate.

I026 fixes an independently reproduced compiler assertion for a called function
constructing a class with one missing lifecycle section. Pair validation now
runs at class finalization; the obsolete inference-time full-tree scan is gone.
Its 331 required checks pass uncached in 104.677 seconds. All 463 intended
integration checks pass across the full run and a targeted retry; the original
run had two infrastructure/cleanup failures, documented below. I025 source-owner
deferral is parked unqualified on its isolated branch, not published. The full
incremental goal remains incomplete.

I024 is merged and qualified: named global readers now retain their initializer
execution, fixing cached 42 versus clean 43 after a provider edit. All 329
required checks pass uncached on master in 111.772 seconds; all 464 integration
checks pass on the identical candidate sources. Three real-program edits match
complete normalized LLVM text and diagnostics. Seven paired controls show no
established active-edit or cold-build regression; this is a correctness repair,
not a speedup claim. The merged worktree/branch are removed, and an actual
six-build archived replay passes after removal. The full goal remains incomplete.

I022 does not change the published compiler: its sorted matcher-directory
experiment failed the independent 31-pair CPU gate and remains unmerged.

Native builds reuse source preparation, eligible body syntax/resolution, resolved
signature fingerprints, typed products and native artifacts in the verified edit
cases. I021 removes an unconsumed full declaration table; it does not complete the
compiler-wide goal. Semantic section construction and broad matching/binding,
validation and persistence work still need finer reuse. Debug/trace builds disable
major reuse paths; LSP and other output modes still need the shared frontend.
Next: requalify the isolated P03 source-owner experiment on the accepted metadata
change. Retain its allocation and activation/classification/lookup proofs, then
make a new independent performance decision against the newly published baseline.

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

| I024 | Named global initializer producer execution dependencies | Correctness fix merged and qualified | Red O0 cached 42/fresh 43; O0/O2 provider, argument, rename, repeat and revert cases pass. Required: 329 uncached in 111.734 s; integration: 464 in 845.712 s. Complete LLVM parity for three real-program edits; seven paired samples each for unused/body/game-data/cold controls. Full goal remains incomplete. |

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


I024 candidate qualification: source `1534c226`, compiler `27ed6070...`, passes
all focused and broader dependency/diagnostic regressions, 329 required checks
uncached in 111.734 s (112.669 s observer), and all 464 integration checks in
845.712 s (846.808 s observer), with source/executable unchanged. Three real
LEXI8 edits agree with fresh builds in complete LLVM text after removing only
the inspector-input ModuleID comment. Completed caches were pruned; source,
LLVM output and reports remain. Unchanged repeated initializer input still skips
frontend work, object generation and linking. The optional scope API now
separates source-only reads from entry/function execution through pure forwarding.
The related C++23 ownership API correction uses `use_count() > 1` in place of the
removed `shared_ptr::unique()` operation.

Seven alternating pairs per case compare frozen baseline `d7ca3497...` with
candidate `27ed6070...` on the same LEXI8 revision `d913a7db`:

| Case | Baseline wall median (s) | Candidate wall median (s) | Baseline CPU median (s) | Candidate CPU median (s) |
| --- | ---: | ---: | ---: | ---: |
| unused | 0.644041 | 0.625829 | 0.638440 | 0.623093 |
| body | 1.550299 | 1.547546 | 1.522657 | 1.535157 |
| lives | 1.938324 | 1.946674 | 1.932282 | 1.941752 |
| cold | 5.491110 | 5.487169 | 5.487950 | 5.483971 |

All 68 comparison rows complete with executable and diagnostic parity. This is
a necessary correctness repair, not a claimed performance optimization. Paired
CPU ratio intervals for active-body, game-data and cold controls include 1;
raw samples and intervals are retained in `i024-cost-*.json`. The native build
used one nice-10 job: 760.240 CPU seconds and 941.475 wall seconds, including
175 seconds with our process tree suspended for a peer website compilation.
That delay is not DynLex compilation time. Frozen executables and the 105-file
compiler data bundle permit replay after worktree removal. Root qualification
and publication are the remaining steps of this bounded iteration; its one
extension still ends at 21:52 UTC. P03's coupled consumer audit now records the
classification/matcher/header/promotion identities that must avoid concrete
reference reconstruction; no parser-only allocation shortcut is accepted.

Evidence: `i024-{build,required,integration}.json`, focused/regression logs,
`i024-ir-parity/report.json`, `i024-cost-summary.json`, individual raw comparison
reports, and `i024-native-object-cleanup.json`. The full compiler-wide goal
remains active and incomplete.


I024 root qualification and cleanup finish on 14 November at 21:55 UTC. Merge
`0dfe6d82`, compiler `1d743883...`, passes all 329 required checks uncached in
111.772 s (112.736 s observer), with source/executable unchanged. Source, tests
and library inputs match the qualified candidate exactly. The one-job master
C++ rebuild takes 640.910 wall / 628.453 CPU seconds, peak 2,271,836 KiB. A frozen
root executable is added to the replay manifest. The merged worktree and branch
are removed; an actual six-build archived replay compares development and root
compilers afterward, checks identical executables/diagnostics and unchanged
compiler hashes, then removes the reconstructed 105-file data root. Its single
pair establishes replay, not a performance estimate.

The 21:52 timebox was exceeded during the final master build, required gate and
archive/publication leg. Candidate implementation qualification and cost controls
finished at 21:41; no implementation was added after that decision. This is an
overrun, not a third optimization extension or an in-time completion claim.
The full compiler-wide goal stays active and incomplete. Next is the coupled
P03 owner/reference consumer boundary, not another isolated header/allocation
cache. Evidence: `i024-root-{build,required}.json`, their logs,
`i024-archived-replay.{json,log}`, and `p03-source-owner-boundary-audit.json`.

## I026: malformed lifecycle-pair source diagnostics

Started 2026-11-14T23:34:34.308962+00:00 from published master `3a78aa02`; 45-minute
iteration including qualification, one extension available only after concrete
progress. I025 source-owner work is parked on `codex/incremental-source-owners`
at `ecae0ad1`; its executable/evidence remain in the structured evidence folder.
It is unqualified (330 required passes, one precedence reuse assertion failure
in 103.962 seconds), with no paired performance claim and no master changes.

Independent correctness goal: a class declaring only retain or only release
must report a configured source diagnostic at the present lifecycle header,
before managed-type consumers enforce their internal complete-pair invariant.
Both minimal no-import required fixtures abort on the published compiler.
Acceptance: both emit the expected single diagnostic, complete paired classes
retain their behavior, required checks pass uncached within 120 seconds, and
the relevant lifecycle/incremental integration checks pass. No speedup claim.
The compiler-wide incremental objective remains active and incomplete.

The suite split additionally needs an audit: integration currently contains
compiler-only regressions with standard-library imports. Required-suite timing
alone does not prove all focused compiler coverage meets the budget.

I026 root-cause correction: unused malformed classes already report the existing
diagnostic during inference. Construction reaches the managed-type invariant
before that late check and aborts. Both required regressions now construct the
class and reproduce SIGABRT on published master. Pair validation is moved from
the late whole-tree inference walk into class finalization; the old walk is
removed. The initial unused-fixture baseline evidence is retained separately.

I026 migrates the old library-importing `integration/lifecycle_missing_release`
fixture to the minimal required reproduction, adding the symmetric missing-retain
case. Incremental tests cover cached diagnostics, adding the missing section,
removing it again, unused declarations, and clean-build parity. The obsolete
late whole-tree pairing scan and duplicate integration fixture are removed.

### I026 qualification and publication

Candidate source `d5e6eae3`, executable
`5c60b49f1a95c631952c527840e12b4fc41e25f8919b7ca8792c90e008b9a053`:
46 targeted lifecycle builds pass; required **331/0/0 in 104.677 s**.
The unchanged-source full integration run reports **462/2/0 in 796.128 s**.
One failure was an untracked empty directory left after moving the library-based
missing-release fixture to required; removing it leaves 406 valid integration
fixture directories. The other was ENOSPC while linking the ownership auxiliary.
After removing a passed disposable test executable and compressing the parked
I025 executable, the exact ownership command passes (242 reachable clones,
peak 256). No compiler or test source changed between these runs. Thus all
463 intended checks passed across runs; a single green full run is not claimed.
Raw failures and retry evidence remain intact in `i026-integration*.json/log`
and `i026-ownership-retry.log`.

The seven-line crash fixtures are minimal in the relevant behavior: replacing
the called function with direct top-level construction reports the old source
diagnostic instead of reproducing the assertion. Both actual regressions abort
on published master and emit one diagnostic on this candidate. Cache tests also
cover section addition/removal, unused declarations and clean-build parity.
This is a correctness repair with no measured performance claim.

Next selected goal, I027/P03: return to the parked source-owner boundary after
this independent correctness fix. Complete global precedence-signature reuse
when concrete reference keys are absent, then qualify the combined owner path
against immutable real-program builds. Preserve the original zero-allocation,
activation/classification/lookup parity, >=2% paired unused-edit CPU, common/cold
regression, complete LLVM/diagnostic parity and required-suite gates. The next
iteration gets a fresh recorded start/deadline; this is not another I025 extension.

The saved real-program unused-edit evidence identifies the remaining cost:
median total 547.896 ms, resolution 286.853 ms, parsing 15.709 ms, outline
restoration 5.235 ms. It still evaluates 2,360 section headers and dispatches
4,174 body lines; classification reuses 1,747 inputs but handles 12,015 edges.
Outline skipping alone cannot establish compiler-wide incrementality. Detailed
counters and timing-scope caveats are in `next-frontend-cost-evidence.json`.

## I027: qualify retained source-owner domains

Started 2026-11-15T00:24:24.541994+00:00 from published `f3651078`, compiler
`1b4056412848c1159440be3222419af63147f214d8ec8f7a79c95f5bbf0d745e`; first deadline
2026-11-15T01:09:24.541994+00:00, with one progress-based extension available.
The previous turn completed I026; the compiler-wide objective remains active.

Restore I025's source-owner changes without its now-published lifecycle fix.
Unchanged eligible ordinary owners must keep source outlines, classifications
and initial/final match products encoded until actual semantic demand or a
relevant invalidation. The existing precedence replay regression stays unchanged:
its saved global fingerprint must remain reusable even when a retained body
creates no concrete reference replay keys. Preserve the current tree/rule proof,
including rejection after a precedence rule changes.

Acceptance remains zero unused-owner outline allocation in the minimal case,
activation/classification/positive+failed lookup cascades, complete normalized
LLVM and diagnostic parity, >=2% paired real-program unused-edit CPU gain with
confidence excluding no improvement, no established active/value/cold regression,
and required checks uncached within 120 seconds plus relevant integration.
Use at least seven ordinary paired samples; investigate uncertain results with
more samples. No graphical application is launched. Section construction and
remaining owner classes/tooling/output modes still belong to the full goal.

Peer website Node PID 611122 is live at the initial resource audit. Source
preparation and evidence freezing can proceed; heavy compiler/benchmark/test
work waits for its actual terminal state and a fresh process inventory.
Evidence: `i027-start-state.json`, `i027-source-owner-port.patch`; the immutable
baseline executable is `i027-compilers/bin/baseline` in the structured folder.


## I027: source-owner retention rejected by the measured acceptance gate

The candidate remains isolated on `codex/source-owner-qualification`, source
`43337883` (compiler built from `134c941a`; the later commit only migrates three
ownership-test implementations). Executable SHA-256:
`fbef84fe75ea718de4923b4ab4cc46e1cf9c5fe2eaeb4def9161adedcc394446`.
Baseline is published compiler `1b4056412848c1159440be3222419af63147f214d8ec8f7a79c95f5bbf0d745e`.
No compiler code from this experiment is merged. The complete incremental goal
remains active and incomplete.

The experiment retains source outlines/references through classification and
initial/final matching until demand. It also fixes the global-precedence reuse
proof when no concrete reference replay keys exist. All 198 focused builds pass,
including activation, parameter retraction, failed lookup and lifecycle cascades.
The C++ ownership products are fully migrated to the changed interface. Three
real-program edits match complete normalized fresh/incremental LLVM text and
diagnostics. Required **331/0/0 in 106.816 seconds**; integration **463/0/0 in
796.920 seconds**, on unchanged compiler bytes.

On frozen LEXI8 Breakout, the unused edit creates 889 pattern references instead
of 6,308, and restores 1,892 outline nodes instead of 12,007. This is actual work
avoided, but does not by itself meet the performance criterion. The initial
seven pairs suggested a 3.2% median paired CPU gain. The subsequent 31-pair
comparison did not confirm it: ratio **1.0115**, interval **0.9875–1.0199**.
The predeclared >=2% unused-edit CPU improvement with confidence excluding parity
therefore failed. Do not publish the favorable seven-pair estimate as the result.

All cases below use identical frozen application sources, paired alternating
execution, matching diagnostics/executable artifacts, and no GUI launch. Times
are median child CPU seconds. The ratio is the median of per-pair candidate /
baseline ratios; it differs from the ratio of the two medians.

| Case | Pairs | Baseline CPU s | Candidate CPU s | Paired ratio | 95% interval |
| --- | ---: | ---: | ---: | ---: | --- |
| unused | 31 | 0.617899 | 0.618058 | 1.0115 | 0.9875–1.0199 |
| body | 7 | 1.518385 | 1.504107 | 0.9895 | 0.9746–0.9987 |
| lives | 7 | 1.932733 | 1.878451 | 0.9933 | 0.9531–0.9937 |
| cold | 7 | 5.513669 | 5.522594 | 0.9998 | 0.9897–1.0150 |
| unchanged | 15 | 0.031879 | 0.031478 | 1.0285 | 0.9438–1.0491 |
| comment | 15 | 0.031826 | 0.031503 | 0.9843 | 0.9488–1.0871 |
| wording | 15 | 0.035817 | 0.037346 | 1.0203 | 0.9781–1.0661 |

The larger comparison shows where the saving was consumed: median parsing
16.092→11.100 ms and resolution 296.677→293.746 ms, but matcher projection
43.554→49.666 ms and cache writing 28.243→31.173 ms. Total reported time is
553.431→557.793 ms. These are nested wall timers; do not sum them. Earlier owner
preparation also moves metadata loading ahead of the classification timer, so
the lower classification/load figures are not equivalent end-to-end savings.

The first required run timed out at 120 seconds. It is preserved, not counted as
a pass. Standalone per-check profiles and paired small-program diagnostics
followed. The paired small-program child-CPU ratio was 0.9978, with interval
0.9832–1.0228. Its parent wall timing was invalidated by a diagnostic writer
repeatedly serializing a growing report; the harness finished and is not reused
for acceptance. A later process sample traced >3 cores of CPU use to the peer's
automated software-rendered browser test. After coordination, the unchanged
required gate passed at 106.816 s. The original timeout had pressure evidence
but no per-process attribution; do not assign those later PIDs retroactively.

Started 00:24:24 UTC on 15 November; first deadline 01:09:24. One allowed
progress-based extension ends 01:54:24. Its recording was 26 seconds late after
a diagnostic-report read failed during a write; this overrun is recorded in
external state. No further extension is taken.

Raw evidence: `.cache/incremental-evidence/structured-20261113/` contains
`i027-start-state.json`, `i027-cost-summary.json`, individual paired reports,
`i027-projection-consumer-audit.json`, both required reports, standalone profiles,
`i027-ir-parity/report.json`, and `i027-integration.json/log`. Archived compiler
paths and hashes remain reproducible; duplicate archived baseline bytes were
hard-linked only after verifying equality, reclaiming 784 MiB.

### Next independent goal: I028 / P14 owner projection metadata

Start from current local master, not the unaccepted source-owner branch. Scope
and measure projection selection, source views, numeric input contexts, header
products and reference-table capture separately. Current code reconstructs or
copies broad JSON products and repacks reference tables even when many owners
are unchanged. Use the existing content-addressed matcher store to retain
unchanged products; preserve positive/negative lookup inputs, numeric spellings,
source visibility, and initial/final matching boundaries. Do not introduce a
general storage engine or accept work merely because timers/counters moved.

Record a new start/deadline and immutable baseline before changing code. Choose
an implementation only after measured avoidable capture/persistence work supports
a >=3% unused-edit CPU improvement. Acceptance requires 31 paired primary
samples with confidence excluding parity, no established common/cold regression,
complete fresh LLVM/diagnostic parity, the uncached <=120-second required gate
and relevant integration checks. A rejected hypothesis stays isolated. Retest
the source-owner candidate only after an independently accepted metadata change
provides a reason; its present failed gate is not waived.

### I028 / P14: qualified obsolete-projection copy elimination

Started 15 November at 02:00 UTC, first deadline 02:45. The permitted single
extension was recorded at 02:39 after a concrete implementation, 31-pair primary
gain and 31-pair common-edit confirmation; it covers qualification/publication,
not another optimization. Baseline source d8df9914 / executable 1b405641;
candidate source 276fbf06 / executable fb80c834. Frozen application is d913a7db.

Scoped probes rule out header capture (~0.02 ms) and reference-table packing
(~1.3 ms) as substantial warm-edit targets. Initial selection/source views each
cost ~6–7 ms. A previous function projection copy costs ~8 ms and the next program
snapshot copies it again for ~11 ms, before replacing its fields. An actual
snapshot microprobe measures copy/discard at 18.240 ms versus 0.526 ms retaining
other fields. The implementation omits only explicitly replaced stage products;
all current products are still published and inferred stage data remain intact.
The primary candidate copies 42 top-level fields and skips six obsolete copies.
This is metadata-copy work, not a claim that six compiler functions were skipped.

Paired CPU results (medians, seconds; ratios are medians of per-pair ratios):

| Case | Pairs | Baseline | Candidate | Paired ratio | 95% interval |
| --- | ---: | ---: | ---: | ---: | --- |
| Unused function body | 31 | 0.584749 | 0.562692 | 0.956981 | 0.948728–0.971688 |
| Active body | 31 | 1.436991 | 1.436959 | 0.994673 | 0.989732–1.004880 |
| Gameplay state | 7 | 1.817870 | 1.798597 | 0.991300 | 0.979361–1.014668 |
| Cache-disabled build | 7 | 5.302093 | 5.340269 | 1.008253 | 0.994081–1.028553 |
| Unchanged | 15 | 0.028904 | 0.028681 | 0.988410 | 0.972927–1.006248 |
| Comment | 15 | 0.029780 | 0.029485 | 0.993527 | 0.961850–1.006859 |
| Config wording | 15 | 0.037387 | 0.035951 | 0.960653 | 0.899995–1.066496 |

An initial seven-pair active-body estimate suggested a slowdown; the complete
31-pair confirmation is consistent with parity. Its first attempt failed on
baseline sample 22 with ENOSPC and is retained as incomplete, excluded from
acceptance. Verified compression of owned parked build objects and removal of
a byte-identical parked executable reclaimed 395 MiB without removing source or
historical compiler evidence; the successful retry started with 841 MiB free.

Qualification: ten focused stage-retention builds, 331 required checks uncached
in 105.689 seconds, and all five relevant native-cache fixtures pass. Three real
edits match complete LLVM text and diagnostics after removing only the inspector's
input-path ModuleID comment. No full 463-check integration run is claimed here.
Evidence under `.cache/incremental-evidence/structured-20261113`: `i028-cost-summary.json`,
`i028-{projection-probe,projection-detail,focused,ir-parity}/report.json`,
`i028-observed-{required,relevant-integration}.json`, `i028-copy-cost.json`, and
`i028-parked-build-compaction.json`. Local-master publication is recorded below.
The full compiler-wide incremental goal is active and incomplete.


I028 local-master verification: source 00243667, executable 27d4d98e, all 331
required checks uncached in 105.088 seconds (106.041 seconds observed). Source and
executable hashes remain unchanged across that run. The executable is archived
read-only as `i028-compilers/bin/published`; root build/required reports are
`i028-root-{build,required}.json`. No complete-incremental claim is made.

Next selected goal, I029/P03: requalify the isolated source-owner retention code
against I028. Restore its verified compressed native objects, merge local master
into that worktree, record a new start/deadline and immutable baseline, and use
new scoped probes to account for numeric/source-context and persistence costs.
Require a fresh 31-pair gain with confidence excluding parity, actual zero-allocation
work counters, full activation/classification/lookup and LLVM parity, the uncached
120-second required gate and relevant integration. Retain the existing >=2%
source-owner performance threshold. If it still fails, park it and select the
next independent measured hotspot; do not waive the gate. The integration-suite
classification audit and shared frontend/debug/tooling coverage remain open.

I028 is published on master. Its merged worktree/branch are removed. A six-build
archived baseline/candidate replay passes after removal, with identical executable
output and unchanged compiler/data hashes; the temporary data-root restoration
cleans up afterward. Evidence: `i028-archived-replay.json/log`.

## I029: requalify source-owner retention against I028

Started 15 November at 02:52:54 UTC, first deadline 03:37:54. Baseline is published
master 0fd33835, executable 27d4d98e. The source-owner branch merges local master
at d80fa557; I027's failed performance decision is preserved. New scoped probes
separate owner numeric/source-context capture and early source-owner selection.
Their times overlap declaration capture/resolution respectively and are not
additional stage totals. No new performance result is claimed before rebuilding.

Accept only a new >=2% median paired unused-edit CPU gain over 31 pairs, confidence
excluding parity, no established common/cold regression, actual allocation-skipping
counters, activation/classification/lookup and full LLVM/diagnostic parity, the
uncached required gate <=120 seconds, and relevant integration. Otherwise park
again and select an independent measured hotspot.

The UI thread has priority for the heavy-job slot released after I028. Its native
build and replacement ownership-test build were confirmed live during preparation;
this candidate's C++ build remains queued. Archived objects have verified bytes.
Restoring their floating-point timestamps initially made Ninja schedule 42 needless
recompiles; exact recorded timestamps from its existing dependency database restore
the two-step build (incrementalCompilation.cpp and link), while changed inputs
still invalidate the object. Evidence: `i029-object-timestamp-restoration.json`.
An independent classification-interface/topology opportunity is audited, not
implemented or claimed as a speedup, in `i029-classification-opportunity-audit.json`.

## I029 outcome and I030 publication

I029 used its single extension and closed incomplete at the 04:22:54 UTC deadline
on 15 November. Its independent primary comparison met the >=2% CPU gate;
common/cold controls, focused and broader regressions, and complete LLVM parity
passed. Required initially reported 330 passed / 1 failed because restoring the
live compiler with `copy2` preserved the frozen archive's read-only mode. The
compiler-identity test legitimately modifies its own metadata-preserving copy.
This was an artifact-restoration error, not a compiler or test expectation change.
Retain `i029-observed-required.json` and its failure log.

I030 started 04:24:11 UTC, first deadline 05:09:11 UTC. Its sole goal is completing
the unchanged candidate's qualification and publication: restore normal mutable
executable permissions, rerun required, run relevant integration and the C++
ownership checks, merge into local master, rebuild serially, verify root required,
push, dispose the merged worktree, and actually replay the archived compilers.
No additional optimization belongs to this iteration.

The candidate required retry passes 331 / 0 / 0 in 108.322 seconds (109.204 observed),
with unchanged compiler/source identities. Relevant native-cache integration
passes in 5.494 seconds; both C++ ownership/provenance executables rebuild and pass
in 21.486 seconds. Frozen archives remain read-only; the live compiler is writable.
Evidence is under `.cache/incremental-evidence/structured-20261113/`, specifically
`i030-observed-required.json`, `i030-observed-relevant-integration.json` and
`i030-cpp-ownership.json`.

Local master is now verified at source 1d40cf7f, executable dcf46435. The serial
C++ rebuild took 660.537 seconds, with 2,239,736 KiB peak RSS. Required passes
331 / 0 / 0 in 113.238 seconds (114.077 observed); full integration passes
463 / 0 / 0 in 801.393 seconds (802.733 observed). Both source/compiler identity
checks pass. The merged worktree and branch are removed; the six-build archived
comparison succeeds after removal and removes its reconstructed compiler-data
path. A separate 63-pair comment control also passes its artifact/identity gates;
median CPU is 0.029702 to 0.030252 seconds, paired ratio 1.01814 with interval
[0.99128, 1.03116]. This is an uncertain small difference, not proof of parity.
No cold-build improvement is claimed. Evidence: `i030-root-build.json`,
`i030-root-required.json`, `i030-root-integration.json`,
`i030-archived-replay.json`, `i030-cost-comment-63.json`.

The separate suite-classification audit verifies that `test_required_focus.py`
explicitly forces the focused C++ `numeric_literals` and `class_layout` checks
into integration. They test compiler behavior without application-library imports.
Correct that policy and audit/minimize imported compiler fixtures without weakening
the 120-second required gate. The current 331-check required pass does not mean
all compiler regression coverage has already been routed into that gate.

Next selected incremental goal, I031/P06: make classification dependency topology
a reusable product of its actual provider/query and scope inputs. The measured
unused edit still remaps 12,015 edges and rebuilds SCC propagation, taking about
17.117 ms. First measure the subphases and add a minimal work-count regression;
then retain unchanged topology and evaluate only the affected frontier. Require
the existing last-parameter-use, failed-lookup, provider/locality/source-movement
and cycle proofs, complete LLVM/diagnostic parity, an independent 31-pair >=2%
whole-build CPU gain, no established control regression, and required <=120s.
Otherwise park the candidate. Start its 45-minute clock when work begins; allow
only the contract's single evidence-backed extension. Full-goal audit and the
unimplemented consumer audit are `i030-full-goal-audit.json` and
`i030-next-opportunity-audit.json`. I030 completes publication, not the full goal.

## I031: topology retention screened out by subphase measurements

Started 15 November at 05:01:19 UTC, first deadline 05:46:19. Published source
02b07805 / compiler dcf46435 is unchanged. The comparison reuses frozen 0e3135e3,
whose tracked compiler/runtime/library sources are verified byte-identical to
02b07805. Its compiler-data replay and a fixed installed runtime preserve the
real LEXI8 source and native-output comparison. The probe compiler is 7fa0dfbf;
its normal single-job build takes 769.590 seconds, peak RSS 2,381,384 KiB.

Seven paired unused edits pass artifact/diagnostic and compiler-identity checks.
Dependency-phase medians are: edges 3.506 ms, reverse construction 0.888,
frontier 0.092, eager-provider traversal 0.327, SCC traversal 0.316, and body
eligibility 10.543. The complete non-eligibility graph subtotal is 5.128 ms:
even eliminating all of it would save only 1.05% of the 489.173 ms measured build,
before any cache-load/persistence cost. This cannot meet the predeclared >=2%
whole-build gate. Do not implement or merge topology retention on this evidence.

Decision: reject this performance hypothesis early. Preserve the timing probes,
compiled evidence and minimal future topology work-count specification on the
unmerged branch. The new test reaches the intended assertion on the published
compiler; topology retention itself is not implemented. Required/full integration
are not rerun for this unaccepted probe; the published compiler remains the fully
qualified I030 snapshot. No optimization or speedup is claimed from this screen.
Evidence: `i031-probe-build.json`, `i031-probe-unused.json`,
`i031-screen-decision.json`, and the unimplemented `i031-design-audit.json`.

Next selected goal, I032: isolate and reduce cached classification eligibility
and related matcher-product proof work. Separate scope/diagnostic/input checks,
reference-metadata restoration and match-key availability before changing any
reuse requirement. Preserve independent eviction/corruption and reactivation
proofs; a faster stage obtained by moving its work later is not an improvement.
Write the actual start/deadline and gain gate before implementation. The broader
topology boundary and test-classification policy issue remain unfinished.

## I032 outcome and I033 correctness repair

I032 began 15 November at 05:27:10 UTC; first deadline 06:12:10, extended deadline
06:57:10. Its single extension was recorded before the first deadline after the
31-pair CPU gate passed. Eligibility subphase probes identify 13,724 match-key
reads; the matcher lookup revalidates all 64 hex characters even for entries already
validated during segment loading. The proposed change reuses a proven index hit,
preserving the existing full validation on misses and newly loaded segments.
Focused native output and direct C++ valid-miss/invalid-key/malformed-segment checks
pass. The initial 31-pair CPU ratio is 0.975149 [0.968770, 0.998603]; elapsed-time
ratio 0.977776 [0.965220, 1.031855] does not establish an elapsed-time gain.
Common/cold/fast controls establish no regression. These measurements belong to
617f35b8 / executable f378fc85, not to a later artifact.

Broader testing stops on cached errors at lines 7, 5, 9 versus clean 5, 7, 9.
An independent four-line repro confirms the same defect on published 0e3135e3:
renaming a provider and editing a later body reports cached lines 3, 2, 4 versus
clean 2, 3, 4. The emitter consumes internal body/waiting queues directly; cached
reactivation legitimately changes that queue order. This is a pre-existing error
reporting defect, not a changed lookup result. The original failed gate is retained.

Decision: park I032 unqualified at 18de3ca8, combined artifact ac2e6ca3. Its diagnostic
repair and 132 broader rows pass, but its complete final-artifact qualification
cannot fit the remaining timebox. Preserve the lookup experiment and its initial
artifact f378fc85; do not merge its optimization, new counters, or eligibility probes.
Evidence: `i032-final-state.json`, `i032-cost-unused-31.json`,
`i032-qualification-regressions.log`, `i032-order-live-and-retained.json`,
`i032-red-order.log` and `i032-fixed-regressions/report.json`.

I033 began at 06:43:42 UTC, first deadline 07:28:42. This independent correctness
goal isolates only the diagnostic emitter and its minimal regression. It does not
contain the lookup optimization or the profiling fields. Source 6d9b70d9 / compiler
926a3eeb builds normally with one job; its order/repair and broader 132-build checks
pass. Three complete LLVM/diagnostic comparisons pass. Required is 331 / 0 / 0 in
106.938 seconds (107.861 observed); full integration is 463 / 0 / 0 in 800.626 seconds.
Both source/compiler identity checks pass. Master rebuild/required, push, merged
worktree disposal and actual archived replay remain the publication gates.

Resource cleanup is separate from performance claims: 310 obsolete root object
files absent from both current Ninja targets and commands are archived and byte
verified before removal, reclaiming about 523 MiB net. The current root target
still reports no missing work. Completed duplicate candidate/test executables are
removed only after their suites pass; the frozen compiler and rebuildable objects
remain. The complete incremental compiler objective is still unfinished.

## I039: independent required test groups

Started 15 November at 09:50:42 UTC, first deadline 10:35:42. The required
runner now overlaps its long incremental check with the other independent
auxiliary checks and fixture worker. Every group has isolated counters and
result storage; the parent joins both workers even after a crash. The unchanged
120-second outer watchdog covers the entire post-build suite. Required DynLex
fixtures still disable incremental caching; no checks or assertions were removed.

Red-before-green regressions cover concurrent worker rendezvous, counter
isolation, whitespace-bearing failure names, output aggregation, worker crashes,
success/failure status recording and joining a surviving worker after a crash.
A successful first auxiliary check previously returned the arithmetic increment's
status 1; the helper now explicitly returns after recording its test result.
The obsolete passed=1 test stub is removed. The initial real grouped run failed
on that status contract; its evidence remains preserved.

With frozen unqualified I038 compiler 2e567cdc, all 333 required checks pass
uncached in 103.988 seconds (105.092 observed). The harness-only cherry-pick
799e743c on master leaves its existing compiler 0ca41928 unchanged. After removing
both focused C++ unit executables and their four dedicated objects, all 333
checks pass in 113.370 seconds (115.233 observed), including those rebuilds.
Compiler and tracked source hashes remain unchanged during each run. These
passes establish the budget; censored earlier failures do not establish a paired
percentage speedup. This qualifies only the runner change. I038's compiler
optimization remains unmerged pending its remaining full qualification.

Evidence: i039-worker-regression-red.log, i039-auxiliary-status-red.log,
i039-join-regression-red.log, i039-observed-required.json (preserved failed
status-contract run), i039-fixed-observed-required.json,
i039-cold-cpp-unit-inputs.json and i039-root-required.json.

## I041: source deferral screened out by whole-build measurements

The isolated experiment retains 206 more flex owners and 417 more references,
but fails its predeclared 2% whole-build CPU gate. All 31 alternating real LEXI8
pairs and clean executable/diagnostic comparisons complete: paired CPU ratio
1.010983, 95% interval [0.988419, 1.031893]; wall ratio 1.005272
[0.967034, 1.029590]. No performance change is established. The candidate
55ed7be8 passes 30 focused, 38 independent contract and 237 broader builds.
Full required/integration qualification is not claimed for this rejected candidate.
Preserve its branch and evidence; do not merge it or retry its timing.

Outline restoration saves only about 0.33 ms and matcher restoration about
0.97 ms; cached-body selection rises about 2.6 ms and projection about 4.1 ms.
The timers overlap. Proof and selection work remain the larger opportunity;
fewer allocated references alone do not justify another cache domain. The next
iteration profiles eligibility/dependency and match-payload availability checks
before changing those contracts. The published I040 compiler and its verified
102.778-second required gate remain unchanged. Evidence is
i041-primary-stage-analysis.json and i041-binding-facts-cost-unused-flex-31.json.

## I042–I043: proof work screened and measured, unmerged

I042 measures 14,706 classification payload availability checks at 7.194 ms of
652.123 ms total. Even eliminating that work cannot meet its 2% gate, so no
optimization is implemented. Its probes and red independence contract stay on
branch codex/incremental-resolution-proofs.

I043 shares unchanged scope indexes and makes function binding proofs follow
actual query demand. Its isolated candidate passes 27 focused and 207 broader
builds, shares 4,219 scope entries and builds 10 proofs instead of 375. Binding
falls 24.526 to 11.426 ms, but all 31 real-program pairs fail the whole-build
gate: CPU ratio 0.991796 [0.976988, 1.005733], wall 0.982140
[0.970746, 1.014730]. No overall speedup is established. Preserve branch
codex/incremental-binding-inputs; no further timing retries or full qualification.
The next audit targets the larger matcher-store load/index cost. Production
compiler I040 and its 102.778-second required qualification remain unchanged.
The full incremental compilation objective remains incomplete.
Evidence: i042-screen-decision.json, i043-candidate-stage-analysis.json and
i043-candidate-unused-flex.json.

## I044–I045: typed records validated, strict fast-control gate failed

I044's seven complete phase probes measure matcher segment reads 14.990 ms,
verification 2.724 ms and indexing 3.908 ms. No flat-map prototype is implemented;
the measured larger metadata decode/publication path becomes I045.

I045 retains typed classification frames and re-encodes only changed units.
Source 3e449a4b / compiler d39b20a1 passes 7 focused, 166 matcher/classification
and 207 broader builds, four full normalized LLVM/diagnostic comparisons, all
333 required checks uncached in 83.440 seconds (84.452 observed), and all 461
integration checks in 684.045 seconds (685.007 observed). Source/compiler hashes
remain unchanged. Matching pre/post-schema test readers are archived explicitly.

The instrumented real-program benchmark has 63 alternating pairs: CPU ratio
0.977505 [0.972189, 0.982590], 2.25% faster; all 1,749 classification records are
retained. All eight controls complete and outputs agree. However, wording-only
CPU ratio 1.038636 [1.000696, 1.063553] fails the strict no-regression gate,
with medians 36.081 to 37.340 ms; wall change is inconclusive. That path does
not execute classification, so its cause is unverified. First-default builds
show no established change across seven pairs. No ordinary non-statistics timing
is claimed. Preserve the failed gate and branch codex/incremental-classification-records
(1b1af858); no timing retries or production merge. An optional absolute-tolerance
question is pending; no approval or preference is assumed.

The next larger source-interface audit distinguishes inputs consumed by resolution
from literal execution data, while retaining negative lookup, binding, metadata,
configuration and active-reader proofs. No shortcut is implemented yet. Main
compiler I040 remains unchanged; the full incremental objective is still active.
Evidence: i044-screen-decision.json, i045-candidate-unused-flex.json,
i045-controls.json, i045-first-default.json, i045-ir-parity/report.json,
i045-observed-required.json, i045-observed-integration.json and
i045-next-source-interface-audit.json.

## I046: resolution-input products at the source boundary

Started 15 November at 13:42:18 UTC; first deadline 14:27:18 UTC.
Persist source inputs consumed by resolution before matching. Reuse the resolution
result only when the source interface and the established inactive-source/inference
reader proof both remain equal. Stage APIs must expose analyzed source without
falsely advancing the live context's resolved validity. No spelling-specific edit
handler, no unproved ignoring of numeric lookup operands, names or metadata.
Require minimal counters, dependency/error/config/import/activation parity, at least
10% paired CPU gain over 31 alternating real LEXI8 pairs with a 95% interval
excluding parity, common/cold controls, full LLVM parity, required uncached within
120 seconds and affected integration before publication. Preserve I045's failed
strict gate; the optional fast-tolerance question does not block this work.
The full compiler objective remains incomplete.

### I046 qualification and disposition

The source-boundary product records scope structure, ordered source provenance,
metadata and lookup-bearing execution inputs. Lookup-free scalar execution values
are separate inference inputs. Equal resolution inputs and the existing complete
inactive-reader proof allow native-result reuse before classification, matching,
binding and validation. Source preparation/section checks still run; the live
context remains at AnalyzedSections. Changed inputs use the ordinary per-unit
resolution/inference products. No pattern spelling or imported-library name is
special-cased.

Candidate source core 0a85c3b9 / frozen compiler 1f2ccc42 passes 38 focused,
159 matcher/classification, 232 broader and 40 tail-replay compilations. A red
counter test exposed parser-cache hits omitted by the early return; collection
now runs on both paths. Component fixtures keep the edited function active so
they still verify resolution/classification replay. Separate no-import tests
verify whole-stage skipping, repeated edits, subsequent activation, last-parameter
retraction, imported edits and failed/numeric lookups. All modified code files
remain below 1,000 lines.

On the same frozen LEXI8 program, 31 alternating pairs give CPU medians
0.533958 to 0.155132 seconds; paired ratio 0.292624 [0.284283, 0.296316],
about 71% less CPU. Elapsed medians are 0.536362 to 0.159590 seconds.
Resolution, inference, object building and linking do no work on this unused
flex-value edit. Source work still evaluates 2,360 headers and 4,174 body-line
dispatches, reusing 44 prepared files and retaining 678 body syntax units.
These are instrumented builds; ordinary CLI and first-default timing are unmeasured.

Full normalized LLVM text and diagnostics match fresh builds for unused flex,
unused body, active body and game-state edits. Exact compiler 1f2ccc42 and test
source 9892d58c pass all 333 required checks uncached in 97.392 seconds
(97.685 observed) and all 461 integration checks in 787.016 seconds
(787.788 observed). Compiler and test-source hashes stay unchanged. Earlier
failed runs are retained: they exposed old work-count expectations, which were
migrated while preserving component behavior and clean-build parity.

The original fd22a838 candidate's eight controls complete with output/diagnostic
parity. Active-edit and cold CPU changes are inconclusive. Unchanged CPU ratio
1.021492 [1.009684, 1.035221] and comment ratio
1.058492 [1.010933, 1.104469] fail the strict gate, with median CPU differences
of 0.666 and 0.730 ms. Their cause is unverified. Preserve those failed gates;
no fast timing retry of the corrected executable is used to override them.
The implementation stays on codex/incremental-resolution-inputs, unmerged.
Root compiler I040 remains unchanged. One extension covers qualification;
closure is before the extended 15:12:18 UTC deadline.

A read-only next-step audit finds semantic-only function ownership, instantiation
and inactive-source tables occupying about 305 KB of one 395 KB default-cache
manifest. They are parsed on fast builds. Next evaluate lazy payloads for these
three products using the existing storage framework; inspect every consumer,
add red reuse/eviction/diagnostic tests and benchmark before keeping a change.
This audit is not an implementation and does not establish the fast regression's
cause. Section reconstruction, active-edit frontend setup and other broad stage
work remain; the full incremental objective is active and incomplete.

Evidence: i046-final-state.json, i046-corrected-unused-flex.json,
i046-controls.json, i046-corrected-ir-parity/report.json,
i046-verified-observed-required.json, i046-verified-observed-integration.json,
i046-source-input-test-support/manifest.json and
i046-next-lazy-manifest-audit.json.

## I047: lazy semantic-only native metadata

Started 2026-11-15T15:02:26.737827+00:00; first deadline 2026-11-15T15:47:26.737827+00:00.
Persist function ownership, function-instantiation identities and inactive-source
proofs in the existing lazy payload framework. Unchanged/comment/wording builds
should not decode them. Require no-import artifact/work tests, full dependency/
error/eviction parity, at least 10% paired CPU improvement for all three fast
cases over I046 across 31 pairs each, plus the complete production-baseline
comparison matrix. Ordinary CLI timing, first-default builds, full LLVM parity,
required uncached within 120 seconds and affected integration precede publication.
Use one 45-minute extension only after concrete progress. I046 remains unmerged;
its failed gate is not waived. The full incremental objective remains active.

### I047 qualification and disposition

Candidate 212f4697 / compiler 6cdfa6d7 moves function_sections,
function_instantiations and inactive_projection into existing CBOR payloads.
Code edits hydrate the inactive-source proof; only inference hydrates function
ownership and instantiation identities. New work counters distinguish those loads.
The no-import red test first rejects the old inline-table manifest, then covers
exact hits, unused/active edits, failure recovery, eviction and demanded corruption.

The first 31-pair matrix against I046 has paired CPU ratios 0.911415 for
unchanged, 0.917586 for comments and 0.854417 for wording. All intervals exclude
parity, but unchanged/comments miss the predeclared 10% minimum. Preserve this
failed gate. One bounded extension investigates another instance of the same
metadata issue: inference availability maps were constructed before inference was
known to be necessary.

Revision e9877232 / compiler 4c0aaeda retains availability observations and
artifact access times without allocating semantic lookup maps on fast hits.
The inference boundary materializes those maps. Borrowed key views remain owned
by native/failure snapshots, and the native snapshot moves into stable ownership
before observation. Counters show observed records with zero materializations
on exact and unused-value hits, and materialization when inference runs.

The revised matrix again completes 31 alternating pairs per fast case, with
executable/diagnostic clean parity. CPU ratios are 0.944510 [0.932283, 0.959408]
for unchanged, 0.942019 [0.918978, 0.975239] for comments and
0.843000 [0.834567, 0.866386] for wording. These are approximately 6%, 6% and
16% improvements; the first two still miss 10%. No third optimization or timing
retry is attempted. Both matrices remain authoritative; this candidate is parked
on codex/incremental-lazy-semantic-payloads and no source optimization is merged.

Exact compiler 4c0aaeda and test source 3bdcfb2f pass 35 focused,
159 matcher/classification and 232 broader compilations. All 333 required checks
pass uncached in 101.845 seconds; all 461 integration checks pass in 784.328
seconds. Source/compiler hashes remain unchanged. An earlier required run exposed
one unmigrated payload-count assertion: an active edit now consumes nine payloads,
including one inactive proof and two native function tables. The test now checks
those roles explicitly. All modified code files remain below 1,000 lines.
Matching pre/post-schema test support is preserved for historical replay.

The production-baseline matrix, ordinary CLI, first-default and full LLVM
comparison were prepared but not run because the attribution gate failed. No
claims are made for them. Root compiler I040 stays unchanged. This closure uses
one extension and is before its 16:32:26 UTC deadline. The full incremental
compiler goal remains active, with semantic section construction and other broad
stage work still incomplete.

Next perform a bounded same-compiler A/A calibration. The present fast protocol
has one randomly located application and fixed separate cache directories; warm
samples share those directory populations. Baseline timings differed between
experiments. That is not proof of a compiler or directory cause, but it warrants
checking independent cache populations before attributing sub-millisecond changes.
Output validation also repeats a digest on an exact hit; its cost is unmeasured.
No failed gate is waived by these unimplemented follow-up hypotheses.

Evidence: i047-final-state.json, i047-fast-attribution.json,
i047-availability-fast-attribution.json, i047-availability-focused/report.json,
i047-verified-observed-required.json, i047-verified-observed-integration.json,
i047-lazy-metadata-test-support/manifest.json and
i047-next-measurement-audit.json.

## I048: fast-path benchmark A/A calibration

Started 2026-11-15T16:00:17.005956+00:00; first deadline 2026-11-15T16:45:17.005956+00:00.
Run the exact frozen I046 compiler as both benchmark labels on the original
frozen LEXI8 inputs. Seven independent workspace/cache populations each run
16 alternating pairs for unchanged, comment and wording cases. Preserve every
original result, source/executable/diagnostic parity and fast work counters.
Report between-population variation separately from warm repetition; do not
select a favorable population, change compiler code, attribute a cause without
evidence, or waive old gates. The full compiler objective remains active.

### I048 result and disposition

All seven populations completed without a semantic or measurement failure:
112 alternating pairs per case, 672 measured invocations. Compiler, paired
source, executable and diagnostic identities agree, and every measured row
has frontend reuse, zero payload loads, zero objects and zero linking.
Median population CPU ratios are 1.004731 for unchanged, 0.999005 for comments
and 0.999664 for wording. Whole-population resampling intervals are respectively
[0.995743, 1.009981], [0.989906, 1.006712] and [0.993550, 1.009498].
None of the 21 original population CPU intervals excludes parity. This control
finds no demonstrated label bias or false positive. It does not prove interval
coverage, identify a directory cause, or waive the I046/I047 failed gates.
No compiler source changed. Close this calibration before its first deadline;
the next work returns to P03 source-section reuse rather than extending timing
calibration. The complete incremental compiler objective remains active.

Evidence: i048-aa-progress.json, i048-aa-population-{0..6}.json,
i048-aa-analysis.json and analyze-i048-aa.py.

## I049: reuse analyzed-source inputs before constructing sections

Started 2026-11-15T16:21:06.391826+00:00; first deadline 2026-11-15T17:06:06.391826+00:00.
P03 hypothesis: a retained ordered source-section input product can prove that
an inactive body edit preserves section layout and all resolution interfaces
before reconstructing the semantic section tree. Parse only changed execution
inputs; any changed declaration, lookup, indentation, import membership, active
reader or diagnostic input invalidates the proof and runs the ordinary stages.
Persist current body/source identities rather than leaving stale snapshots.
Expose reused source-line inputs, checked changed expressions and avoided section
construction. No language/library spelling is special-cased.

Baseline is frozen I047 compiler 4c0aaeda (source e9877232), with root I040
2e567cdc retained as the production reference. First add a minimal no-import
work-count/clean-parity regression. Acceptance requires at least 15% median
whole-build CPU reduction for the fixed real LEXI8 unused-body case over I047,
with an interval excluding parity, zero header/body section evaluations on
eligible edits, and no established unrelated fast/cold regression. Keep all
existing failed gates. Run focused invalidation/error/recovery tests, LLVM parity,
required uncached within 120 seconds and affected integration before publication.
Use one 45-minute extension only after concrete progress. Otherwise preserve
and park the experiment. The full compiler objective remains active.

### I049 first candidate: measured regression

Source 1a6e5ca5 / test correction d36f7e81 / compiler 179c9e73 passes 33
new focused builds. The 15-pair fixed application comparison against I047 fails:
CPU medians 151.797 -> 169.532 ms; paired ratio 1.140292 [1.080273, 1.173510].
Section headers fall from 2360 to zero, but saved parse/resolution-input times
(11.708/12.650 ms) are offset by source-product checking/copying (13.872 ms),
additional payload loading (8.668 ms) and writing (7.074 ms). Preserve this failed
matrix. One revision replaces the per-line JSON materialization/copies with a
compact immutable byte product, retains existing parser entries without constructing
outlines, and refreshes only changed body identities. The original 15% gain and
correctness/control gates remain unchanged. No source optimization is merged.

### I049 compact candidate: disposition

Source 91c8edd6 / compiler bd4eff4a uses an immutable byte product, preserves
parser keys without restoring outlines, moves products rather than copying them,
and recomputes body fingerprints only for changed owners. The framing of source
body inputs is shared with the existing producer. A formatter-induced include
ordering build error was fixed before this candidate; its failed build remains.

The fixed 15-pair unused-flex comparison against I047 improves CPU medians
151.220 -> 125.415 ms, paired ratio 0.817355 [0.793625, 0.834402]. Wall medians
153.013 -> 126.791 ms, paired ratio 0.829661 [0.814746, 0.906328]. Every measured
candidate checks one expression and evaluates zero section headers/body lines;
2360 baseline section headers are avoided. Executable and diagnostic comparisons
agree. This clears the primary 15% threshold, but does not clear the whole gate.

All fixed unrelated controls complete. Unchanged CPU ratio is 1.050837
[1.007880, 1.117872], comments 1.050208 [1.027529, 1.074680], wording 0.999238
[0.986547, 1.037459], active body 1.011574 [1.005603, 1.023826], and cache-disabled
cold 1.009914 [0.991941, 1.015488]. The first, second and fourth establish
regressions under the declared protocol. No retries or exemptions replace them.
Fast overhead is measured mostly in cache scanning; the cause of that difference
is not established. The active path also recaptures the global source input:
section_input_ms is 33.746 ms including 12.836 ms of ordinary section analysis.
Its new producer is therefore still broad work after an active edit.

The final source/compiler pass 33 focused builds, 159 matcher/classification
contracts and 265 broader builds (the latter include the 33 new checks). All
333 required checks pass uncached in 104.468 seconds (105.429 observer), with
source/compiler identities unchanged. Full integration, ordinary-CLI/first-default
qualification and the complete LLVM comparison are not claimed for this rejected
experiment. Preserve both candidates and all failed gates on
codex/incremental-section-inputs. No compiler optimization is merged.

Close this iteration before its first deadline, without using its extension.
The next bounded P03 investigation must use file/chunk source units shared with
their consumers: unchanged source units stay encoded on active as well as inactive
edits. An additional global proof cache is ruled out by this measurement. Any
continuation of this same experiment gets only the one remaining 45-minute
extension, ending no later than 17:51:06 UTC; it must otherwise park. Required
checks stay uncached and capped at 120 seconds. The complete incremental compiler
objective remains active and incomplete.

Evidence: i049-{primary,first-candidate-stage-analysis}.json,
i049-compact-{primary,controls,control-stage-analysis}.json,
i049-compact-{focused,contracts,regressions}/report.json,
i049-observed-required.json/log and i049-final-state.json.

### I049 bounded continuation: source-input units

Started 2026-11-15T16:54:09.568898+00:00. Use the one remaining extension; absolute deadline
17:51:06 UTC on 15 November, without another timing reset. Baseline remains
frozen I047 compiler 4c0aaeda; retain both failed I049 matrices. Attach immutable
analyzed-source inputs to the existing preprocessing chunks. On active edits,
reuse unchanged chunks' encoded input products; capture only invalidated units.
On eligible inactive edits, inspect only changed units and skip section objects.
Keep source products in the independent parser index, with small successful-stage
references in the existing lazy projection; remove the standalone native payload.
Missing parser products must not invalidate an otherwise valid native hit.

First add a minimal two-file active/inactive edit regression with unit capture/reuse
counters and clean parity. Retain last-parameter retraction, lookup, diagnostics,
import-layout and error recovery tests. The original >=15% unused-flex CPU gain,
no established unrelated control regression, uncached required <=120 seconds and
publication parity/integration requirements remain unchanged. If the deadline or
gates fail, park this whole source-input experiment. The overall goal is incomplete.

Before measuring the unit candidate, predeclare three independent original-harness
workspaces for unchanged/comment/wording controls (15 alternating pairs per case
per workspace). Report all original summaries and the median population ratios;
resample entire populations for an aggregate interval. This extends the I048
calibration's independence check to the new compiler; it does not replace or
waive a previous candidate's failed result. Primary unused-flex uses 15 pairs;
active-body and cache-disabled controls use seven pairs in their fixed workspace.
No favorable-population selection or additional timing retry is allowed.

The first unit candidate f8f9c543 / 05767edf passes its original focused/contracts
but a new minimal import-reparenting regression fails: moving an imported body
from an inactive to an active function preserves preprocessing input/global scope
set; a later literal edit reuses the stale executable. Intermediate inspection
confirms its cached owner is still function spare while the live consumer is
runner. Fix unit dependencies to include the actual consuming-scope sequence;
record that successful-stage contract and validate edited units in source order.
Retain the failing report and first unit timing matrices; this is a correctness
fix, not a reason to waive their failed fast/active performance controls.

### I049 continuation closed: correct source units, rejected performance gate

Closed 2026-11-15T17:51:17.481131+00:00. Evidence publication overran the final 17:51:06 UTC deadline by 11.5 seconds; compiler work and benchmark jobs were already terminal. One extension used. Source 07f406e1 / compiler 7d686114 remains isolated on codex/incremental-section-input-units. No compiler optimization is merged.

Source inputs now reuse preprocessing chunks and their exact successful-stage references. Active minimal edits capture one of twelve input lines; inactive application edits reuse 80 units and inspect one expression with zero section evaluations. Independent parser-product eviction leaves a validated executable usable. The import-reparenting bug in the first unit candidate is fixed with consuming-scope sequence dependencies and a regression that checks runtime and clean parity. All prior failed reports remain.

Primary 15-pair CPU medians 155.578 -> 126.843 ms; paired ratio 0.827305, interval [0.7906130686858094, 0.8648123704853585]. This clears 15%, but not the overall gate.

Instrumented fast control unchanged: population ratios [0.9850897877744902, 0.9993161285678935, 1.0027881722796965], median 0.999316, descriptive whole-population interval [0.9850897877744902, 1.0027881722796965].

Instrumented fast control comment: population ratios [1.0177618033988531, 1.002333882481869, 1.0094190960534322], median 1.009419, descriptive whole-population interval [1.002333882481869, 1.0177618033988531].

Instrumented fast control wording: population ratios [0.9418825985510446, 0.9305856832964974, 1.0187474050660963], median 0.941883, descriptive whole-population interval [0.9305856832964974, 1.0187474050660963].

Control body: CPU {'baseline_median': 1.445338999999997, 'candidate_median': 1.4690290000000061, 'paired_ratio_median': 1.0135800085504252, 'paired_ratio_interval_95': [1.0025251495917977, 1.0359052337959898]}.

Control cold: CPU {'baseline_median': 5.256510000000006, 'candidate_median': 5.307757000000002, 'paired_ratio_median': 1.003559411506635, 'paired_ratio_interval_95': [0.9897159295488864, 1.0363121158335076]}.

The declared screen rejects comment and active-body regressions. The three-group interval is descriptive and is not a coverage guarantee; retain every original within-population interval. No selection or retry replaces these failed results. Ordinary CLI without statistics establishes no regression for its three fast cases; it does not waive the instrumented failure. A 61-pair --version probe, which compiles nothing, costs 5.294 -> 6.050 ms CPU. This localizes part of the fast difference outside compilation, without establishing its underlying cause.

Correctness: 46 focused builds, 159 matcher/classification contracts and 278 broader builds (the latter include focused cases) pass. All 333 required checks pass uncached in 102.378 seconds; all 461 integration checks pass in 773.366 seconds. Exact source/compiler identities remain unchanged. Twelve real-program builds establish complete normalized LLVM and diagnostic parity for four cases. The required limit stays 120 seconds.

The complete objective remains active: semantic sections still rebuild on active edits, the global scope-set input overinvalidates source units on declarations, and resolution setup, persistence, tooling and other output paths still have broad work. Next investigate shared source/semantic catalog products and the separately measured process/configuration setup cost; do not add another whole-program proof cache or waive failed gates. Production remains I040.

Evidence: i049-unit-owner-{red,root-cause}.json/report, i049-scoped-unit-{focused,contracts,regressions}/report.json, i049-scoped-unit-{primary,controls,fast-analysis,ordinary-fast,version-profile}.json, i049-scoped-unit-observed-{required,integration}.json/log, i049-scoped-unit-ir-parity/report.json and i049-unit-final-state.json.

## I050: reusable semantic execution-section products

Started 2026-11-15 18:20 UTC; first deadline 19:05 UTC. Previous goal turn
answered a direct question and made no implementation progress. Current source
starts at 56ced37a on codex/incremental-execution-products; the production compiler
and all rejected I049 gates remain unchanged.

Complete one producer/consumer boundary: persist parsed execution-section topology
with its exact source, configuration, enclosing-scope and indentation inputs. An
active edit must retain unchanged execution regions without evaluating their
headers, dispatching their statements or repeating their finalization. Consumers
receive real current-source sections and deferred syntax products. Counters must
distinguish restored metadata from unevaluated semantic work. Start with regions
whose parse effects consist of ordinary execution/custom sections and expression
products; declarations, scope-rule effects and parse diagnostics still require
their actual producer until their effects have persistent representations. This
is one stage goal, not completion of the cross-stage objective.

Before code, add minimal active-edit, nested-section, import-reparenting, trailing
blank/boundary, diagnostic/error-repair and last-parameter-use parity tests. Verify
against clean builds across separate compiler processes. Benchmark the existing
real Breakout application with frozen I049 compiler 7d686114 as baseline: 12 paired
active-body edits, seven paired uncached builds, and 15 paired unchanged/comment/
wording controls. Keep only with correctness parity, substantial skipped section
work (at least half of eligible unchanged headers/body dispatch), a measured active
CPU improvement whose paired interval excludes parity, no established unrelated
regression, and the uncached required suite <=120 seconds. Affected integration
coverage is required before publication. No timing retry replaces a failed gate.
If the stage does not improve the real workload, preserve it isolated and move to
the next plan item; do not turn this into another global proof cache. At most one
45-minute extension is allowed for substantial concrete implementation progress.

### I050 first measured product: correct, not accepted

Source 1ec03b4e / compiler 4c1f093d passes 21 new builds, 159 matcher/classification
checks and 299 broader builds. The fixed original matrix completes. Active body
CPU medians are 1.4494055 -> 1.4425745 seconds; paired ratio .997149 with interval
[.988274, 1.017502], so no established gain. Unchanged paired CPU ratio 1.085259
[1.004969, 1.108567] fails the unrelated control. Other controls do not establish
a regression. No timing retry or selection replaces this candidate's failure.

An active edit reuses 372 regions/785 section records/3393 source lines, reducing
header evaluations 2360 -> 1947 and statement dispatch 4174 -> 1671. Nevertheless
parse medians increase 12.722 -> 16.629 ms. The old and new source-outline scoped
times remain about 3 ms: replay still constructs the same per-line deferred
products. New work also hashes every reused source line's identity again and
decodes another per-line metadata layer. Much of the apparent dispatch saving
is work moved into restoration, not eliminated work. Unchanged total compiler
wall scopes are about 20.6 -> 20.5 ms, versus process CPU 26.3 -> 28.2 ms; the
process-setup component is confirmed by the later compilation-free version
probe, but comparing whole-process CPU to an internal wall scope does not by
itself attribute the entire difference. Its underlying code cause is not established.

One fixed follow-up variant is authorized by the bounded iteration: share source
line fingerprints with persisted preprocessing products, and retain syntax as
whole execution-region products with lightweight stable line handles. Only a
consumer needing references/syntax should allocate the corresponding detailed
product. Keep the original deadline 19:05 UTC and all original gates/matrix sizes.
Preserve the original failed candidate and evidence. No additional variant or
timing retry is planned. This work does not complete the full incremental goal.

### I050 lifecycle correction and single deadline extension

At 19:00 UTC a four-build minimal repro confirms an omitted product-lifetime
rule: after a source-only inactive edit, the next active edit reuses zero
execution products; another active edit reuses one. The SourceComplete snapshot
preserves unvisited definition/temporary products but not the new execution
products, whose producer was also skipped. Correct that phase-publication rule,
add the failing edit sequence before the fix, and verify the exact corrected
compiler. Do not attach extra semantic dependencies to source input units or
change those units' immutable input/output contract.

Use the one allowed deadline extension, ending 19:50 UTC on 15 November. This
repairs a verified lifecycle defect; it is not another optimization variant or
a timing retry. Both original performance matrices remain failed, and no new
acceptance attempt is authorized in this correction. Preserve the completed
first/lazy artifacts and integration result. Rerun focused, matcher/classification,
broader, required and integration correctness on the correction; required remains
120 seconds. Then close and move to the measured persistence opportunity. The
full objective remains active and incomplete.

### I050 closed: lifecycle verified, performance unqualified

Compiler work closed 2026-11-15T19:25:58.840498+00:00, within the single extension ending
19:50 UTC. Final source f2c4a0c0 / compiler 712a2005 remains isolated on
codex/incremental-execution-products. Production C++ is unchanged.

The first candidate and the whole-region follow-up both fail the declared
performance gates. The latter's 12-pair active-edit CPU medians are 1.446475 ->
1.434477 seconds; paired ratio .992278, interval [.987646, 1.012614], establishing
no gain. Unchanged ratio 1.046545 [1.034051, 1.065242] and wording 1.050842
[1.014474, 1.090213] establish regressions. Comment 1.044538 [.973497, 1.076449]
and cache-disabled cold .992591 [.963883, 1.000759] are inconclusive. Keep both
original complete matrices; no retry or selection replaces them.

The correction changes the SourceComplete publication rule to preserve the new
execution products when their producer is skipped. The minimal red test and
intermediate inspection confirm the omission; three repeated inactive/active
pairs now preserve reuse. A separate removal probe confirms Complete publication
prunes the removed owner while retaining/reusing the untouched one. No extra
semantic dependency was attached to the immutable source input units.

Exact corrected-source verification: 36 focused, 159 matcher/classification and
314 broader builds pass (the latter include focused cases). Required: 333 / 0 / 0
in 105.195s (106.176s observer), uncached and within 120 seconds.
Integration: 461 / 0 / 0 in 690.694s (691.994s observer).
Source/compiler identities remain unchanged. Twelve real builds establish full
normalized LLVM/diagnostic parity for four edits. The corrected artifact restores
372 execution regions / 785 metadata records / 3393 source lines; 2916 line handles
need only 274 detailed products. Declaration headers and global setup still run.

A compilation-free 61-pair version probe on the measured follow-up costs 4.889 ->
5.453 ms CPU, paired ratio 1.103487 [1.022046, 1.180388]. Minor-fault medians rise
949 -> 1002. A separate 31-pair loader-instrumented probe records loader phase
cycles. This confirms a startup component; it does not establish the exact
underlying code/link-layout cause or attribute the entire fast-build difference.
The corrected artifact was not remeasured for acceptance.

Next bounded work should begin from production, preserving these isolated
experiments: persistent graph units shared by capture and encoding. On the
measured follow-up, graph encoding is 32.102 ms and capture 50.801 ms; snapshot
construction is 132.798 ms, cache writing 95.874 ms, resolution setup 115.039 ms.
These scopes overlap their parent stages and must not be added to those parents.
Graph persistence still encodes the entire graph even when graph entries reuse.
Avoid thousands of loose cache files, another global proof cache, and an isolated
header cache. Startup attribution remains a separate task. A preference question
about future small startup tradeoffs is pending; existing gates are unchanged.

The full objective is active/incomplete. Remaining work includes semantic
declaration catalog reuse, finer source/config dependencies (including the global
scope-set input), global resolution setup, graph/persistence units, broader parse
effects, and non-native/tooling paths. Required-suite timing does not establish
that all compiler regressions are correctly placed in that suite.

Evidence: i050-{performance,lazy-performance,startup-profile,final-state,completion-audit}.json,
i050-{red,lazy-red,lifetime-red}/report.json, i050-lifetime-repro.json,
i050-product-pruning.json, i050-retained-{focused,contracts,regressions}/report.json,
i050-retained-observed-{required,integration}.json/log,
i050-retained-ir-parity/report.json and i050-retained-test-support/manifest.json.

## I052: preserve dependency facts for deferred source matches

Started 15 November 2026 at 20:23 UTC; first deadline 21:08 UTC. This bounded
correctness goal starts from production f004339f. Preserve the failed I051 graph
packet experiment on codex/incremental-graph-products; no performance retry or
gate waiver. Its active-edit CPU regression and ENOSPC report are recorded there
and in i051-edges-partial-analysis.json.

Persist matched-source dependency facts independently of aggregate forward and
inferred edges. A retained matching producer must carry its body-layout and
resolved-match product identity. Graph capture reuses facts only for that exact
producer version; a changed/missing fact product demands that owner's current
match result. Do not force unaffected source materialization, copy old inferred
edges, or change the parity test to hide the difference. Migrate the compact
graph codec and inspection helper together. Add minimal warm/fresh parity tests
first, including edits to other owners and changing call/inference demands.
Verify runtime, diagnostics, graph parity, dependency cascades, required uncached
<=120 seconds and affected integration. This repairs actual dependency state;
it does not complete incremental graph persistence or the full compiler goal.

The initial production repro is red: editing one called owner removes a source
edge from another owner's deferred execute scope. The first candidate fixes that
edit while retaining the unrelated body. Adding an unused declaration then shows
a second, distinct issue: section-level reads flatten trial ancestors only during
fresh inference. Calls already register direct callee reads before inference or
replay; retain direct section reads and follow those callee edges instead of
publishing a cache-history-dependent transitive view. Constraint-probe closure
tracking is unchanged. Both cases remain exact warm/fresh graph parity checks.

### I052: include failed constraint snapshots; one bounded extension

At 21:02 UTC the completed e50e191a / df9f4483 checks are 24 focused,
159 matcher/classification and 207 broader compilations, all 333 required checks
in 97.883 observer seconds, and four complete normalized LLVM/diagnostic edit
comparisons. The library run is live and will finish on its unchanged inputs.
These results do not qualify publication yet: inspecting every consumer found
that captureConstraintGraph still collects only materialized references. A
six-line failed-constraint repro and a separate red regression confirm that it
loses the unchanged second owner's source edges after editing the first owner.

Migrate this last consumer to the same versioned match facts. Retain them in
failed constraint snapshots, and allow native/inference graph capture to consume
the newer failed-stage match producer independently of its older typed graph.
Add failed/fresh graph parity and repair tests before changing the compiler.
Use the single additional iteration, ending 21:53 UTC on 15 November. Complete
exact-source verification and measure correctness-repair costs; do not claim a
speedup or merge the partial fix. No second extension or timing retry is planned.
