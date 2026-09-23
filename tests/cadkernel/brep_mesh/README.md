# Native B-rep mesh verification

`lib/cadkernel/brep_mesh.dl` ports the complete public mesh behavior from the
pinned source revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It covers
face and body tessellation, display meshes, edge and isoline sampling,
silhouettes, surface area, volume, centroids and inertia. Plane, cylinder,
sphere, cone, torus, ruled and NURBS surfaces are exercised at both `-O0` and
`-O2`.

The required fixtures test topology, mass properties, holes, boolean results,
missing faces, chordal tolerance, ownership, periodic seams, wireframes,
silhouettes and sweep/loft surfaces. The NURBS fixture additionally covers an
elliptical cone with a collapsed surface boundary and an elliptical frustum
whose planar caps have NURBS boundary curves without stored pcurves.

The differential corpus contains 40 deterministic cases and performs 3,360
comparisons across both optimization levels. Bounds, completeness, provenance,
edge counts, winding, display data and physical properties are checked. Most
analytic solids also require identical triangle and sample counts. Spheres and
NURBS solids use topology-independent checks because valid pole handling and
adaptive span refinement need not reproduce the reference triangulation. Their
positions must still form complete triangles, native winding must be outward,
and surface and mass metrics must remain within the documented one-per-thousand
mesh tolerance. The singular reference cone has inward pole slivers, so only
the magnitude of its reference winding is used; native winding remains required
to be positive.

Run the full differential corpus from the repository root:

```powershell
python tests/cadkernel/brep_mesh/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --optimization O0 --optimization O2 `
  --output build/brep-mesh-checks
```

Use `--case NAME` for a focused case and `--skip-build` only with binaries
created by an earlier run in the same output directory. All executables run
through the shared guarded process helper. `summary.json` records the compiler
and source hashes, compile times, case counts and optimization parity.
