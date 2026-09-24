# Planar sheet thickening

`brep_thicken_planar.dl` implements the single bounded planar-face branch of pinned `src/brep/thicken.rs` at revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It validates the borrowed sheet and signed distance, extracts every boundary loop, and builds a solid through the native region extrusion. Multi-face, revolved and NURBS sheets remain outside this slice and return an invalid body result.

The required fixture checks a sheet with a hole at positive and negative thickness, closed output topology, unchanged input and zero-distance refusal. The differential probe compares six valid/refusal cases with the pinned Rust library at O0/O2: all nine arena counts, roots, flaws, manifold edges, pcurves, face-direction counts and sorted vertex coordinates. The region constructor reverses inner-loop traversal to match the source's bottom-cap copy, which is needed for the thickened cavity walls.

Run from the repository root:

```text
python -B tests/cadkernel/verify.py --filter cadkernel_brep_thicken_planar
python -B tests/cadkernel/brep_thicken_planar/verify.py --source <pinned-cadkernel-checkout>
```
