# ACIS history frame preparation

This is a limited native slice of `src/acis/history.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` (SHA-256
`de4c1e74bdf560cd44503ac9aa0a0c28b67cf8fa3f46e1330f4697e5c32147bc`).
The OCS reference also uses the codec at
`70ac6da7cf149cea6398a3d8829dd5e48b485b96`.

`lib/cadkernel/acis_history_frames.dl` covers the source helpers
`placement`, `compose_placements`, `ocs_plane`, `straight_curve`, and
`placed_curve`. Matrix input is a borrowed list of exactly 16 binary64 values;
the additional length check is a DynLex boundary check for Rust's fixed array.
Placement parsing checks finite entries and the affine homogeneous column.
The later placed-curve step checks similarity. Results use `valid` and `failure`:
`0` is success, `1` is `InvalidTransform`, and `2` is `InvalidParameters`.
Only read `value` when `valid` is true.

The OCS frame follows the pinned arbitrary-axis choice, including the branch
near world Z. Straight-curve preparation retains the source's short-line and
near-horizontal thresholds. In particular, the source can return a curve with
NaN planar coordinates for a NaN endpoint on its horizontal branch; this slice
preserves that observed result. The placement and frame helpers do not rebuild
history bodies or dispatch sweep, extrusion, revolve, loft, fillet, or chamfer.

The required fixture checks malformed and short matrices, error categories,
borrowed matrix input, retained and cloned polyline storage, and 100 repeated
placements. The differential runner checks source and codec commit IDs plus
the exact `history.rs` file hash, extracts those five unedited Rust functions
into an isolated reference harness, and compares 39 bit-level traces at O0/O2.
Run from the DynLex repository root:

```powershell
python -B tests/cadkernel/acis_history_frames/verify.py --source <pinned-cadkernel-checkout> --codec <pinned-codec-checkout>
```

The observed run passed the required fixture in both modes and all 39 Rust
comparisons in each mode. This is helper coverage only; it does not promote
the full history module or the main-crate port to verified.
