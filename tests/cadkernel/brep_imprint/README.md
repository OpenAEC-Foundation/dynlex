# B-rep imprint verification

This directory verifies the native imprint implementation of cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

The implementation prefilters face bounds, gathers exact supported surface
meetings, handles coincident ground, cuts both bodies, and aligns edge vertices.
Unsupported analytic intersections retain the source snag distinction. Line,
circle, arc, ellipse and NURBS pcurves are eligible for aligned edge splitting;
other pcurve kinds are left untouched.

`main.dl` covers the nine source behaviors in six groups: consistency and
partitioning of overlapping boxes, disjoint boxes, equal coplanar faces, a
partly shared wall, explicit no-closed-form refusal and survey coordinates.
The required fixture mirrors this program and its expected output.

`verify.py` emits and compares both complete mutated body arenas against the
pinned Rust source. The recorded combined run passes 91 cases and 390,952 field
comparisons across O0 and O2, with 90 valid cases and exact native optimization
parity. The compiler SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.

Run the differential check with:

```powershell
python -B tests/cadkernel/brep_imprint/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-imprint-checks
```
