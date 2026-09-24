# Public open-sheet path sweep

`brep_sweep_path_public.dl` connects the existing profile placement, path-frame
walk, adaptive cubic fitting and open-sheet body assembly into a bounded public
route from pinned `src/brep/sweep_path.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It accepts one open line or
NURBS profile along an analytic line or a straight, clamped NURBS path. It
supports base-point selection, initial rotation and multiple straight knot
spans. The source profile and path remain borrowed; the returned body owns its
topology and geometry.

The differential verifies the pinned source hash and compares 11 Rust/DynLex
cases at both O0 and O2: 308 fields per mode, 616 comparisons total. The
cases include weighted profiles and paths, reversed direction, off-plane base
points, topology and provenance. The required fixture checks source preservation,
repeated calls and explicit refusals. The earlier base and sheet-body fixtures
remain regression checks.

Run from the repository root:

```text
python -B tests/cadkernel/brep_sweep_path_public/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_sweep_path_public
```

One open, genuinely curved quadratic NURBS span is dispatched to the separate
curved-path module and differential fixture. Other curved or closed paths,
multiple profile curves, twist, scale, bank and solid caps remain outside the
bounded route and are refused without returning a partial body.
