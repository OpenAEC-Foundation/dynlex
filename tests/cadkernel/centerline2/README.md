# Centre lines between finite segments

`lib/cadkernel/centerline2.dl` ports all 126 source lines of
`geom2d/centerline.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
The source module contains no unit tests. Six independent native test groups
cover parallel extents, opposite input directions, selected endpoint identity,
both bisected sectors, over-trimming/degeneracy and copied base endpoints.

The public operation takes two line records, two pick positions and independent
start/end extensions. Its explicit `valid/value` result preserves both the
extended endpoints and the unextended `baseStart/baseEnd`. These records own
no external storage. Copies, equality and explicit clone preserve all fields.
Degenerate directions, collapsed spans and reversed final spans retain the
source's 1e-10 refusals. Negative extensions trim the natural span. The source's
IEEE behavior for non-finite values is preserved without a new validation rule.

```text
python tests/cadkernel/centerline2/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Executed: **653 cases / 10,432 scalar comparisons**, plus all six native test
groups at O0/O2. See `build/centerline2-checks/summary.json` and
`build/centerline2-differential.log`. The numerical reference imports the
unchanged complete source. Reciprocal multiplication, ordered summation and
endpoint flips preserve source arithmetic. Native and Rust optimization parity
are each exact. Flags, signed zeros and non-finite values are checked; finite
coordinates use relative tolerance 3e-12 with a 16-ULP input-scale allowance.

Cases include all picked sectors, reversed source directions, near-parallel
determinants, independent negative/positive extensions, EPSILON boundaries,
non-finite inputs, survey coordinates and seeded random lines.
