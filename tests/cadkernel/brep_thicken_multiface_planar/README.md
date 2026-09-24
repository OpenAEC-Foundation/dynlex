# Coplanar two-face sheet thickening

This bounded route thickens a valid open sheet with two adjacent, coplanar
planar faces. It extracts and thickens each face independently, then removes
the coincident interior face to form one closed solid. Signed distances follow
the source faces' oriented normals. The source body remains borrowed, and the
result owns its topology and geometry.

The differential pins `cadkernel` revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and the hash of
`src/brep/thicken.rs`. Nine cases per O0/O2 mode compare acceptance,
source immutability, topology counts, vertices and analytic box mass against
the pinned Rust public `thicken` route. The required fixture also checks
both distance signs, invalid offsets, closed-source refusal and refusal of
angled face pairs.

Run from the repository root:

```powershell
python -B tests/cadkernel/brep_thicken_multiface_planar/verify.py --source <pinned-source-checkout> --compiler build/dynlex.exe
python -B tests/cadkernel/verify.py --filter cadkernel_brep_thicken_multiface_planar
```

Angled face pairs need the source's edge connector and are not yet supported
by this route. Larger multi-face sheets and NURBS patches also remain open.
