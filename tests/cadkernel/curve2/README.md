# Curve2 and Polyline2

Native binary64 ports of `src/geom2d/curve.rs` (510 production lines,
12 tests) and `src/geom2d/polyline.rs` (248 production lines, 12 tests) at
cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
Source: <https://github.com/HakanSeven12/cadkernel>.
Both libraries carry SPDX MPL-2.0 provenance.

## Public shapes and patterns

All points and directions use `cad vector2`; radii and parameters use f64.
The existing `cad ellipse`, NURBS2 and core definitions are reused.

| Payload | Construction |
| --- | --- |
| `cad line2` | `a cad line2 from START to END` |
| `cad circle2` | `a cad circle2 at CENTRE with radius RADIUS` |
| `cad arc2` | `a cad arc2 at CENTRE with radius RADIUS from START to END` |
| `cad ellipse arc2` | `a cad ellipse arc2 on ELLIPSE from START to END`; `the full cad ellipse arc2 on ELLIPSE` |
| `cad ray2` | `a cad ray2 from ORIGIN along DIRECTION` |
| `cad xline2` | `a cad xline2 through BASE along DIRECTION` |
| `cad polyline2` | `the cad polyline2 with vertices VERTICES and closure CLOSED`; `an empty cad polyline2` |
| `cad nurbs curve2` | Existing constructors in `nurbs2.dl` |

`the cad curve2 of PAYLOAD` wraps any of these eight payload types in one
managed runtime record. Its `kind` values are 0 line, 1 circle, 2 arc,
3 ellipse, 4 polyline, 5 NURBS, 6 ray, 7 xline. Named values are
`cad line curve2 kind`, `cad circle curve2 kind`, and so on.
The corresponding payload fields are `line`, `circle`, `arc`, `ellipse`,
`polyline`, `nurbs`, `ray`, `xline`. Read only the active payload.

| Source operation | DynLex pattern |
| --- | --- |
| Line direction/length | `the cad direction of LINE`; `the cad length of LINE` |
| Arc/EllipseArc sweep | `the cad sweep of ARC` |
| Extent | `the cad extent of CURVE`; `the cad bounded/forward/infinite extent2` |
| Extent membership | `EXTENT holds cad parameter T` |
| Shape predicates | `CURVE is cad straight`; `CURVE is cad closed` |
| Straight representation | `the cad ray of CURVE` → `cad curve2 ray result` with `valid` and `value` |
| Point/inverse | `the cad point on CURVE at T`; `the cad parameter on CURVE nearest POINT` |
| Rectangle-side recognition | `the cad rectangle side of CURVE with bounds [[XMIN,XMAX],[YMIN,YMAX]]` → `cad curve2 index result` |
| Segment decomposition/count | `the cad segments of CURVE`; `the cad segment count of CURVE` |
| Density sampling | `the cad samples of CURVE with density DENSITY` |
| Angular sampling | `the cad angular samples of CURVE within ANGLE` |
| Derivative | `the cad tangent of CURVE at T` |
| Equality/copy | `LEFT = RIGHT`, ordinary assignment, `the cad clone of CURVE` |
| Explicit release/reset | `free cad curve CURVE` |

The point, inverse, segment-count and angular patterns also accept a
`cad polyline2` directly when importing `curve2.dl`.
The ordinary evaluator protocol `the cad curve point/tangent of CURVE at T`
returns `cad vector2`; use the planar angular pattern above for sampling.

## Bulges and analytic polyline ranges

- `a cad polyline vertex2 at POINT with bulge BULGE` and
  `a straight cad polyline vertex2 at POINT` produce value records.
- `the cad bulge arc2 from START to END with bulge BULGE` and
  `the cad segment arc of POLYLINE at index INDEX` return
  `cad bulge arc2 result` (`valid`, `value`).
- `cad bulge arc2` exposes `centre`, `radius`, `startAngle`,
  `endAngle`, `sweep`. Point evaluation follows its signed sweep and
  does not clamp. Its angular sampling returns all endpoints.
- `the cad range of POLYLINE from FROM to TO` returns
  `cad polyline range2 result` (`valid`, `value`).
  The managed value contains an open `polyline` and borrowed `segments`.
  Each `cad polyline range segment2` contains `sourceIndex`, `from`, `to`.
- `the cad interpolation of SEGMENT from START to END` returns a fixed
  binary64 array of two interpolated quantities.
- `the cad clone of POLYLINE/RANGE` duplicates storage.
  `free cad polyline POLYLINE` and `free cad polyline range RANGE` reset
  the caller's value.

Closed ranges may cross the seam once. Straight segments preserve the
source's weighted-sum formula in range extraction; general point evaluation
uses the original lerp formula. Partial circular segments keep signed
analytic bulges. Nonfinite geometry and collapsed curved segments refuse
range extraction. The full source thresholds are retained.

## Ownership

Polyline constructors borrow and copy input vertices. Curve constructors
borrow and retain managed payloads. Ordinary copies share immutable backing
storage with paired retain/release hooks; they are safe owning values.
Deep clone duplicates every owned list. Scalar primitive payloads copy by value.

The polyline storage wrapper owns exactly one raw list. Polyline/range
records use ordinary automatic member lifetimes, including the nested
polyline in a range. Raw `vertices` and `segments` fields are read-only
borrowed views: never free or mutate them; retain the managed owner for
their entire use. Returning a raw pointer does not retain its owner.

Curve2 uses default member lifecycle traversal. Its inactive polyline and
NURBS fields are null-safe zero values; a whole-result type witness supplies
the concrete record type without executing witness allocations. Explicit
reset releases the caller's reference and leaves other retained copies valid.
A reset polyline/range is a lifecycle-safe empty storage value, not an
evaluatable geometry object; reconstruct it before reading its raw lists.

Returned sample lists and lists from segment decomposition have manual
ownership: free each list once. List operations retain/release managed curve
elements. Invalid result records still receive automatic cleanup.

## Dependency coverage

These exact production pieces are included in `curve2.dl` because
`Curve::tessellate` requires them:

| Original module | Included production pieces | Still outside this port |
| --- | --- | --- |
| `geom2d/arclength.rs` | Complete `Curve::tangent_at` (65–113), all eight variants | Length, partial length, inverse distance, point at distance, integration and polyline length helpers |
| `geom2d/deviation.rs` | `MAX_DEPTH`; complete `Curve::tessellate_angle`, `polyline_angle`, `angular_steps`, `angular_curve`, `subdivide_angle`, `sample_uniformly`, `span_boundaries` | Distance/deviation sampling and its circle/arc/ellipse/NURBS helpers |

This is not a whole-module verification claim for arclength or deviation.
A subsequent full port should reuse or extract these implementations instead
of defining overlapping patterns.

The exact source depth limit is 16, with nine tangent samples per interval;
NURBS starts at minimum depth two, ellipse at zero. No degree or coordinate
dimension cap was introduced. Clockwise bulges become counterclockwise Arc
variants with reversed endpoints during decomposition, as in the source;
the polyline tangent consequently preserves that source convention.
Ellipse sweep adds one turn to a nonpositive raw difference, without further
normalization. Straight-line sampling returns stored endpoints exactly,
including at cancellation-prone coordinates.

## Verification

Verified on 2026-09-13: all 24 original Rust tests, all 24 translated source
groups at O0/O2, the ownership fixture at O0/O2, and 12 expected compile
refusals passed. The complete differential passed 722 runtime cases and
1,126,844 numeric Rust comparisons with exact O0/O2 parity. The ordinary
required runner passed all six fixture/mode combinations with matching output
and no unexpected diagnostics.

From the repository root, with Python, Rust and the native compiler available:

```text
python -B tests/cadkernel/curve2/verify.py --source /path/to/pinned/cadkernel
python -B tests/cadkernel/verify.py --filter cadkernel_curve2*
python -B tests/cadkernel/verify.py --filter cadkernel_polyline2
```

`--source` is required and checked against the pinned revision and unchanged
source files. The compiler defaults to `build/dynlex.exe` on Windows and
`build/dynlex` elsewhere; `--compiler PATH` overrides it. All native launches
inherit the shared unattended-process error mode, and all `.out` execution
uses Python subprocess. The runner does not rebuild the compiler.

The reference compiles unchanged curve, polyline and dependency modules.
The Ellipse declaration/implementation is copied verbatim from the pinned
module by balanced-brace extraction; no reference geometry is rewritten.
All 24 original tests are executed and every translated test name is checked
in source order.

Runtime probes cover all variants, extents, projections, rectangle sides,
decomposition, bulge signs and degeneracies, exact range/provenance,
nonfinite inputs, boundary thresholds, weighted NURBS through degree 24,
sampling policies, and the 65,537-point depth-limit result. Ownership covers
input-copy independence, shared copies, deep clones, list removal, nested
result lifetime, null inactive owners and 1,000 repeated ownership cycles.
Six refusal fixtures check coordinate type, bounds dimension/precision,
vertex payload, variant payload, and local helper visibility.

Numerical Rust comparisons use relative tolerance 3e-12 for finite scalars.
Coordinates additionally permit 16 ULPs of the geometry magnitude: the host's
native math libraries differ slightly near trigonometric zeroes.
`math_probe.rs` and `math_probe.dl` isolate this independently of geometry:
on this Windows GNU host, both the intrinsic and direct C `sin(-TAU)`
produce approximately 2.44921270764475452e-16 in the Rust executable and
2.4492935982947064e-16 in the DynLex executable. Both LLVM outputs use the
ordinary sine intrinsic/direct function. Exact stored line endpoints bypass
that tolerance. O0/O2 comparisons remain exact, including signed zero;
NaN classification must agree in all comparisons.

Validation here is native Windows. Other targets, allocation-failure
injection and sanitizer-based leak proof are not covered. Sampling requires
resources proportional to its result, just as the source; unbounded resource
requests are not exercised. Standard list capacity and target integer
representation remain the library's limits.

## Files

- Production: `lib/cadkernel/curve2.dl`, `lib/cadkernel/polyline2.dl`.
- Required fixtures: `tests/required/cadkernel_curve2/{main.dl,expected.txt}`,
  `tests/required/cadkernel_polyline2/{main.dl,expected.txt}`,
  `tests/required/cadkernel_curve2_ownership/{main.dl,expected.txt}`.
- Shared tests: `tests/cadkernel/curve2/{README.md,verify.py,reference.rs,probe.dl,check_fixture.py}`.
- Refusal fixtures: `tests/cadkernel/curve2/{wrong_point.dl,wrong_bounds.dl,wrong_precision.dl,wrong_vertices.dl,wrong_payload.dl,local_visibility.dl}`.
- Isolated native math diagnostics: `tests/cadkernel/curve2/{math_probe.rs,math_probe.dl}`.
- Polyline entry point: `tests/cadkernel/polyline2/{README.md,verify.py}`.
