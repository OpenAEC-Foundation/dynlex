# B-rep press-pull verification

This directory verifies the native face-editing and planar-region implementation of
cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

The native implementation extracts exact planar face profiles with nested
trim loops and orientation-aware outward normals. Point picking projects onto
the supporting plane and tests the exact pcurve boundaries, so a point on the
infinite plane but outside the trimmed face is rejected. Offset-mode
press-pull clones the source body, performs the edit near the selected face's
origin, moves vertices along analytic line, circle and ellipse rails, rebuilds
boundary curves from adjacent surfaces, invalidates stale pcurves and checks
area orientation, surface membership, topology and vertex gaps before the
result is returned.

Closed circles and ellipses are split into exact quarter pieces before an
extrusion. `planar_region` builds a bounded planar sheet directly from exact
line, circle, ellipse and planar-spline loops, including inner loops. Inner
loops follow the reversed traversal of the source's copied bottom cap. The
source body and caller-owned curves remain independent of successful results.
Extrude mode builds a translated solid while preserving the input body.
Planar union, intersection and subtraction use the native split, imprint and
Boolean pipeline. Contact is distinguished from separation, and region holes
are transferred into the two extrusion caps without introducing artificial
edge splits.

`main.dl` checks a source-preserving cuboid offset and extrusion, trim-aware
face picking, closed-conic splitting, a bounded planar sheet, all three planar
Boolean operations, contact classification and the periodic analytic
parameters for plane intersections with circles and ellipses. The required
fixture passes at O0 and O2. Its latest measured compile and run times were
226.797/0.625 seconds at O0 and 214.359/0.297 seconds at O2.

`verify.py` builds `probe.dl` and `reference.rs` against the pinned source. It
compares result validity, vertex/edge/face counts, Euler characteristic,
topology flaws, worst vertex gap, complete vertex sets and curve/surface kind
histograms. The corpus covers cuboids, wedges, pyramids, frustums, cylinders,
invalid and collapsing distances, rectangles, circles, ellipses and planar
regions with holes. Random cases span scales from about `1e-4` to `1e5` and
world coordinates around `1e9`.

The general differential run passes 292 cases and 16,928 comparisons across
O0 and O2, with 211 valid results in each mode. Native compilation took
200.703 seconds at O0 and 198.985 seconds at O2; the linked Rust probes took
0.484 and 0.828 seconds. The separate planar-Boolean matrix passes 69 cases
and 16,370 comparisons with exact native O0/O2 parity. Its measured native
compile times were 196.609 seconds at O0 and 204.750 seconds at O2, versus
0.531 and 0.828 seconds for the linked Rust probes. The compiler SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.
The manifests are `build/brep-presspull-complete/summary.json` and
`build/brep-planar-hole/summary.json`.

Disjoint analytic circles and tangent classification are included. Solid
Booleans for overlapping or contained analytic circles currently exceed the
120-second per-case verification limit in DynLex and remain an explicit
performance limitation; they are excluded from the required quick matrix.

Run the differential check with:

```powershell
python -B tests/cadkernel/brep_presspull/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-presspull-checks
```

`source-tests.txt` records both tests in the pinned source module. The planar
verifier refuses to run if that inventory changes.
