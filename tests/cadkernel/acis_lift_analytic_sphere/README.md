# In-memory spherical SAT lift

The native SAT-to-B-rep lift recognizes `sphere-surface` records and circular
edges with distinct endpoints. It resolves record IDs independently of array
slots, preserves provenance and shared topology, applies face and surface
sense, and reconciles projected arc parameters with the stored periodic range.

The verifier pins the Rust kernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and codec revision
`70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It compares 70 complete
body, geometry and topology trace lines from Rust and DynLex at O0 and O2.
The cases include reordered records, stale and shifted edge parameters,
reversed senses, and reversed edges. The Rust reference additionally checks
partial-loss behavior for invalid records. The native entry point still
refuses those inputs with an empty body; other analytic and spline geometry,
pcurves and partial-loss handling remain.

Run from the repository root:

```text
python -B tests/cadkernel/acis_lift_analytic_sphere/verify.py --source <pinned-source-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_lift_analytic_sphere
```
