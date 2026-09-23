# Native analytic mass properties

`lib/cadkernel/brep_mass.dl` ports `src/brep/mass.rs` from pinned revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It returns exact volume,
centroid, origin moments and products of inertia, principal axes and moments,
and radii of gyration for complete analytic spheres, circular cylinders and
their concentric closed shells. Unsupported surface sets return an invalid
result. The returned record owns no allocations and the input body is borrowed.

The required fixture translates all three source tests and adds a partial
annular-cylinder sector, a concentric spherical shell and explicit unsupported
surface refusals. Run it in both optimization modes with:

```powershell
python -B tests/cadkernel/verify.py --filter cadkernel_brep_mass --compile-timeout 120
```

The differential runner builds independent executables from the pinned source
and the native module, then compares all 25 scalar fields for fixed and
deterministic randomized cases at O0 and O2:

```powershell
python -B tests/cadkernel/brep_mass/verify.py --source C:/path/to/pinned/cadkernel
```

Its synthetic annular sectors carry explicit rectangular pcurves and paired
edge uses. This isolates the mass recognizer from the still separate shell and
boolean operation verification while exercising the same public body graph.

On 2026-09-16, compiler SHA-256
`8ed4ee0e491a9adb5dcec892c09ca158484ae98405f4700065800e1d5650c59d`
passed the required fixture at O0 and O2. The differential run passed 247 cases,
including 165 valid analytic bodies, with 17,488 comparisons. Native O0 and O2
output was byte-for-byte equal. The Rust references differed in the final few
bits of three derived values under optimization; their O0/O2 values and every
native-to-reference value passed the recorded `5e-12` relative and `5e-10`
absolute tolerances. `build/brep-mass-checks/summary.json` records the source,
compiler and native-source hashes used for that run.
