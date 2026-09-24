# In-memory cylindrical SAT lift

The SAT-to-B-rep lift recognizes cylindrical and nonzero-angle conical
`cone-surface` records in addition to planes and ring tori. It reconstructs
the radius from the major-axis vector and the cone angle from the sine and
cosine tokens, preserves record IDs as provenance, applies face and surface
sense, and shares the side seam and cap edges. Record order does not need to
match IDs. Non-unit `ellipse-curve` ratios are retained as exact elliptic
edge curves for closed rims.

The verifier pins the Rust kernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and codec revision
`70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It compares 113 complete
body, surface, curve, topology, provenance, sloped-cone and elliptical-rim
trace lines from Rust and DynLex at both O0 and O2. The Rust reference also
checks partial-loss
behavior; the native entry point still refuses malformed inputs with an empty
body. Other conical topologies, open elliptical arcs, splines, pcurves and
partial-loss handling remain.

Run from the repository root:

```text
python -B tests/cadkernel/acis_lift_analytic_cylinder/verify.py --source <pinned-source-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_lift_analytic_cylinder
```
