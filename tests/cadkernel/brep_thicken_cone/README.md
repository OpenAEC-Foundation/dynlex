# Bounded conical-sheet thickening

`brep_thicken_cone.dl` ports the single-face bounded conical branch of pinned
`src/brep/thicken.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
It uses the shared rectangular parameter-space patch guard, calculates the
source and offset radii at both heights, and revolves the exact four-sided
section into a solid. Signed distances follow face orientation. The borrowed
source body remains unchanged and the resulting body owns its topology.

The differential compares 19 pinned Rust cases at O0 and O2, including
positive and negative distances, both face orientations, partial and full
turns, refusals, topology and vertices. It also derives volume and centroid
from the revolved section because the general mass function does not yet
measure these bounded conical solids. The required fixture checks geometry,
refusals, source immutability and result ownership after profile release.

Run from the repository root:

```text
python -B tests/cadkernel/brep_thicken_cone/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_thicken_cone
```

Multi-face, toroidal and NURBS thickening and unified public error
categorization remain outside this slice.
