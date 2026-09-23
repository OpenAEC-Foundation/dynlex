# Planar fillets

`lib/cadkernel/fillet2.dl` ports both public operations and every helper in
`src/geom2d/fillet.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
The twenty original test groups are translated without weakening tangency,
short-arc, survey-coordinate or enclosure assertions.

The ray construction expects unit keep-directions and returns `valid/value`.
The general construction accepts lines, rays, infinite lines, circles and
circular arcs. It evaluates the source's ordered offset pairs, preserves
first-wins centre deduplication and selects the correct near/far circular
touch point. Ellipses, polylines and splines have no supported closed-form
offset here and return an empty list, as in the source. General nonpositive
or non-finite radii are rejected. The ray form retains the source's distinct
threshold and IEEE behavior rather than imposing an extra validation policy.

Returned fillet lists are owned by the caller and must be freed once. Their
records contain only scalar values, so a copied record outlives its list.
Input curves are borrowed and unchanged. Separate calls allocate independent
output lists; equality and explicit clone preserve every record field.

```text
python tests/cadkernel/fillet2/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Executed: **565 cases / 17,982 scalar comparisons**, all twenty original Rust
tests, native translated groups and ownership fixtures pass O0/O2.
Artifacts: `build/fillet2-checks/summary.json`, `build/fillet2-differential.log`
and `build/fillet2-ownership.log`. Both native and Rust optimization parity
are exact. Counts, flags, signed zeros and non-finite values are checked;
finite numerical values use relative tolerance 3e-12, with a 16-ULP allowance
at the input geometry's scale for coordinates only. Angles have no blanket
absolute allowance. The unchanged pinned Rust module is the numerical oracle.

Cases include all eight curve kinds in both operand positions, endpoint
extension, nested circles, degenerate directions, near-parallel corners,
tolerance boundaries, non-finite radii and seeded random curve pairs.
