# B-rep geometry

Native implementation of the complete `src/brep/geometry.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`: 595 production lines and twelve
source tests. The implementation is `lib/cadkernel/brep_geometry.dl`.

## Public geometry

Construct analytic payloads with the typed `a cad cylinder`, `a cad cone`,
`a cad sphere`, `a cad torus`, `a cad line3`, `a cad circle3`, `a cad ellipse3`
and `a cad planar spline3` constructor phrases in that file. Convert the
payload using `the cad surface of payload` or `the cad edge curve3 of payload`.
Surfaces accept planes and managed tensor NURBS as well; edge curves accept
managed spatial NURBS. Construct tagged values through these factories.

| Source operation | Native pattern |
| --- | --- |
| Surface point | `the cad point on surface at parameters u and v` |
| Surface tangents | `the cad tangents of surface at parameters u and v` |
| Surface normal | `the cad normal of surface at parameters u and v` |
| Analytic frame | `the cad frame of surface` |
| Inverse parameters | `the cad parameters on surface nearest point` |
| Ray parameters | `the cad ray hits of surface from origin along direction` |
| Signed distance | `the cad distance from surface to point` |
| Containment | `surface contains cad point point within tolerance` |
| Edge point / tangent | `the cad point on edge at t`, `the cad tangent of edge at t` |
| Edge inverse parameter | `the cad parameter on edge nearest point` |
| Explicit deep copy | `the cad clone of surface` or `the cad clone of edge` |
| Variant equality | `first = second` |

Optional results expose `valid`. Ray results expose `valid` and `values`;
`valid=true` with an empty list means no roots, whereas `valid=false` means
the operation is unsupported or its required frame is unusable. Torus and
NURBS ray queries return the latter, preserving the source distinction.
NURBS surfaces provide no analytic frame or inverse parameter operation.
Distance sentinels and nonfinite/degenerate behavior follow the source.

Planar spline edges evaluate their underlying 2D NURBS in normalized
parameters. Spatial NURBS edges and tensor surfaces use knot parameters.
Analytic frames are retained as supplied, including nonunit, skewed and
degenerate axes; these constructors do not silently orthonormalize them.
Ray parameters are sorted in IEEE total order and deduplicated with numeric
equality, including the source's signed-zero and NaN behavior. Negative ray
parameters are retained.

## Ownership

Analytic payloads are ordinary value records. Tagged wrappers use the normal
field lifecycle for their managed spline payloads. Ordinary copies share
retained storage; explicit clone deep-copies the active spline and its control
rows. Inactive spline fields are null-safe zero values and cause no allocation.
Private type witnesses supply concrete field types without runtime evaluation.
Equality compares only the active payload, preserving nonreflexive NaN equality.

Ray result values have shared managed lifetime. Their raw `values` pointer is
borrowed while a result copy remains alive; do not free it separately or mutate
its owner counter. No manual release is required for either valid or invalid
results. The ownership fixture exercises returned wrappers, copies in lists,
variant replacement, independent clones and final reference counts.

## Reproduction

From the repository root with the compiler built:

```sh
python -B tests/cadkernel/verify.py --filter 'cadkernel_brep_geometry*'
python -B tests/cadkernel/brep_geometry/verify.py --source /path/to/pinned/cadkernel
```

The second command checks the exact source revision and that its eight included
modules are unmodified. It compiles the original Rust source and all twelve
source tests, the three required fixtures and the native probe at O0 and O2.
No Rust implementation is shipped as the native geometry implementation.

The default reference on Windows is the installed
`stable-x86_64-pc-windows-msvc` toolchain, which uses the same native C math
behavior as the DynLex Windows executable. `--rust-toolchain` selects another
installed toolchain. On other systems, the default is the native `rustc`.
The verification record includes the actual Rust version and compiler hash;
this does not establish portability to other targets.

Each run writes its inputs and result under a fresh `build/brep-geometry-*`
directory. `--limit N` performs an explicitly partial run and records
`complete=false` unless all inputs ran. Native processes use the shared
Windows error-dialog guard with explicit timeouts and exit checks.

## Evidence

Full differential: **2,561 inputs and 103,684 Rust scalar comparisons passed**,
with exact O0/O2 parity. Inputs cover all six surface and five edge variants,
ordinary and nonunit frames, nonunit knot domains, random points, tiny/huge
values, signed zeros, infinities, NaNs and degenerate quadratics. The twelve
original tests and their translated assertions pass. Additional fixtures
exercise spindle-torus inverse selection, signed distances, unsupported
results, tangent/normal fields and ownership.

Rust numeric comparisons use relative tolerance `3e-12` with zero absolute
tolerance. Discrete fields and result sizes are exact. A zero never matches
a nonzero, zero signs must match, infinity signs must match, and NaNs compare
by classification. O0/O2 comparisons use exact numeric equality with those
same signed-zero/nonfinite rules; NaN payload identity is not asserted.

The full run used Rust 1.91.1, LLVM 21.1.2, target
`x86_64-pc-windows-msvc`, and DynLex compiler SHA-256
`91d137351179e189166b3b39b038e09896ad837e346b1600f3a33d10a392eec9`.
An [independent source review](../../../docs/reviews/2026-09-13-cadkernel-brep-geometry.md)
adds 191 public ray cases, managed-variant checks and LLVM inspection of the
analytic constructors. It found no actionable correctness defect.

This verifies the geometry module. Connected topology, B-rep operations and
the complete CAD port have separate coverage obligations.
