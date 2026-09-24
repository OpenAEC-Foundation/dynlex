# Cylindrical sheet thickening

`lib/cadkernel/brep_thicken_cylinder.dl` ports the bounded, single-face
cylinder branch of pinned `src/brep/thicken.rs`. It verifies a rectangular
surface parameter boundary, respects the face orientation, builds an exact
radial section, and revolves that section into a solid. Partial, near-full,
full, and negative revolutions are covered with positive and negative
distances. Zero and collapsing offsets are refused. The source sheet is
borrowed and remains unchanged.

The required fixture runs at O0 and O2. The differential runner compares 12
public `thicken` cases per mode against the pinned Rust crate: validity,
complete topology arena counts, validation flaws, paired edges, pcurves, face
orientation, analytic volume and centroid, and sorted vertices. It also pins
the Rust revision and `thicken.rs` hash.

```powershell
python tests/cadkernel/brep_thicken_cylinder/verify.py --source PATH_TO_PINNED_SOURCE
```

This module does not yet dispatch all `thicken` surfaces. Multi-face bodies,
cones, spheres, tori and NURBS patches remain to be ported. Its invalid result
currently conveys validity only; source-style error categories are part of the
later dispatcher.
