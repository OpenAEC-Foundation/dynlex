# B-rep boolean verification

This directory verifies the native solid boolean implementation of cadkernel
revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

The implementation clones both inputs, imprints them, classifies retained faces,
copies and stitches exact topology, orients connected components, and restores
separate exterior lumps and inward cavity shells. Component-volume
classification covers tessellated general faces plus exact planar solids,
spheres and coaxial circular cylinders, including cylinders whose mantle was
divided by imprinting. Invalid operations and unsupported geometry return the
source snag values without mutating the inputs.

`main.dl` checks overlapping union, intersection and difference, a disjoint
union with two roots, an empty disjoint intersection, and contained box and
sphere differences with exterior and cavity shells. `source-tests.txt` accounts
for all 21 source tests. `verify.py` compares complete body arenas against the
pinned Rust implementation for overlaps, box/cylinder intersections, coincident
and partially shared walls, disjoint solids, containment in both directions,
survey coordinates, randomized scales and the source's five-sided wall and door
cut scenario.

The recorded combined run passes 96 cases and 244,716 field comparisons across
O0 and O2. All 96 cases are valid and the native outputs have exact optimization
parity. The extended probe compiled in 127.000 seconds at O0 and 201.172 seconds
at O2. The exact door-cut case contributes 5,127 comparisons per mode and the
partially shared wall cases contribute 2,868 comparisons per mode. The compiler SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.

Run the differential check with:

```powershell
python -B tests/cadkernel/brep_boolean/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-boolean-checks
```

This establishes the complete `src/brep/boolean.rs` source scope at the pinned
revision. Unsupported surface intersections remain explicit upstream
limitations and are not treated as disjoint geometry.
