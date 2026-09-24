# Analytic ACIS geometry write-back

The in-memory lower route writes dirty cylindrical, conical, spherical and
toroidal faces and circular or elliptical edges through their source record
pointers. It uses the pinned SAT token layouts, including the major-axis
length for cone radius and the continuation tokens before its half-angle.
Record IDs are resolved independently of physical slots, and existing record
metadata and unrelated records are preserved.

The verifier pins kernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`
and codec revision `70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It
compares 54 complete Rust/DynLex record and refusal lines at O0 and O2. The
earlier plane, line and point differential adds 15 regression lines per mode.
Cases include wrong record kinds, missing source pointers, swapped record
positions and preserved neighboring records. The source body is borrowed; the
document is edited in place, following the source's partial-write behavior.

Run from the repository root:

```text
python -B tests/cadkernel/acis_lower_analytic/verify.py --source <pinned-source-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_lower_analytic --filter cadkernel_acis_lower_geometry
```

The pinned `lower.rs` source refuses NURBS geometry and skips synthesized
topology even though `pending` counts it. The separate lower audit verifies
these behaviors, error priority and partial-write order for the whole module.
