# Bounded spherical-sheet thickening

`brep_thicken_sphere.dl` ports the single-face bounded spherical branch of
pinned `src/brep/thicken.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
It recognizes a rectangular parameter-space boundary on a spherical surface,
builds an exact annular section and revolves it into a solid. Positive and
negative distances follow the source face orientation. The rectangular-patch
guard is shared with the existing cylindrical branch; its previous behavior
remains covered by the cylindrical differential and required fixtures.

The differential verifies 16 pinned Rust cases at O0 and O2. It compares
validity, topology, analytic volume and centroid, and vertex positions, and
checks that the borrowed source body remains unchanged. The required fixture
checks signed distances, a full turn, refusals, and owned results. The general
mass function does not yet measure these bounded spherical solids, so the
differential calculates their mass analytically from the constructed radii.

Run from the repository root:

```text
python -B tests/cadkernel/brep_thicken_sphere/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_thicken_sphere
```

Multi-face, cone, torus and NURBS thickening, and unified public error
categorization remain outside this slice.
