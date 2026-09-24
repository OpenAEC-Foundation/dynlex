# In-memory ring-torus SAT lift

The native SAT-to-B-rep lift recognizes a ring-torus surface and closed
circle edges in addition to its faceted records. It reads the pinned record
fields, preserves record IDs as provenance, applies surface sense, and builds
shared topology. The fixture also checks that changing a closed edge's SAT
ratio to a non-unit value now yields an exact elliptic curve, as in Rust.

The verifier pins both the Rust kernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and codec revision
`70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It compares 51 complete
trace lines from Rust and DynLex at O0 and O2. The existing faceted-lift
differential remains a regression check for plane and line records.

Run from the repository root:

```text
python -B tests/cadkernel/acis_lift_analytic_torus/verify.py --source <pinned-source-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_lift_analytic_torus
```

This is still a partial port of `src/acis/lift.rs`. Other conical and
elliptical topologies, spline and pcurve records, and partial-loss handling
remain open. The source can return a partial body with loss information for
some errors; this native entry point currently refuses those inputs.
