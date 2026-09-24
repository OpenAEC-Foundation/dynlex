# ACIS lower source audit

This audit covers the complete behavior of the pinned 206-line
`src/acis/lower.rs` module. It writes dirty analytic faces, edges and vertices
through provenance and source pointers, preserves unrelated SAT records, and
returns the source's error at the same stage of a partial write.

The source deliberately does not write newly synthesized topology: `pending`
counts synthesized nodes, while `lower` skips them because it cannot attach
new records to the pointer graph. NURBS faces and edges are refused with
`Surface` or `Curve` before source-pointer lookup. These are source behaviors,
not missing native capabilities within this module.

The differential pins kernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and codec revision
`70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It compares 184 complete
result, error, metadata and record lines per O0/O2 mode, including clean,
dirty and synthesized provenance; unsupported or missing geometry; invalid
circle/ellipse frames; missing pointers; sparse and duplicate record IDs; and
partial face/edge/vertex writes. The earlier plane/line/point and analytic
write-back differentials add 15 and 54 lines per mode.

Run from the repository root:

```powershell
python -B tests/cadkernel/acis_lower_audit/verify.py --source <pinned-kernel-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_lower_geometry --filter cadkernel_acis_lower_analytic --filter cadkernel_acis_lower_audit
```
