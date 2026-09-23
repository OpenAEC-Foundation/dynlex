# Planar arrangements and labelled boundaries

`lib/cadkernel/arrangement2.dl` ports all 638 production lines of
`geom2d/arrangement.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
The native fixture preserves all 18 original test groups. Both original Rust
tests and native groups pass at O0/O2.

Public operations find bounded point rings, find bounded labelled edge rings,
compute stable signed area, and report segment crossings as none, point or
overlap. Crossing kinds are 0/1/2; point parameters repeat in the start/end
fields. A labelled edge carries a full unsigned 64-bit source tag. The first
inserted coincident edge retains its tag. Face orientation is counter-clockwise;
face order and the starting vertex of a ring are not part of the contract.
Nested rings are separate bounded faces, not an assembled polygon with holes.

The split stage retains the less-congested-axis sweep and exact total ordering
of floating parameters. It preserves the original distinction between tagged
and untagged parametric distance rejection. The graph uses spatial buckets,
saturating signed-64-bit cell coordinates, sorted angular adjacency and bounded
directed-edge traversal. The 1e-12 relative parallel and 1e-10 area thresholds
remain unchanged. Area uses a triangle fan around the ring's first point.

Inputs are borrowed. Returned `cad arrangement regions2` values own both levels
of ring lists. Assignments retain immutable storage, including across local
returns and resets. Views must not be freed or modified. Explicit `the cad
clone of regions` allocates independent lists, including for an empty result.
The ownership fixture also checks extreme tags and reversed duplicate edges.

```text
python tests/cadkernel/arrangement2/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Executed: **597 cases / 139,260 scalar and integer comparisons**, with each
native optimization mode checked against the corresponding unchanged Rust
mode. Evidence: `build/arrangement2-checks/summary.json` and
`build/arrangement2-differential.log`. Cases cover split crossings, collinear
overlap, endpoint gaps, spurs, nested/disconnected loops, duplicates, signed-zero
and non-finite inputs, large and tiny scales, saturating cells, survey
coordinates, grids through 400 bounded cells and seeded random graphs.

Comparison canonicalizes ring rotation and face order, preserving direction
and every source tag. Integer fields are parsed without floating conversion.
Finite coordinates allow relative error 3e-12 and 16 ULPs of the input scale;
parameters and areas have no absolute allowance. Signed zeros, counts and
non-finite classifications are strict.

One demonstrated source exception prevents an unconditional optimization-parity
claim: for `(0,0)->(10,1e308)` against `(5,-1)->(5,1)`, the original overlap
`aStart` is `-0` at O0 and `+0` at O2. DynLex reproduces each sign in the matching
mode. The runner records this difference and admits a sign difference only for
equal-zero overlap bounds when both Rust executions demonstrate it. Every
other tested field has exact native and Rust optimization parity.

`pair_index.dl` is an internal native ordered-i64-pair index shared by the
spatial buckets and edge tables. It uses pre-sized open addressing and bounded
probing. Missing keys return -1; stored values are nonnegative native indices.
Copies share retained mutable table storage. Graph construction rejects more
than 268,435,456 pieces before endpoint/directed-edge size multiplication. This
is a representability limit, not a claim that such an allocation is practical
or has been exercised. Native list/allocation checks remain active.

The two arrangement and six pair-index required wrappers pass **16 O0/O2
checks**, including expected failures for negative/overflowing capacity,
negative values, insertion into a full table and use of a reset handle.
Evidence: `build/arrangement2-pair-index-required.log`. These were added after
the 633-check full compiler run and are not included in that total.
