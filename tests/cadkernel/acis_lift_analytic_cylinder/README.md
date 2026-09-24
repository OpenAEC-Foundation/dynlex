# In-memory cylindrical SAT lift

The SAT-to-B-rep lift recognizes a cylindrical `cone-surface` record in addition
to planes and ring tori. It reconstructs the cylinder radius from the major-axis
vector, preserves record IDs as provenance, applies face and surface sense, and
shares the side seam and cap edges. Record order does not need to match IDs.

The verifier pins the Rust kernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and codec revision
`70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It compares 84 complete
body, surface, curve, topology and provenance trace lines from Rust and DynLex
at both O0 and O2. The Rust reference also checks partial-loss and noncylindrical
inputs. The native entry point still refuses those inputs with an empty body;
general cones, open arcs, splines, pcurves and partial-loss handling remain.

Run from the repository root:

```text
python -B tests/cadkernel/acis_lift_analytic_cylinder/verify.py --source <pinned-source-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_lift_analytic_cylinder
```
