# B-rep primitive-builder verification

This directory compares every public primitive constructor in the pinned
`src/brep/make.rs` with its native DynLex implementation. Complete body arenas
are emitted for cuboids, faceted solids, circular and elliptical cylinders,
spheres, circular and elliptical cones and frusta, ring/horn/spindle tori,
wedges, pyramids and polygonal frusta.

The current differential corpus passes 585 cases and 325,472 field
comparisons across O0 and O2 with exact optimization parity. It covers invalid
and non-finite dimensions, pointed-top thresholds, survey coordinates, near
circular dispatch and randomized translated geometry. The separate faceted
corpus passes 413 cases and 260,294 comparisons. Required fixtures exercise
the source topology assertions and the extended analytic constructors.

`source-tests.txt` pins all 36 source-test names. All have native coverage
through the make, topology, validation and mesh fixtures. The four integration
tests that tessellate constructed primitives are covered by the complete mesh
suite at both optimization levels.

Run the complete differential corpus with:

```powershell
python -B tests/cadkernel/brep_make/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-make-checks
```
