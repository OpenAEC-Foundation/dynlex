# Full-turn solid revolution

This differential harness compares the native full-turn line-and-arc revolution
with the pinned kernel revision. It emits the complete body graph: arena slots,
ownership, coedge senses, pcurves, analytic geometry, provenance, Euler value
and validation findings. The corpus covers axis-touching cylinders and cones,
annular sections, spheres, toroidal arc sections, reversed and negative-side
profiles, translated and rotated frames, invalid axes, and deterministic random
rectangular, tapered and arc profiles.

Run from the repository root:

```text
python tests/cadkernel/brep_sweep_revolve/verify.py --source PATH_TO_PINNED_SOURCE
```
