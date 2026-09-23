# Geometric snap candidates

`lib/cadkernel/snap2.dl` ports all 306 production lines of `geom2d/snap.rs`
at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. All sixteen source test groups
are translated, including the spline perpendicularity and survey-coordinate
assertions. `cad snap kind` retains six distinct typed categories.

The four public operations return characteristic points, perpendicular feet,
tangent points and the nearest snap record. They preserve curve extents,
candidate order, polyline segment parameters, circular/elliptical quadrants
and the source's boundary tolerances. Nearest candidates retain the source's
Perpendicular category. Picking a visible curve or prioritizing candidates in
screen pixels belongs to an application, outside this geometric API.

Circle/line operations use the source's closed forms. For other curves,
perpendicular candidates use its 128 finite-difference brackets and 60
bisection steps. This preserves the source algorithm; it does not prove that
every stationary point on every possible curve is found. No rendered
polyline substitutes for evaluation of the original curve.

Result lists are owned by the caller and freed once. Candidate records are
scalar values and copied records outlive their source curve or list. Inputs
are borrowed and unchanged. Equality/clone preserve all fields.

```text
python tests/cadkernel/snap2/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Executed: **754 cases / 30,170 scalar comparisons**, all sixteen original Rust
tests, native translated groups and ownership fixtures pass O0/O2. See
`build/snap2-checks/summary.json`, `build/snap2-differential.log` and
`build/snap2-ownership.log`. Both native and Rust optimization parity are
exact. Counts, categories and flags are exact, signed-zero/non-finite behavior
is retained, and finite values use relative tolerance 3e-12. Coordinates alone
allow 16 ULPs at the input geometry's scale; parameters have no blanket
absolute allowance. The reference imports the complete unchanged Rust module.

Cases cover all eight curve kinds, every operation, singular curves,
non-finite inputs, signed radii, circular and elliptical partial/full spans,
bulged/closed/empty polylines, extent/tangency boundaries and random curves.
