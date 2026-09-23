# Endpoint length changes

`lib/cadkernel/lengthen3.dl` ports all 186 production lines of
`src/space/lengthen.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`,
including the ellipse and polyline operations after the source test module.
All five original Rust test groups have native equivalents.

The API provides delta, total, percentage, delta-angle, total-angle and dynamic
changes. Each operation returns an explicit `valid` result. The endpoint
nearest the pick moves; ties select the end. Dynamic line projection may pass
the fixed endpoint. Arc and ellipse spans retain the source full-turn and
degeneracy refusals. Ellipses reject angular changes; open polylines accept
scalar changes only. Closed, degenerate or non-finite chains are rejected.

Returned polyline vertices preserve source indices, signed bulges and segment
fractions for metadata interpolation. Fractions outside 0..1 describe an
extension. Both source and result list views are read-only borrows. Result
copies retain storage; resetting a result releases that reference. Never free
or mutate its borrowed `vertices` list. Invalid results have the same safe
ownership lifecycle as valid ones.

Verification command (the Rust checkout must match the pinned revision):

```text
python tests/cadkernel/lengthen3/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Executed evidence: `build/lengthen3-final-checks/summary.json` and
`build/lengthen3-final-differential.log`: **1,382 cases / 15,496 scalar
comparisons**, both original Rust test runs and the native source/ownership
fixtures pass at O0/O2. The reference includes the complete unchanged source
module with `geom2d` enabled. Flags, indices and counts match exactly; numerical
comparisons retain signed-zero and non-finite behavior, with relative tolerance
3e-12 and no blanket absolute tolerance. Native and Rust optimization parity
are each exact. Cases include invalid changes, both endpoints, tilted/scaled/
singular frames, full-turn boundaries, signed bulges and seeded random chains.

The ownership fixture additionally checks results outliving local sources,
independent allocations, retained invalid copies, first-end trimming and
extension metadata. `tests/cadkernel/space` checks composition with the other
spatial modules through the single `lib/cadkernel/space.dl` import.
