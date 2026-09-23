# Analytic planar bounds

`lib/cadkernel/bounds2.dl` ports all of `src/geom2d/bounds.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` (130 production lines and three tests).

`the cad analytic bounds of curves` borrows a pointer to a list of `cad curve2`
values. Its value result is `cad bounds2`: `valid`, `low` and `high`. The last
two fields are binary64 `cad vector2` values. The result contains no allocation
or borrowed storage; neither it nor the function takes ownership of the input.

Lines, circles, directed arcs, rotated ellipse arcs and bulged polylines use
analytic extrema. Supplied ellipse axes are not normalized. Negative polyline
bulges reverse the interval before finding the circular extrema. No sampling
approximation is used.

An empty collection, a collection containing only empty polylines, nonfinite
geometry or invalid conic parameters returns `valid = false`. Empty polylines
inside a nonempty collection contribute no points. Rays, infinite lines and
NURBS also return no bounds, matching the source's deliberately restricted
analytic operation. Invalid results contain zero vectors, whose values are
not meaningful bounds.

## Verification

From the repository root:

```text
python -B tests/cadkernel/bounds2/verify.py --source PATH_TO_PINNED_CADKERNEL
python -B tests/cadkernel/verify.py --filter cadkernel_bounds2
```

The differential runner validates the source revision and unchanged source
files, includes the original Rust implementation, and executes its three
tests. The native required fixture translates those three groups. Probes pass
runtime input to both implementations, including all eight curve kinds,
signed bulges, skew axes, directed and full-turn intervals, degenerate and
nonfinite inputs, mixed collections and deterministic randomized geometry.

The complete Windows run passed 1,172 cases and 7,152 scalar comparisons with
Rust 1.91.1 for the MSVC target. Native O0 and O2 outputs agreed exactly,
including signed zero. Rust comparison uses relative tolerance `3e-12` for
nonzero finite coordinates and exact validity/zero comparisons. Compiler
SHA-256: `91d137351179e189166b3b39b038e09896ad837e346b1600f3a33d10a392eec9`.
The report is `build/bounds2-u0g5rdfg/report.json`; a new run records its own
compiler hash, input corpus and results in a separate `build/bounds2-*` folder.
`--limit N` runs a subset and is not evidence of complete verification.
