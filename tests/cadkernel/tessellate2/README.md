# Planar arc sampling into world points

`lib/cadkernel/tessellate2.dl` ports all of `src/geom2d/tessellate.rs` at
revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f` (107 production lines,
11 original tests). Its public operations are `the cad arc samples at ...`
and `the cad ellipse samples of ...`, with explicit elevation and density.
`the cad default segments per radian` is 20. The imported core module supplies
the source `Vec2::lerp` operation as `the cad lerp from ... to ... at ...`.

Circular arcs normalize their start and sweep counterclockwise, including a
full turn for coincident endpoints. Ellipse parameters are used directly;
backward and zero-length intervals retain their original direction and span.
Every result includes both endpoints and at least four segments. The source's
non-finite/negative density behavior is retained. Coordinates stay binary64,
and the supplied elevation is copied unchanged. Each result is an independently
owned raw list; callers free it once.

DynLex lists have a signed 32-bit element count. Finite requests exceeding
2,147,483,646 segments fail before narrowing or adding the final endpoint.
This is an explicit representation limit, not a density clamp. The two overflow
fixtures check this failure separately for circular and elliptical arcs, with
native crash-dialog suppression. Impossible-size Rust allocations are not run
as a numerical reference.

## Verification

All 11 source tests are translated with their original tolerances and pass
O0/O2. The unchanged Rust tests also pass in both modes. Differential checks
cover **118 cases / 38,018 scalar comparisons**, including density extremes,
partial/full/backward spans, radii from `1e-150` to `1e150`, rotated and
non-unit axes, survey coordinates, NaN/infinity, signed-zero elevation, and
interpolation inside and outside the segment.

Counts are exact; floating values use relative tolerance `3e-12`. Planar
coordinates have a 16-ULP cancellation allowance derived from the geometry's
scale, independently of density, elevation or angles. Signed-zero and non-finite
classification are checked. Native and Rust optimization parity each hold
exactly. Overflow fixtures pass all four O0/O2 checks.

Compiler SHA-256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Evidence: `build/tessellate2-checks/summary.json`,
`build/tessellate2-differential.log`, `build/tessellate2-overflow.log`.

```powershell
python tests/cadkernel/tessellate2/verify.py --source <pinned-checkout>
```

The runner rejects source drift and records compiler/native hashes. Windows
uses MSVC rustc to match the native math runtime; `--rustc` overrides its path.
