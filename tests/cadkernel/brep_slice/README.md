# B-rep slice verification

This directory verifies the native slice implementation of cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

Plane slicing orthonormalizes the frame, projects body bounds, creates bounded
half-space solids and intersects closed bodies through the regular Boolean
pipeline. Open sheets are imprinted and partitioned without adding caps.
Oriented single-face surface cutters classify both sides by their analytic
signed-distance field; closed results reuse the divided cutter faces as caps.
NURBS cutters retain the source `NoClosedForm` result and malformed cutters are
refused without changing either input.

`source-tests.txt` locks both upstream tests in source order. `main.dl` covers
those plane tests plus oblique and survey-coordinate solids, empty and invalid
inputs, an open-sheet split, a capped surface split, malformed-cutter refusal,
all five public analytic side fields with orientation reversal, and an
integrated sphere-sheet split. The required fixture mirrors all nine groups.

`verify.py` compares both complete result-body arenas with the pinned Rust
implementation. Its recorded combined run passes 146 cases and 253,856 field
comparisons across O0 and O2, with 144 valid and 111 divided cases. The added
coverage exercises the public signed side of plane, cylinder, cone, sphere and
torus cutters in both orientations; integrated cylinder and cone cuts of a
closed box; a sphere cut of an open sheet; and an intersecting torus that must
return `NoClosedForm`. Native O0 and O2 output is exactly identical. Probe
compilation took 193.672 seconds at O0 and 210.984 seconds at O2. The compiler
SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.

Run the differential check with:

```powershell
python -B tests/cadkernel/brep_slice/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-slice-checks
```

The module is verified against the complete pinned source surface. Exact
closed-form intersections are exercised for plane, cylinder, cone and sphere
cutters. The pinned implementation deliberately refuses intersecting torus
cuts because plane-torus intersection has no supported closed form; the native
port returns the same snag instead of reporting an empty or successful slice.
