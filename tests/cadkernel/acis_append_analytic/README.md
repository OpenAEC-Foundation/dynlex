# Analytic SAT append

The native in-memory append route writes cylindrical and conical surfaces as
`cone-surface`, toroidal surfaces as `torus-surface`, and bounded elliptical
edges as `ellipse-curve`. It also writes line pcurves with the support surface
embedded in their SAT record. For analytic surfaces, unsupported arc pcurves
are omitted, following the pinned source. Record indices account for the
optional pcurve immediately before its coedge. Existing document records are
preserved when a supported body is appended.

The verifier pins cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and codec revision
`70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It builds the Rust
reference from those checkouts and compares complete SAT records, including
token kinds, pointers and binary64 values, for six cases in both O0 and O2.
The cases yield 154 records per optimization level. The required fixture
checks the complete line-pcurve case through the standard test runner. The
faceted append fixture retains the preflight-refusal regression.

Run from the repository root:

```text
python -B tests/cadkernel/acis_append_analytic/verify.py --source <pinned-source-checkout> --codec <pinned-codec-checkout> --compiler build/dynlex.exe
python -B tests/cadkernel/verify.py --filter cadkernel_acis_append_analytic --filter cadkernel_acis_append_faceted --filter cadkernel_acis_append_sphere
```

This is a bounded append slice, not full `src/acis/append.rs` coverage. NURBS
3D curves and surfaces, NURBS pcurves and sparse arena keys remain pending.
The native preflight rejects unsupported geometry before adding records.
