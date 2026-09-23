# Native geometry and compiler verification

This record describes implementation-side source inspection and executed
verification. It is not an independent reviewer sign-off or full-port approval.
The complete 83-module delivery remains incomplete.

## Compiler and standard library

Compiler SHA-256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.

The full required suite passed **633 checks, zero failures and zero skips**
in 953.203 seconds (`build/required-area2-budget.log`). The checked integration
state includes provisional signature domains, unevaluated type-query effects,
borrowed-expression rollback, buffered JSON-RPC stderr draining, split B-rep
fixtures, affine transforms, signed area and centroids.

Inspection of the borrowed-expression journal confirms first-write capture,
preservation of the outer snapshot during nested promotion, and restoration
before provisional instantiations are removed. Structural constraint probes
discard speculative layout indices; incomplete candidate domains only exclude
known mismatches. Execution-effect snapshots remain distinct from committed
type and grouping information. Import-free focused regressions cover these
boundaries; their 42 O0/O2 and 38 recursion results are documented in
`cadkernel-type-query-effects.md`.

The stdout/stderr test barrier is process completion, which joins the readers.
The lifecycle assertion compares the complete early-plus-final stderr content,
so it still detects loss or duplication without imposing cross-stream ordering.
The synchronized buffered-stderr fixture fails without its library correction
and passes at O0/O2 with it; all related checks pass in the full run.

Large graph/diagnostic integration programs take approximately 18–29 seconds
to compile on the validation host. Their explicit 60-second budgets replace
the unsuitable ordinary 20-second Windows limit for those fixtures only.
The parser enforces 1–300 seconds and rejects malformed limits. Tests preserve
all runtime/output assertions. Ordinary fixture and runtime deadlines remain
unchanged. The preceding 624-pass/two-timeout result is superseded by the full
633-pass run.

## Native source checks

| Module | Source comparison and executed evidence |
| --- | --- |
| B-rep topology and diagnostics | Complete node/body/provenance operations, typed keys, nested ownership and all active payload fields; 210 graphs / 19,372 Rust trace lines. Split wrappers pass 10 O0/O2 checks, and both combined programs pass again after splitting. |
| Planar affine transforms | All 18 source tests, variant promotion/reflection, independent transformed storage and diagnostics; 354 cases / 24,230 comparisons. |
| Signed area and centroids | All 12 source tests, exact quadrature ordering, translated moments and both open-chain conventions; 218 cases / 3,456 comparisons. No source polyline convention was silently corrected. |
| Source-directed spatial joins | All five operations and two source tests, degree elevation, seam preference, rational scaling and borrowed input lifetime; 289 cases / 10,554 comparisons. Prepending preserves source controls and weights exactly. |
| Planar tessellation | All 11 source tests, world-coordinate binary64 output, ellipse parameter direction, density behavior and unchanged elevation; 118 cases / 38,018 comparisons. Unrepresentable native list sizes are explicitly rejected before conversion/allocation. |
| Rational spline approximation | Both source tests, exact knot insertion, adaptive rational subdivision, original work/depth limits and retained output storage; 88 cases / 35,116 comparisons. The million-point and maximum-depth ceilings have not been exhaustively exercised. |
| Endpoint length changes | All five source groups and all four operations, including production code after the source test block; scalar/angular/dynamic behavior, reversed-chain source metadata and retained result storage; 1,382 cases / 15,496 comparisons. |
| Spatial import surface | All spatial module imports compose; construction, approximation, measurement, endpoint editing, alignment, helix conversion and polygon meshing pass together at O0/O2. |
| Planar fillets | Both ray and general offset-intersection constructions, all 20 source groups, ordered centre deduplication and scalar/list lifetime; 565 cases / 17,982 comparisons. |
| Geometric snapping | All four operations and six categories, all 16 source groups, source finite-difference refinement and result lifetime; 754 cases / 30,170 comparisons. |
| Finite-segment centre lines | Complete source, six independent native groups, picked sectors, stable endpoint identities and extension refusal behavior; 653 cases / 10,432 comparisons. The source has no unit tests. |
| Planar arrangements | All 18 source groups, tagged/untagged splitting, spatial welding, directed-face traversal, full-width provenance and nested ownership; 597 cases / 139,260 comparisons. One source min/max overlap bound changes its zero sign with optimization; each native mode matches its Rust mode. |
| Variable-width polyline bands | Complete source, six independent native groups, rail/corner geometry, two-pass cleanup, source distances and ownership; 441 cases / 200,826 comparisons. Native optimization parity is exact; Rust hash iteration produces documented coordinate/station rounding differences even within one binary. |
| Rational B-rep builders | Complete source, eight native groups, deep ownership and four typed refusals plus oversized runtime guards; 515 cases / 216,098 comparisons with exact native/Rust optimization parity. The source has no unit tests. |

| Spatial B-rep bounds | Complete source, all nine original groups and 924 cases / 49,486 comparisons; oriented analytic extents, NURBS hull validity, damaged topology and operation tolerance; exact optimization parity. |
| Solid constructors (partial) | Cuboid, cylinder, sphere, cone, pyramid and polygonal frustum pass 490 cases / 280,472 comparisons, including all arena fields; exact optimization parity. Faceted solids additionally pass 413 cases / 260,294 comparisons after canonicalizing only source-unordered edge keys. Remaining constructor work continues. |

All differential counts include O0 and O2. Within each language, optimization
parity is exact except for the separately recorded arrangement overlap zero
sign and Rust band-map ordering described in their READMEs and summaries.
Differential runners retain signed-zero/non-finite checks,
exact count/flag comparisons and documented floating tolerances. The original
Rust source is pinned at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and checked
for drift. No geometry implementation is replaced by a Rust wrapper.

The spatial-join and planar-tessellation required wrappers were added after
the full run started. They pass an additional four and six O0/O2 checks,
respectively; those are not included in the 633 full-suite total.
Evidence: `build/source-join3-required.log`, `build/tessellate2-required.log`.
The approximation wrappers pass four further O0/O2 checks
(`build/polyline-approximation3-required.log`), and endpoint changes plus the
spatial import surface pass six (`build/lengthen3-space-required.log`). These
were also added after the full run and do not change its 633-check count.
The fillet wrappers pass four further checks (`build/fillet2-required.log`).
The snapping wrappers pass four (`build/snap2-required.log`).
The centre-line wrapper passes two (`build/centerline2-required.log`).
The arrangement and pair-index wrappers pass sixteen, including ten expected
runtime refusals (`build/arrangement2-pair-index-required.log`).
The band wrappers pass four (`build/band2-required.log`); the differential
runner separately checks two malformed width types per optimization mode and
six comparison-harness tests.

Dynedra Lab now includes six scenarios, with planar face counts/areas and a
variable-width circular/straight band beside the existing four curve/surface
examples. Source guides retain their storage with scene copies. The selector,
parameter limits, planar camera, framing and displayed measurements were
updated together. Model, state and six drawable hidden-window frames pass at
O0/O2, and the interactive application compiles in both modes
(`build/lab-planar-graphics.log`, `build/dynedra-lab-3b017ymk`). This is actual
native draw completion, not a pixel inspection. CPU-only model/state required
wrappers pass four further checks (`build/lab-planar-required.log`).

Remaining review scope includes the still-unported modules, expanded application
scenarios and an independent review of the final complete changeset before the
single delivery PR.

Rational-builder required wrappers pass four additional checks
(`build/nurbs-builder-required.log`). Final differential and refusal evidence
is in `build/nurbs-builder-final-checks/summary.json` and
`build/nurbs-builder-final-differential.log`. No compiler change was required.

Spatial-bounds required wrappers pass two further checks (`build/brep-bounds-required.log`). Faceted-solid API type witnesses reject three malformed empty-list types at both optimization levels (`build/brep-faceted-refusals.log`).
