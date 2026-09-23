# Signed area and centroids

`lib/cadkernel/area2.dl` ports all of `src/geom2d/area.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` (306 production lines, 12 tests).

Public operations are `the cad enclosed area of curve`, `the cad chord closed
area of curve`, `the cad enclosed centroid of curve` and `the cad chord closed
centroid of curve`. Optional results have `valid` and scalar/vector `value`
fields. A false result has a safe zero value. Inputs are borrowed.

Lines and circular arcs use their analytic boundary contributions. Ellipses
and splines use the original 16 panels and five Gauss–Legendre nodes. Centroid
integration preserves the source operation order and translates to a local
origin first. Polylines use their exact segment contributions, including bulges.
Unbounded curves have zero enclosed area and absent chord-closed results.
Centroids are absent when the source rejects the curve or its area/moments.
No new tolerance or finite-input rejection is added.

The source's two polyline conventions are preserved: `enclosed_area` already
adds an end-to-start chord to an open polyline, and `chord_closed_area` adds
that chord again. A translated open square explicitly tests this behavior.

## Verification

All 12 original test groups are translated with their original tolerances.
`area2_centroid` additionally checks circle/ellipse/square/semicircle centroids,
survey-coordinate translations, open and closed chains, zero area, unbounded
curves, unchanged borrowed inputs and returned-local scalar lifetime.

The differential runner compiles unchanged Rust area, transformation and
arrangement modules, with the shared reference geometry dependencies. The
arrangement module supplies the original polygon-area assertion in a source
test; reference geometry is not reimplemented by the runner.

Result: **218 cases, 3,456 Rust scalar comparisons**, all 12 unchanged Rust
tests and native fixtures at O0/O2. Coverage includes all eight curve kinds,
positive/negative/zero/extreme radii, periodic and partial spans, empty/short
chains, bulges, rational and closed splines, NaN/infinity and deterministic
random cases. Flags/counts are exact; floats have relative tolerance `3e-12`;
coordinates have a 16-ULP cancellation allowance at the geometry scale.
Signed zero and non-finite classification are checked. Native and Rust
optimization parity are each exact.

Compiler SHA-256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Evidence: `build/area2-checks/summary.json`, `build/area2-differential.log`.

```powershell
python tests/cadkernel/area2/verify.py --source <pinned-checkout>
```

The runner rejects source drift and records compiler/source hashes. Windows
uses MSVC rustc to match the native math runtime; `--rustc` selects its path.
