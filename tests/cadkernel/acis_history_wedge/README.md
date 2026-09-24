# ACIS history wedge reconstruction

The public `SolidHistoryOperation::Wedge` path from cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` is projected onto the native
history dispatch as operation kind `2`. The pinned `src/acis/history.rs` SHA-256
is `de4c1e74bdf560cd44503ac9aa0a0c28b67cf8fa3f46e1330f4697e5c32147bc`.
The pinned codec revision is `70ac6da7cf149cea6398a3d8829dd5e48b485b96`.

The codec shares `SolidHistoryBox` dimensions between Box and Wedge. Its
length, width and height become the native size vector's x, y and z. The
borrowed transform is a column-major list of exactly 16 binary64 values.
The triangular prism is constructed before parsing that transform, matching
the source's `finish` error priority: invalid dimensions return
`InvalidParameters` (`failure=2`) even if the transform is also invalid;
invalid placement returns `InvalidTransform` (`failure=1`). The successful
result owns its B-rep body.

`verify.py` pins the unchanged Rust and codec sources, builds a reference
which calls public `rebuild_body` and single-operation `rebuild_history`, and
compares the complete B-rep trace from DynLex at O0 and O2. The 34 cases per
mode cover valid placement and dimension ranges, malformed dimensions and
transforms, error priority, and Box/Unknown selector controls. The native
probe separately checks topology, borrowed input, owned result, clone and
repeated release. The verified run compared 36,776 trace fields across 68
cases with no mismatch.

This slice does not parse codec records or rebuild subsequent Fillet and
Chamfer history operations. Other creation paths remain separate.

Run from the DynLex repository root:

```powershell
python -B tests/cadkernel/acis_history_wedge/verify.py --source <pinned-cadkernel-checkout> --codec <pinned-codec-checkout>
```
