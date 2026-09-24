# Bounded toroidal-sheet thickening

`brep_thicken_torus.dl` ports the single-face bounded toroidal branch of pinned
`src/brep/thicken.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
It uses the shared rectangular parameter-space patch guard, checks that both
tube radii are positive and clear the torus axis, and revolves the exact
arc-ring section. Signed distance follows face orientation. The borrowed
source body remains unchanged; the result owns its topology.

The differential compares 20 pinned Rust cases at O0 and O2, including
positive and negative distances, both face orientations, partial and full
major-axis turns, invalid radii, topology and vertices. It evaluates the
sector's volume and centroid analytically from the matched radii and angles;
the general B-rep mass function does not measure these toroidal solids. The
required fixture checks geometry, refusal, source immutability and result
ownership after profile release.

Run from the repository root:

```text
python -B tests/cadkernel/brep_thicken_torus/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_thicken_torus
```

The pinned sheet constructor refuses a fully closed minor arc, so that case
has no source-body differential. Multi-face and NURBS thickening and unified
public error categorization remain outside this slice.
