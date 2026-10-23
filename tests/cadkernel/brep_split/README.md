# B-rep split verification

This directory verifies the native edge and face splitting implementation from
cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

Edge splitting updates vertices, edge intervals, coedge order and exact pcurves
for open and closed analytic edges. Face splitting projects an exact line into
a single planar trim loop, reuses existing corner vertices, inserts shared cut
topology and leaves failed edits atomic by proving the edit on a deep clone.
Closed circles and ellipses can split planar interiors. Closed sections split
periodic bands with one or several boundary loops and replace the artificial
seam of an untrimmed sphere. Periodic cutter and midpoint images use the source
`-2..=2` turn range. More than two boundary landings are sorted along the
cutter and the first strictly interior span is selected. Existing cuts are
refused atomically.

`main.dl` contains eleven groups covering all 23 pinned source tests: edge endpoints, point selection,
closed circular edges, forward and backward coedges, planar cuts, diagonal
corner reuse, repeated cuts, survey coordinates and atomic refusals. The
required fixture mirrors this program and its expected output. `closed.dl` and
its own required wrapper add five groups for planar islands, cylindrical seams,
repeat refusal, successive periodic bands, and axis-aligned and oblique sphere
sections. `source-tests.txt` locks the upstream test inventory in source order.

`verify.py` compares complete emitted body arenas with the pinned Rust source.
The recorded combined run passes 329 cases and 385,550 field comparisons across
O0 and O2, with 310 valid cases and exact native optimization parity. The
compiler SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.

Run the differential check with:

```powershell
python -B tests/cadkernel/brep_split/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-split-checks
```

Unsupported projections and geometrically ambiguous cuts return absence, as in
the pinned source. The public operation remains transactional: a failed edit
does not mutate the supplied body.
