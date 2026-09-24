# Path sweep: open-sheet body assembly

`brep_sweep_path_body.dl` ports the open-sheet branch of `build_body` from
pinned `src/brep/sweep_path.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
Prepared rational profile wires and cubic path-frame patches are borrowed.
The result owns a B-rep with shared station rims and rails, rational NURBS
skins, pcurves, a shell and lump, and synthesized provenance.

The differential compares four public Rust path-sweep examples against the
native assembled body at O0 and O2, checking 176 numeric fields. The required
fixture checks shared topology and pcurves, orientation, a closed profile
requested as a sheet, refusals, repeated construction, and ownership after
input release. Empty or disconnected input, closed paths and solid-cap requests
are explicitly refused by this entry point.

Run from the repository root:

```text
python -B tests/cadkernel/brep_sweep_path_body/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_sweep_path_body
```

This entry point starts with prepared wires and patches. The complete public
`sweep_path` route still needs profile preparation, path fitting and option
integration, solid caps, and closed-path seams. The imported cubic fitter
expects its caller to supply a concrete frame-sample evaluator when fitting
is used; the current body entry point consumes patches already fitted.
