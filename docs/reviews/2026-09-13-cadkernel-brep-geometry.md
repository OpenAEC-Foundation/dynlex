# Independent B-rep geometry source review

Reviewed 2026-09-13 against complete `src/brep/geometry.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, including the complete test module
after `#[cfg(test)]`. That suffix contains the twelve tests and their `xy`
helper; there is no further production algorithm after the test module.

Native source: `lib/cadkernel/brep_geometry.dl`, SHA256
`320728f23f759ee88e32c34e35c2fdc0d17941bc4366791284ce2016906dc74d`.
No production files were changed by this review.

## Findings

No actionable production correctness defect found in the complete source
comparison, adversarial ray checks, managed-variant probes or analytic-witness
IR review. No missing source algorithm or source-test assertion identified.

## Source coverage

| Source operation | Native implementation | Review outcome |
| --- | --- | --- |
| `Surface::point_at` | `the cad point on cad surface at parameters` | All six variants, cone radius past the apex, torus signed ring, raw supplied frames and NURBS knot parameters match. |
| `Surface::tangents_at` | `the cad tangents of cad surface at parameters` | Plane returns raw axes even for degenerate frames; analytic variants require the normal where the source does. Product/negation order retained. NURBS delegates to knot derivatives. |
| `Surface::normal_at` | `the cad normal of cad surface at parameters` | All six branches present; cylinder radial normalization, sphere center subtraction, cone/torus formulas and NURBS tangent cross product retain source refusal behavior. |
| `Surface::frame` | `the cad frame of cad surface` | All analytic frames returned as value results; NURBS has `valid=false`. No invented spline frame. |
| `Surface::parameters_at` | `the cad parameters on cad surface nearest` | Plane projection and height refusal propagate; sphere pole detection uses `ring <= 0`; spindle/horn torus chooses the smaller forward-map residual using strict `<`, preserving tie/NaN selection. NURBS returns no inverse. |
| `Surface::ray_hits` | `the cad ray hits of cad surface from along` | All four supported analytic branches present; torus/NURBS explicitly unsupported. Negative roots preserved. Sphere uses no frame normal. Degenerate plane/cylinder/cone frames propagate refusal. |
| `Surface::contains`, `distance_to` | `contains cad point within`, `the cad distance from cad surface to` | Signed analytic distances, both cone nappes, nearest torus sheet, and source infinity sentinels retained. |
| `offset_along_normal` | `the cad geometry offset` | Returns the unshifted point when frame normal is absent. |
| `height_above` | Direct reuse of plane distance | Same optional result and supplied frame. No duplicate helper needed. |
| `perpendicular` | `the cad geometry perpendicular` | Same projection subtraction and operation order. |
| `roots` | `the cad geometry roots` | MIN_POSITIVE inclusive linear thresholds, ordered discriminant refusal, stable root pairing, IEEE total sorting and equality-based deduplication match. |
| `axial_distance` | `the cad geometry axial coordinates` | Same normal-based decomposition; absent normal returns `(0, offset.length())`. |
| `Curve3::point_at` | `the cad point on cad edge curve3 at` | All five variants. Planar NURBS uses normalized parameters; spatial NURBS uses knot parameters. |
| `Curve3::tangent_at` | `the cad tangent of cad edge curve3 at` | Analytic derivatives unchanged; planar spline uses normalized derivative, spatial spline uses the source finite-difference knot tangent. |
| `Curve3::parameter_at` | `the cad parameter on cad edge curve3 nearest` | Zero-length line, failed plane projection, ellipse rescaling and both spline inverse calls match. |
| Derived `Clone` / `PartialEq` for the variant enums | `the cad clone of`, `=` overloads | Active payload only for semantic equality; explicit clones deep-clone active spline storage; ordinary record copies retain storage. Analytic frames and radii use component equality. |

The signed-integer transform in `cad geometry first precedes second` is the
same transform used by Rust `f64::total_cmp`: negative encodings are XORed with
`i64::MAX`, then signed integers are compared. A negative zero sorts before a
positive zero. A two-element sort plus conditional append reproduces source
sorting and `dedup_by(x == y)`. The DynLex `!=` used here is the standard
inverse-of-equality replacement, so NaNs remain unequal and are not deduped.

## Completed checks

- The unmodified complete Rust module was included with its real plane,
  vector, 2D/3D NURBS, spline and tessellation dependencies. All twelve source
  tests passed using the installed MSVC Rust toolchain.
- Both `tests/cadkernel/brep_geometry` and `brep_geometry_extra` compiled and
  matched their expected output at O0 and O2: four fixture/mode combinations.
  Every assertion in the twelve source tests has a corresponding native
  assertion; splitting compound assertions does not weaken their thresholds.
- 175 adversarial public `ray_hits` cases passed Rust/O0/O2 comparisons.
  Cases cover valid, degenerate and sheared/scaled frames; parallel, tangent,
  inside, zero and negative rays; threshold-adjacent tiny coefficients; huge
  values and infinities; signed quiet/signaling NaN input encodings. Counts
  and validity match exactly, finite values use `3e-12` relative tolerance
  with `1e-300` absolute tolerance, and zero signs match. NaN payload identity
  is not asserted; each returned list is separately checked in IEEE total
  order including its actual NaN encodings.
- Sixteen additional rays starting on a surface passed **exact hexadecimal
  Rust/O0/O2 output equality**, covering positive/negative zero roots for
  planes, cylinders, cones and spheres.

These runs used compiler SHA256
`9682cf3b88de4b6db6c8b82bc3a7ca6517437b81db9dbd7225bcc5d27eb16234`.
Artifacts: `build/review-brep/rays-results.json` and
`build/review-brep/signed-zero-results.json`. `build/review-brep/run.py`
now includes all 191 cases for reproduction. It uses the shared guarded
runner and does not alter source algorithms.

## Managed-variant and witness checks

`build/review-brep/ownership.dl` tests the spatial NURBS edge, planar NURBS
edge and tensor NURBS surface through their actual tagged wrappers. It checks
deep clones, retained list entries, reference counts after variant
replacement, non-unit knot domains, derivatives, inverse queries and NaN/
signed-zero equality. `analytic-only.dl` is for checking null inactive
payloads and the absence of runtime allocations caused by type witnesses.
Both probes passed at O0 and O2 with the rebuilt compiler SHA256
`91d137351179e189166b3b39b038e09896ad837e346b1600f3a33d10a392eec9`.
The three inactive spline owner fields remain null. O0 LLVM inspection shows
zero-initialized inactive payloads, no emitted runtime witness functions, and
no allocation reachable from the two analytic empty-variant constructors.
The print/string path separately allocates its output text; this is unrelated
to geometry witnesses. Artifacts are `ownership-results.json` and
`analytic-only.ll`; reproduce through `build/review-brep/finish.py`.

The managed probes cover ordinary result returns and copies, wrapper copies
stored in lists, dropping/replacing the original active variant, independently
cloned control arrays and surface rows, and returning to a single remaining
reference after container cleanup. Inactive NURBS payloads are never cloned or
evaluated. Analytic equality is nonreflexive for NaN radius and treats signed
zeros as equal. A planar edge on domain `[2,4]` correctly evaluates at
normalized `0.5` and returns derivative `4`; a spatial edge on that domain
evaluates at knot `3` with tangent `2`. The surface likewise uses its non-unit
U/V knot domains. No lifetime or parameter-space mismatch was observed.

The tracked fixtures currently give limited direct coverage to these three
managed wrapper variants. The build probes are available as additional
regression cases for integration; no duplicate production differential
harness was added by this review.

The broad production differential harness remains a separate verification
obligation; this review does not replace it or certify the rest of the crate.

## Integration verification

The broad differential subsequently passed all 2,561 inputs and 103,684 Rust
scalar comparisons, with exact O0/O2 parity. It used the same rebuilt compiler
`91d137351179e189166b3b39b038e09896ad837e346b1600f3a33d10a392eec9`.
The owned-variant probe is retained in
`tests/required/cadkernel_brep_geometry_ownership`. The source and additional
geometry fixtures are `cadkernel_brep_geometry` and
`cadkernel_brep_geometry_extra`; the reference runner remains under
`tests/cadkernel/brep_geometry`.
