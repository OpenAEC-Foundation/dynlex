# Planar arc-length measurement

Native port of `src/geom2d/arclength.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The tangent implementation is shared
with `curve2.dl`; the remaining operations are in `arclength2.dl`.

Public operations are `the cad length of shape`, `the cad length of shape
to parameter parameter`, `the cad parameter on shape at distance distance`, and `the cad
point on shape at distance distance`. Circle, arc and ellipse payloads also
have length overloads. `the cad length of shape from parameter first to
parameter last` measures a signed interval. Scalar inputs and results use binary64.

Lines and circular curves use closed forms. Polylines use cumulative segment
lengths. Ellipses and NURBS retain the source's 32-panel, five-point quadrature
and bounded bisection. Rays and construction lines have infinite length.
Inputs are borrowed. Private cumulative and segment lists are released by
the measuring operation; scalar and vector results have no manual ownership.

Verification on compiler SHA-256
`1d89c3a8a361349105e797c31eccde8b1e84c9865953aeaadb7684f322bb714b`:

- All 14 original Rust tests and all 14 translated groups passed at O0/O2.
- 446 differential cases passed, totaling 6,656 native-to-Rust scalar
  comparisons, with exact parity between optimization modes on each side.
- NaN distance on a nondegenerate polyline preserves the source refusal:
  Rust panics and the native implementation aborts. All four mode/backend
  checks passed separately.
- The shared `cadkernel_measurement2_ownership` fixture passed at O0/O2:
  repeated calls balance owners, inputs remain unchanged, output lists are
  independent, and samples survive release of all source ownership.

The comparison uses relative tolerance `3e-12` for finite scalar calculations.
Point coordinates additionally allow 16 ULPs at the input geometry scale.
NaN/infinity categories, signed zero, discrete results and cross-optimization
parity are checked explicitly. The Windows reference uses MSVC Rust 1.91.1
to match the native math target. The numerical method retains the source's
limitations; these checks do not establish an arbitrary geometric tolerance
for every possible spline.

Run from the repository root:

```text
python tests/cadkernel/arclength2/verify.py --source <pinned-cadkernel-checkout>
python tests/cadkernel/verify.py --filter cadkernel_arclength2 --filter cadkernel_measurement2_ownership
```
