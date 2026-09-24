# ACIS history torus reconstruction

This independent slice projects `SolidHistoryOperation::Torus` from codec
revision `70ac6da7cf149cea6398a3d8829dd5e48b485b96` onto the public
history rebuild route in cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The pinned
`src/acis/history.rs` SHA-256 is
`de4c1e74bdf560cd44503ac9aa0a0c28b67cf8fa3f46e1330f4697e5c32147bc`.

The API receives the codec's major and minor radii and borrowed base
transform. The existing torus builder creates a ring for `major > minor`,
and trims the surface at the axis for the horn and spindle cases. The body
is constructed before placement parsing, so invalid radii take precedence
over invalid placement. Failure `1` is `InvalidTransform`; failure `2` is
`InvalidParameters`. Read the owned result `value` only when `valid` is
true. Native matrix lists must contain exactly 16 binary64 values; this
boundary check represents the source's fixed array.

The required fixture covers all three shapes, topology, synthesized
provenance, malformed transforms, error priority, independent input/result
lifetimes, and repeated release. Its 180-second compile marker accommodates
builds beyond the runner's 30-second default. The differential calls
unchanged public Rust `rebuild_body` and one-operation `rebuild_history` at
O0 and O2. It compares the full B-rep arena/topology, geometry, provenance,
Euler characteristic, and flaw count. This slice does not parse codec
records or dispatch other creation operations and subsequent history steps.

Run from this repository root using the pinned source and codec checkouts:

```powershell
python -B tests/cadkernel/acis_history_torus/verify.py --source <cadkernel-checkout> --codec <codec-checkout>
```
