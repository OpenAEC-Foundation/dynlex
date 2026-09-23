# Curve2 and Polyline2 source review

Reviewed source: `src/geom2d/curve.rs` and `src/geom2d/polyline.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, including all production helpers
and all 24 source tests. The additional dependency scope is
`Curve::tangent_at` in `geom2d/arclength.rs:65` and the angular sampling path
in `geom2d/deviation.rs:67`. This review does not certify the other arc-length
or deviation operations.

The reviewed native files and their imports are frozen in
`build/cadkernel-curve2-review/snapshot`. File references below refer to that
snapshot; subsequent extraction can change live line numbers.

| Artifact | SHA-256 |
| --- | --- |
| `lib/cadkernel/curve2.dl` | `70db01bf0f63c6a1d3b5b8b1e71213ca22d2ff1a39a09e0ef94ebe33cb92eec9` |
| `lib/cadkernel/polyline2.dl` | `bbba14fbe934237478241691a2b5b2b6ad293d4cb4e753acadf0d81d75d40729` |
| `build/dynlex.exe` | `91d137351179e189166b3b39b038e09896ad837e346b1600f3a33d10a392eec9` |

`build/cadkernel-curve2-review/snapshot-manifest.json` records 217 captured
files. `snapshot.py` refuses to overwrite an existing snapshot. The native
compiler ran with the snapshot as its working directory and import root.
The reference driver checked ten upstream files against their pinned Git
blobs before compiling unchanged source modules and the verbatim Ellipse
declaration/implementation.

## Findings

No concrete production correctness defect was found in the reviewed scope.
Two verification issues remain:

1. **P2: imported pattern warnings prevent a clean strict verification run.**
   `lib/cadkernel/spline.dl:97` repeats `knots` in `with knots knots`;
   `lib/cadkernel/nurbs2.dl:321` repeats the typed `pieces` capture in
   `with {integer:pieces} pieces per span`. The current compiler emits
   `Repeated pattern word ... stays literal because it already became a
   parameter` for both. Compilation exits zero, but the strict runner's
   unexpected-diagnostic check fails. Eight successful-build checks in the
   observation run emitted these warnings. The polyline-only fixture is clean.
   The diagnostic originates in
   `src/cpp/compiler/patternResolutionExpansion.inl:267`,
   `emitDuplicatePatternWordWarnings`, with warning construction around line
   311. Its explicit-literal handling exempts bracketed literal words.
   The no-import `build/cadkernel-curve2-review/repeated-word.dl` reproduces
   the diagnostic at O0/O2. **Recommendation:** the owning modules should
   disambiguate intended literals/captures and verify that their public call
   patterns stay unchanged. This is an interpretation of the intended API,
   not evidence of a geometry failure or a compiler crash. Do not suppress
   unexpected compiler diagnostics to obtain a passing verification result.

2. **P3: two source assertion conditions are not explicit in the native
   Polyline2 fixture.** In
   `tests/required/cadkernel_polyline2/main.dl:91`, `original` is used without
   asserting `original.valid`; upstream `polyline.rs:389` uses `.unwrap()`.
   At native lines 105–106, the closed-seam test checks the first two source
   indices but not that the provenance list has exactly two elements;
   upstream `polyline.rs:427` compares the entire collected vector to
   `[3, 0]`. Other assertions provide indirect coverage, but do not state
   these same conditions. **Recommendation:** add an explicit validity check
   and an exact list-length check. Both conditions pass in the independent
   `contracts.dl` probe at O0/O2. This is a test-parity gap, not a demonstrated
   production failure.

The pinned `.unwrap()` and whole-vector equality are explicit source
requirements. The priority labels and suggested DynLex assertions above
are review judgments.

## Source behavior and API coverage

| Source behavior | Native entry points reviewed |
| --- | --- |
| Primitive records, constructors and derived equality | `a cad line2 from ... to ...`, `a cad circle2 at ... with radius ...`, `a cad arc2 at ... with radius ... from ... to ...`, `a cad ellipse arc2 on ... from ... to ...`, `the full cad ellipse arc2 on ...`, `a cad ray2 from ... along ...`, `a cad xline2 through ... along ...`; corresponding equality overloads |
| Line direction/length, Arc/EllipseArc sweep | `the cad direction of ...`, `the cad length of ...`, `the cad sweep of ...` |
| Extent and eight Curve variants | `the cad curve2 of ...`, kind constants, `the cad extent of ...`, `... holds cad parameter ...`, `... is cad straight`, `the cad ray of ...`, `... is cad closed` |
| Evaluation and inverse parameter | `the cad point on ... at ...`, `the cad parameter on ... nearest ...`, Curve2 and Polyline2 overloads |
| Decomposition and rectangular boundaries | `the cad segment count of ...`, `the cad segments of ...`, `the cad rectangle side of ... with bounds ...` |
| Curve angular/density sampling and tangent dependencies | `the cad angular samples of ... within ...`, `the cad samples of ... with density ...`, `the cad tangent of ... at ...`, curve point/tangent protocol aliases |
| BulgeArc construction, signed evaluation and sampling | `the cad bulge arc2 from ... to ... with bulge ...`, `the cad point on ... at ...`, `the cad angular samples of ... within ...`; curve point/tangent protocol aliases |
| Polyline vertices, empty/default, segment arcs | `a cad polyline vertex2 at ... with bulge ...`, `a straight cad polyline vertex2 at ...`, `the cad polyline2 with vertices ... and closure ...`, `an empty cad polyline2`, `the cad segment arc of ... at index ...` |
| Ranges and provenance interpolation | `the cad range of ... from ... to ...`, `the cad interpolation of ... from ... to ...`; vertex, range-segment, bulge-arc, polyline and range equality |
| Native ownership additions | `the cad clone of ...`, `free cad curve ...`, `free cad polyline ...`, `free cad polyline range ...` |

The source's arithmetic and branch behavior, rather than a simplified
geometric interpretation, was used for comparison:

- `curve.rs:299` reevaluates Line/Ray/XLine endpoints through their point
  formulas. The Line angular-sampling branch returns stored coordinates;
  Ray/XLine angular sampling reevaluates parameters zero and one. Overflow,
  infinity times zero, cancellation and signed zero can distinguish these
  cases. The port preserves that distinction.
- `curve.rs:448` uses segment-uniform, clamped Polyline parameters, including
  empty and single-vertex handling. BulgeArc sampling itself is signed and
  unbounded. Inverse polyline parameters preserve angle wrapping at the
  branch cut and the source's nearest-segment selection.
- `polyline.rs:204` preserves stored range endpoints at local parameters
  zero and one. Partial straight endpoints use the weighted expression
  `(1-t)*start + t*end`; point evaluation uses its separate interpolation
  formula. Partial bulges retain the source atan/tan calculation, while a
  complete segment preserves the stored bulge. Closed ranges preserve the
  original source indices across the seam.
- Polyline angular sampling restores stored endpoints before exact joint
  deduplication. The equality is numeric coordinate equality: signed zeros
  compare equal and NaNs do not. No tolerance-based deduplication was added.
- `curve.rs:248` reverses clockwise BulgeArc endpoints when constructing
  a counterclockwise Arc variant. Polyline tangent evaluation follows that
  decomposition, including its direction convention.
- `EllipseArc::sweep` adds one turn when its raw difference is nonpositive;
  it does not repeatedly normalize arbitrary multiple-turn differences.
  NaN-preserving parameter clamping and rectangle tolerance/order checks
  match the source, including NURBS control checks before boundary coverage.

## Source assertion inventory

All source test functions were compared with their translated assertions.
The two exceptions are recorded above; source test names alone were not
treated as proof of assertion parity.

| Curve source test | Polyline source test |
| --- | --- |
| `every_curve_runs_from_zero_to_one` | `a_half_turn_bulge_gives_the_chord_as_diameter` |
| `a_line_parameter_is_the_fraction_along_it` | `the_bulge_sign_picks_the_side` |
| `a_parameter_past_the_end_continues_the_line` | `sampling_hits_the_endpoints` |
| `an_arc_parameter_walks_its_own_sweep` | `a_quarter_turn_bulge_sweeps_a_quarter_turn` |
| `closedness_is_reported_per_shape` | `a_bulge_over_one_sweeps_past_a_half_turn` |
| `a_polyline_shares_its_parameter_between_segments` | `degenerate_input_has_no_arc` |
| `a_closed_polyline_has_a_segment_back_to_the_start` | `a_closed_polyline_arcs_back_from_its_last_vertex` |
| `a_polyline_arc_segment_bulges` | `an_open_polyline_has_no_closing_segment` |
| `tessellation_keeps_both_endpoints` | `a_range_keeps_segment_provenance_and_interpolation_parameters` |
| `a_straight_line_needs_only_its_endpoints` | `a_partial_signed_arc_remains_the_same_exact_arc` — validity gap |
| `a_tessellated_arc_stays_on_its_circle` | `a_closed_range_can_wrap_once_across_the_original_seam` — count gap |
| `a_degenerate_polyline_does_not_panic` | `invalid_ranges_and_geometry_are_rejected` |

## Ownership and inactive variants

The native Curve2 stores eight payload fields. Polyline and NURBS are managed
members; aggregate member traversal handles their lifetime. Primitive
constructors initialize inactive payloads to null values. The type-only NURBS
witness in `curve2.dl:151` is not executed by the empty-value constructor:
`build/cadkernel-curve2-review/primitive-O0.ll:184` contains only zero field
initialization and return for that constructor. This establishes absence of
witness allocation there, not absence of unrelated program allocations.

Polyline construction copies a borrowed vertex list. Managed copies retain
storage, explicit clones copy active owned contents, and range results own
both the nested polyline and provenance storage. Free patterns reset the
caller-visible value. Raw list views require a live owner and are documented
as borrowed; they must not be independently freed or mutated through the view.

The existing ownership fixture checks detached input lists, shared versus
deep copies, list removal, nested ranges, null inactive fields, NURBS knot
detachment and 1,000 repeated cycles. Independent `contracts.dl` checks
primitive replacement of a local managed variant, list append/replacement/
removal, nested copies and release, and exact reference-count restoration
after 100 helper calls for each managed payload. Retained curves survive
resetting their original owners. Internal DynLex calls may pass addressable
arguments by reference (`docs/stages.md:435`); the probe therefore makes an
explicit local copy before testing local replacement.

## Verification results

Fresh native Windows run with the compiler hash above and direct
`stable-x86_64-pc-windows-msvc/bin/rustc.exe`, version 1.91.1:

| Check | Result |
| --- | --- |
| Original Rust Curve/Polyline tests | 24 passed |
| Three required fixture outputs, O0 and O2 | 6 matched |
| Independent ownership/source-assertion contracts | 2 matched |
| Six type/visibility refusal fixtures, O0 and O2 | 12 expected rejections |
| Existing differential | 722 cases; 1,126,844 Rust scalar comparisons passed |
| Independent boundary differential | 159 cases; 23,210 Rust scalar comparisons passed |
| Combined differential | 881 cases; 1,150,054 Rust scalar comparisons passed |
| Native O0 versus O2 | Exact, including zero signs; NaN classes agree |
| Strict successful-compilation diagnostic checks | Failed on 8 checks; two imported warnings per check |

Scalar comparison counts include both native optimization levels. Rust finite
comparisons retain the documented relative tolerance `3e-12`; coordinates
also allow 16 ULPs of geometry magnitude. Stored Line sampling coordinates
are compared exactly. These are not claims of bitwise Rust parity for every
trigonometric result. The additional cases cover stored versus reevaluated
endpoints (36), signed bulge wrap (80), ellipse zero/turn parameters (8),
range arithmetic (6), closed seams (8), degenerate sampling (12), and
nonfinite BulgeArc sampling (9).

The strict runner stops at unexpected diagnostics. The explicit observation
mode records those failures, continues independent runtime comparisons, saves
`strict_diagnostic_check: false`, and exits 2. No warnings are filtered out.
The refusal checks require exit 1 and the expected diagnostic fragments;
their complete output, including additional warnings, is retained.
This review provides no sanitizer, allocation-failure or other-target proof.

## Reproduction and extraction boundary

Run from the worktree root. The existing captured snapshot is required;
`snapshot.py` captures live files and must not be used to replace this evidence.

```text
C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -B build/cadkernel-curve2-review/review.py --source C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546
C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -B build/cadkernel-curve2-review/review.py --source C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546 --observe-native-diagnostics
C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -B build/cadkernel-curve2-review/refusals.py
```

All compiler, Rust and executable launches use Python subprocess under
`scripts/process_error_mode.py`'s unattended error-mode context. The compiler
is not rebuilt. The first command fails strictly on the recorded warnings;
the second completes observations and exits 2; the refusal command exits 0.

Evidence lives under `build/cadkernel-curve2-review`: `results.json`,
`refusals-results.json`, `curve-rust-tests.txt`, `polyline-rust-tests.txt`,
`extra-rust-results.json`, `contracts.dl`, `primitive.dl`, `primitive-O0.ll`,
`primitive-compile.txt`, `repeated-word.dl`, and its two optimization logs.
The captured `tests/cadkernel/curve2/{verify.py,probe.dl,reference.rs}` defines
the original dataset, output schema and reference wrapper. These build
artifacts are local evidence; they are not required fixtures or production
dependencies.

This report covers the identified snapshot only. Moving tangent/sampling
helpers into separate modules requires a final comparison of signatures,
visibility/import reachability, exact formulas, recursion limits and ownership
against this snapshot, followed by affected O0/O2 verification. That extraction
diff has not yet been reviewed here.

On 2026-09-14 the live Curve2 and Polyline2 SHA-256 values still matched the
snapshot exactly. No visibility, comment or helper-body delta was present
in those two files to review at that point.

The subsequent integration delta exposes only `the cad curve2 uniform samples
of ... in ...` and `the cad curve2 span boundaries of ...`, with a caller-owned
list comment on each. The exact comparison in
`build/cadkernel-curve2-review/integration.py` verifies unchanged bodies and
imports. Final Curve2 SHA-256 is
`ae53b80f00403d9ef2b725d914a2e377de1f227a485f5724ced79fb86950317e`.
With compiler SHA-256
`47b0eef44c8d24f4302baab142b5bfd4130b6a848e594044818c0c9ec8f132c2`,
the three existing required fixtures and an importing caller exercising both
helpers pass at O0/O2: eight clean compilation/runtime checks.
`integration-results.json` records the results and commands can be reproduced
with the same Python runtime running `-B build/cadkernel-curve2-review/integration.py`.
The earlier imported warnings are absent in these new integration runs. The
881-case differential results above remain attributed to their original
compiler and snapshot; they were not rerun for this visibility-only delta.

On 2026-09-15 both P3 assertions were added to the required Polyline2 fixture:
the original signed arc must be valid, and the wrapped provenance list must
have exactly two entries. The updated fixture passed at O0/O2 without diagnostics
using compiler SHA-256
`5b81b92da3da254c9deb12e062272b2aa2c8eddf233cf59f9b2350b543d60817`.
The two recorded verification findings are closed for this integrated snapshot.

A further exact source comparison includes explicit grouping of three clone
operands: `shape's polyline`, `shape's nurbs`, and `range's polyline`. Without
parentheses the surrounding clone can also consume the complete owner before
the property is read. `build/review-curve-clone-delta.py` confirms these are the
only additional changes beyond the two sampling-helper visibility edits.
Current Curve2 SHA-256 is
`0d4738e5025524e26b3d7881941b95ac179a90ba21a08df1384759f5f1721263`;
Polyline2 is
`d113e9fc83966629661016145d566e4894c74fb3b9d57b3c49ee72915e706f19`.
The required curve, polyline and ownership fixtures passed in the complete
610-check run on compiler
`ec4f37ae0da9455353f8825f05f69caed3b3567e529d484e6037993f8c0e85bc`.
The original 881-case numerical comparison remains attributed to its recorded
snapshot and compiler; this source delta is not a rerun of that comparison.
