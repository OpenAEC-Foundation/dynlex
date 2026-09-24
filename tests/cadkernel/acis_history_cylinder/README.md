# ACIS history cylinder reconstruction

This independent slice projects `SolidHistoryOperation::Cylinder` from codec
revision `70ac6da7cf149cea6398a3d8829dd5e48b485b96` onto the public
history rebuild route in cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The pinned
`src/acis/history.rs` SHA-256 is
`de4c1e74bdf560cd44503ac9aa0a0c28b67cf8fa3f46e1330f4697e5c32147bc`.

The API receives the codec's `major_radius`, `minor_radius`, `x_radius`,
`height`, and borrowed base transform. The source validates all three radii,
but `x_radius` does not affect geometry. It then builds a circular analytic
cylinder or exact elliptic loft, parses the placement, and transforms the
body. Invalid dimensions therefore take precedence over invalid placement.
Failure `1` is `InvalidTransform`; failure `2` is `InvalidParameters`. Read
the owned result `value` only when `valid` is true. Native matrix lists must
contain exactly 16 binary64 values; this extra boundary check represents the
source's fixed array.

The required fixture covers both geometric branches, malformed transforms,
error priority, independent input/result lifetimes, and repeated release.
The differential calls unchanged public Rust `rebuild_body` and one-operation
`rebuild_history` at O0 and O2, comparing the full B-rep arena/topology,
geometry, provenance, Euler characteristic, and flaw count. Structural
fields are exact; floating fields use the repository's tight comparator.
This does not parse codec records or dispatch other creation operations and
subsequent history steps.

Run from this repository root using the pinned source and codec checkouts:

```powershell
python -B tests/cadkernel/acis_history_cylinder/verify.py --source <cadkernel-checkout> --codec <codec-checkout>
```
