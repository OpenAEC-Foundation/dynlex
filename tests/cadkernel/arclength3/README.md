# Spatial arc-length traversal

Full native port of cadkernel `src/space/arclength.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, licensed MPL-2.0.
Production implementation: `lib/cadkernel/arclength3.dl`.
Dependencies are the native CAD vectors, NURBS3 and polygon chain measurement.
The Rust executable is a test reference, never a production dependency.

## API

| Source operation | DynLex pattern |
| --- | --- |
| `from_polyline` | `the cad arclength curve3 through POINTS with closure CLOSED` |
| `from_nurbs` | `the cad arclength curve3 of SPLINE` |
| `length` | `the cad length of CURVE` |
| `is_closed` | `CURVE is cad closed` |
| `point_at` | `the cad point on CURVE at PARAMETER` |
| `tangent_at` | `the cad tangent of CURVE at PARAMETER` |
| `parameter_at_distance` | `the cad parameter on CURVE at distance DISTANCE` |
| `point_at_distance` | `the cad point on CURVE at distance DISTANCE` |
| Derived `Clone` | `the cad clone of CURVE` |
| Early reference release | `free cad arclength VARIABLE` |

Constructors return a `cad arclength result` with `valid` and `value`.
Query only valid results. Points use `cad vector3`; coordinates and arithmetic
are binary64. The private source variant uses an integer tag and an actual typed
`cad nurbs curve3` field, with no erased geometry pointer or external callback.

Polyline construction removes only adjacent exactly equal points and optionally
appends the first point. Stations use segment index divided by segment count,
with exact source-order accumulated segment lengths. The normalized point
parameter is therefore not generally proportional to travelled distance.

Tangents are not unit vectors: the polyline result is the selected segment's
vector, and the spline result is its analytic derivative in knot coordinates.
It is not multiplied by the knot-domain span. NaN point parameters retain Rust's
NaN interpolation behavior; a polyline NaN tangent parameter selects segment zero.
NaN or nonpositive distance returns parameter zero; distance at or beyond total
length returns one. Infinity and large finite parameters follow the source clamp.

The segment helper implements `(floor(scaled) as usize).min(last)` by checking
NaN/negative input and the last-index bound before casting. Standard list lengths
use DynLex's signed integer type. A remaining positive value below the last index
fits that type; no out-of-range or NaN native float-to-integer cast is executed.

## Ownership

Constructors borrow input lists and allocate independent contents. The NURBS
constructor normalizes weights by their maximum, rebuilds with strict validation
and preserves periodicity without changing the supplied curve.

Ordinary copies retain shared, read-only storage. The outer record has default
field lifecycle, retaining both its managed points/stations owner and its typed
managed NURBS field. The storage's paired hooks release both lists on the final
reference. Invalid results also release their allocations safely.

The `storage`, `points`, `stations`, `spline` and owner fields describe internal
storage, not a mutation API. Raw list views are borrowed: do not free or modify
them, and keep an owning value alive while using them. `the cad clone of ...`
duplicates point/station lists and every NURBS control, knot and weight list.
Resetting one variable releases only its reference; repeated reset is safe.

## Numerical contract and limits

Spline stations retain the five-point Gauss–Legendre nodes, weights and summation
order. Each distinct nonempty knot span is refined with
`max(control_polygon_length * 1e-10, 1e-12) * span_width`; subdivision halves the
tolerance, refuses nonfinite quadrature and refuses further refinement at depth
20. Accepted intervals append their midpoint and endpoint stations. Distance
inversion retains the source's 44 bisection iterations and quadrature calls.

These are source-parity guarantees, not a stronger integration error proof.
For the line from `(0,0,0)` to `(0,0,5)` with degree one, knots `[0,0,1,1]` and
weights `[1e-12,1]`, the pinned source accepts length approximately
`6.049980398562509e-10`, although the geometric length is 5. The quadrature misses
the narrow endpoint speed peak. Weights `[1e-6,1]` and `[1e-8,1]` instead exhaust
adaptive refinement and are refused. The port preserves these outcomes.
Squared-length underflow/overflow and normalization underflow also preserve
source refusal behavior; this module does not establish a later robustness gate.

## Verification

From the repository root:

```text
python -B tests/cadkernel/arclength3/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Optional flags: `--compiler PATH`, `--filter SUBSTRING`, `--reference-only`.
The default runs every case. A filter selecting nothing fails. Reference-only
output is explicitly labelled and does not claim native verification.

The runner verifies the Git revision and unchanged source dependencies, includes
those original Rust modules without editing them, and runs the **one original
arc-length test**. It then compiles native probes and ownership fixtures at O0/O2.
Child processes use `scripts/process_error_mode.py`, explicit timeouts, preserved
exit statuses, and `.exe` filenames on Windows. Artifacts and numerical results
are written only under `build/cadkernel-arclength3-checks/`.

Verified on Windows with the current compiler build:

- 207 differential cases and 6,014 scalar comparisons against pinned Rust.
- 144 independent assertions, including the original 3–4 polyline, a straight
  spline, a closed triangle and a rational quarter-circle.
- Exact Rust comparison for every polyline output, including signed zero;
  NaN classifications are compared explicitly. Spline comparisons use relative
  tolerance `2e-12` and absolute tolerance `2e-13`. All O0/O2 outputs agree exactly.
- Two ownership fixture runs: input independence, exact polyline stations,
  deep clone buffer identity, return/assignment copies, repeated resets,
  balanced managed list-element references and 100 invalid-result lifetimes.
- Four expected compile rejections: wrong point dimension and local helper
  visibility, each at O0/O2.

Cases include repeated points, zero-length stations from numerical underflow,
nonfinite coordinates/parameters/distances, overflow, unequal segment lengths,
periodicity, knot spans, weight normalization and underflow, adaptive refusal,
and deterministic random spatial polylines and rational splines.
Required fixtures reuse the same probes: `cadkernel_arclength3` checks the
original 3–4 polyline with exact independent expected output;
`cadkernel_arclength3_ownership` covers both curve variants and their lifetimes;
`cadkernel_arclength3_overflow` checks nonfinite length refusal; and
`cadkernel_arclength3_refusal` checks the pinned adaptive-depth refusal.
Run these through `python -B tests/cadkernel/verify.py --filter "cadkernel_arclength3*"`.
All four required fixtures passed at O0/O2: eight fixture-mode combinations,
zero failures and zero skips. The full differential runner also passed with
the compiler's pure-flex execution correction included.
Required integration and the complete main-crate port have separate verification
gates; these module results do not establish full-port completion.
