# Solid region extrusion verification

The required `cadkernel_brep_region_solid` fixture checks native extrusion of
one and two square holes, a circular hole expanded into arcs, and extrusion
against the plane normal. It verifies manifold edges, topology, face counts,
root count, cap-ring counts, and unchanged borrowed profiles. The shared solid
builder also covers positive, negative, and zero taper. Cap areas confirm that
positive taper shrinks the outer loop and widens holes; negative taper reverses
those changes. It checks a tapered circular hole and refuses far-cap collisions,
invalid angles, and collapsed outer profiles. Empty, outside, intersecting,
touching, and nested input holes are refused. Run it at O0 and O2 with:

```powershell
python -B tests/cadkernel/verify.py --filter cadkernel_brep_region_solid
```

The pinned-source differential in `tests/cadkernel/brep_sweep_region_diff`
compares its supported solid-region cases alongside lateral sheets. Revolved
regions and path sweeps remain separate work.
