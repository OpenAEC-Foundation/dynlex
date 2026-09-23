# Constrained parameter-domain triangulation

`lib/cadkernel/constrained2.dl` ports `src/geom2d/constrained.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The source file has 171 physical
lines and no direct unit tests. Its observable contract is verified against an
unchanged copy of the Rust source in O0 and O2.

The native mesh normalizes every coordinate to the source binary64 epsilon
grid, keeps the first parameters associated with a normalized vertex, rejects
non-finite and zero-range domains, and applies even-odd ring containment.
Boundary construction refuses proper crossings while retaining repeated and
touching constraints accepted by the source. `insert` reports whether a vertex
was new. `constrain` inserts endpoints, splits existing constraints at every
crossing, and reports the number of newly created vertices.

Triangulation reuses the native ring triangulator, planar arrangement and
managed list storage. Arrangement cells make internal constraints explicit on
both adjacent triangles. Temporary hole-bridge vertices are removed by
retriangulating their cavities, so the emitted source vertices, triangle count,
covered area and constrained edge multiset agree with the reference. Free
vertices split their complete incident cavity, including points that land on a
boundary, internal edge or former hole bridge. Triangle order and a legal
choice of unconstrained diagonal are intentionally not compared because the
source exposes neither as part of its contract.

The differential corpus covers 68 fixed and seeded-random scenarios and 1,842
semantic comparisons. It includes holes, nested islands, disconnected and
duplicate rings, survey and subnormal coordinates, self-crossing refusals,
duplicate and non-finite insertions, boundary/outer/duplicate constraints,
crossing constraints and free vertices on constrained lines. The required
fixtures pass in O0/O2 and include 64 repeated return/copy/mutate/release
cycles for shared managed storage.

Run the verifier with:

```powershell
python -B tests/cadkernel/constrained2/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/constrained2-checks
```

Recorded evidence: `build/constrained2-final/summary.json`.
