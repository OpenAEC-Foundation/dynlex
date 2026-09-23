# Variable-width polyline bands

`lib/cadkernel/band2.dl` ports the complete 502-line `geom2d/band.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The source contains no unit tests.
Six independent native groups check a straight band, linear width changes,
invalid widths, intersected corner rails, circular source stations and
collapsed source geometry.

`the cad band boundary of source with widths widths within angle angle` borrows
a planar polyline and a list of binary64 pairs. Each pair gives the full width
at the beginning and end of one source segment. Closed polylines require one
pair per vertex; open ones require one fewer. Negative/non-finite widths,
mismatched counts, fewer than two vertices or all widths at most 1e-12 return
an allocated empty default. Invalid sampling angles retain the existing
source sampling-angle fallback.

The result contains `edges`, `stationPieces` and analytic `sourceLength`.
Boundary and station records retain endpoints, source segment identity,
whole-source distances and distances within the original segment. Boundary
distances project each endpoint onto its sampled source chord, retaining the
analytic station interval. Source station order is preserved. Boundary edge
order is not a geometric contract.

The implementation preserves sampled circular tangents, linearly interpolated
widths, width-continuity checks, curvature/width-gradient corner intersections,
end caps, local normalization and both arrangement passes. Interior edges
shared by bounded faces are removed by exact endpoint bit keys, with both zero
signs normalized. This is the source's bounded-region cleanup; it does not
introduce a new winding rule for closed-path holes. All spatial lookup and
edge-counting code is native DynLex.

Result views are read-only and must not be freed separately. Assignments retain
both owned lists; explicit clone duplicates them. Edge and station records are
scalar values. Ownership tests cover local returns, released source/width
lists, retained copies, reset handles, independent clones and empty defaults.

```text
python tests/cadkernel/band2/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Executed: **441 cases / 200,826 scalar and integer comparisons**, six native
groups, ownership checks and two typed input refusals, all at O0/O2. Six Python
tests also verify that the comparator rejects wrong geometry, orientation,
source identity, distances and reuse of a matched edge. Evidence:
`build/band2-final-checks/summary.json`, `build/band2-final-differential.log`
and the four required-wrapper checks in `build/band2-required.log`.

Cases cover open/closed paths, variable/discontinuous widths, degenerate and
non-finite inputs, positive/negative arc bulges, sharp/reflex corners, survey
coordinates and seeded random self-crossing chains. Native O0/O2 outputs have
exact parity after canonicalizing edge order.

The original Rust function uses unordered map iteration between arrangement
passes. Repeated execution of the same binary can therefore choose a slightly
different welded representative, changing both coordinates and projected
distances. Eight runs per mode of the tiny-start-width example each produced
two distinct normalized results (`build/band2-checks/order-repeat-*.txt`).
The final summary records the cases that differed between Rust runs; exact
Rust optimization parity is not claimed.

Comparison requires a one-to-one match of every oriented edge and all its
metadata. Finite coordinates allow relative error 3e-12 and 16 ULPs of the
input/reference-output scale, including distant miter points. Boundary station
allowances propagate this coordinate rounding through the source chord
projection and analytic station span, plus 16 ULPs of station arithmetic.
Source station records and total source length keep their stricter scalar
checks. Counts, source indices, zero signs and non-finite classifications are
checked directly. No source cases are skipped to hide ordering differences.

Cleanup counts reject more than 268,435,456 directed boundary entries before
index capacity multiplication. Existing native list, allocation and sampling
limits also apply. Maximum representable allocations have not been exercised.
Width pairs with the wrong fixed-array length or 32-bit components are rejected
at compilation. The required wrappers were added after the 633-check full
compiler run and are not included in that total.
