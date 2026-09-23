# Planar tessellation by deviation

Native port of `src/geom2d/deviation.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. Angular sampling and shared span
helpers remain in `curve2.dl`; `deviation2.dl` implements deviation sampling.

`the cad samples of shape within deviation tolerance` borrows a `cad curve2`
and returns a caller-owned raw list of `cad vector2`. Free that list once.
Each call owns a separate output list. No output element borrows curve storage.

Circular and elliptic segment counts retain the source's 8..16384 limits.
NURBS subdivision retains depth 16 and two forced subdivision levels per span.
These caps can leave more deviation than requested. Straight and unbounded
curves produce their two endpoint samples; a polyline samples each segment and
joins exact shared vertices. Invalid tolerances and nonfinite coordinates
follow the source behavior rather than introducing a new validation policy.

Verification on compiler SHA-256
`1d89c3a8a361349105e797c31eccde8b1e84c9865953aeaadb7684f322bb714b`:

- All 16 original Rust tests and all 16 translated groups passed at O0/O2.
- 431 differential cases passed, with 7,517,464 native-to-Rust comparisons.
- Rust and native results each have exact O0/O2 parity, including signed zero
  and the classification of nonfinite results.
- Shared measurement ownership tests passed at O0/O2. They check balanced
  reference counts, unchanged inputs, independent sample lists and output
  lifetime after releasing the curve.

Counts and straight-line samples are exact comparisons. Other coordinates
use relative tolerance `3e-12` and an allowance of 16 ULPs at geometry scale.
Cases include all eight variants, caps, tiny and extreme bulges, nonfinite
inputs, repeated vertices, NURBS boundaries and degrees, and seeded random
curves. The Windows reference uses MSVC Rust 1.91.1 to match the native math
target. Sampling caps and source numerical limitations remain visible.

Run from the repository root:

```text
python tests/cadkernel/deviation2/verify.py --source <pinned-cadkernel-checkout>
python tests/cadkernel/verify.py --filter cadkernel_deviation2 --filter cadkernel_measurement2_ownership
```
