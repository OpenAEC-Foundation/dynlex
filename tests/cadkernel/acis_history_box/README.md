# ACIS history box reconstruction

This bounded port maps the codec's `SolidHistoryOperation::Box` record to the
public `rebuild_body` route in pinned cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The pinned
`src/acis/history.rs` SHA-256 is
`de4c1e74bdf560cd44503ac9aa0a0c28b67cf8fa3f46e1330f4697e5c32147bc`;
the codec revision is `70ac6da7cf149cea6398a3d8829dd5e48b485b96`.

Native operation kind `1` means Box; `0` and all other kinds return
`Unsupported` (`failure=3`). The size's x/y/z values are the codec's
length/width/height, and the borrowed list is its base transform in the
codec's column-major layout. A list of anything other than 16 values is
rejected at the native boundary. The result owns its body. Failure `1` is
`InvalidTransform`; failure `2` is `InvalidParameters`. The source creates
the cuboid before checking the placement, so invalid dimensions win when
both are invalid. Read `value` only when `valid` is true.

The differential compares the entire returned B-rep trace, including arena
keys, topology, curves, surfaces, provenance, Euler characteristic and flaw
count, against the pinned public Rust `rebuild_body` and one-operation
`rebuild_history` at O0/O2. Structural fields are exact and floating fields
use the repository's established tight numeric comparator. The required
fixture checks malformed input, non-similarity refusal, error priority,
borrowed input and owned result lifetimes, and repeated release.

This does not implement the other history creation variants or subsequent
fillet/chamfer dispatch, nor does it parse codec records or ACIS data itself.

Run from this repository root with the pinned source and codec checkouts:

```powershell
python -B tests/cadkernel/acis_history_box/verify.py --source <cadkernel-checkout> --codec <codec-checkout>
```
