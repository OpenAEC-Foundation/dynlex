# Curved open-sheet path sweep

`lib/cadkernel/brep_sweep_path_curved.dl` integrates the existing profile
preparation, initial placement, adaptive path-frame walk, cubic fit and
open-sheet body assembly for one genuinely curved, open quadratic NURBS span.
`brep_sweep_path_public.dl` dispatches this bounded case through the public
NURBS-path entry point. The reference is pinned to
`src/brep/sweep_path.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`;
`verify.py` checks its file hash before running either optimization mode.

The differential compares complete vertex positions, edge endpoint positions,
curve samples and use counts, coedge directions and pcurve samples, face
orientation and surface samples, arena counts, roots, validation flaws and
provenance against the pinned Rust body in O0 and O2. The required fixture
checks public dispatch, explicit unsupported
inputs, source preservation after repeated calls, and detached body ownership.

The profile and path are borrowed. Temporary raw walk, fit, span and source
lists are released on success and refusal. The returned body manages its own
topology and geometry. Multiple path spans, degree other than two, nearly
straight and closed paths, multiple profile curves, twist, scale, bank and
solid caps are outside this route and return an invalid result.

Run from the repository root:

```text
python -B tests/cadkernel/brep_sweep_path_curved/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_sweep_path_curved
```
