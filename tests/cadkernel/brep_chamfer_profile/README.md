# Native B-rep chamfer profile trimming

`lib/cadkernel/brep_chamfer_profile.dl` ports the complete private helper in
`src/brep/chamfer_profile.rs` from pinned revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The helper trims the two profile
segments adjacent to every selected node, inserts the straight bevel segment,
preserves supported line and circular-arc segments, and rejects missing face
adjacency, excessive trim distances, axis contact and profile intersections.
Curve kinds other than lines and circular arcs return the explicit unsupported
surface error, matching the source.

Inputs are borrowed. A successful result owns a reference-counted topology list
and its managed curve payloads; an error owns an empty list. Selection nodes are
expected to be unique, as they originate from the source implementation's map.

The required fixture has eleven independent groups. It covers one and two
selected corners, face-specific distances, malformed profile lengths, missing
and same-side adjacency, distance exhaustion, axis avoidance, line/arc
preservation, unsupported curves, self-intersection and a wrapped arc whose
forward sweep is trimmed at a selected endpoint. Run both optimization modes
with:

```powershell
python -B tests/cadkernel/verify.py --filter cadkernel_brep_chamfer_profile --compile-timeout 120
```

The differential runner copies the unchanged pinned helper beside its Rust
adapter, compiles independent Rust and DynLex executables at O0 and O2, and
compares success/error variants, edge identifiers, curve order, curve kinds and
every output scalar:

```powershell
python -B tests/cadkernel/brep_chamfer_profile/verify.py --source C:/path/to/pinned/cadkernel
```

On 2026-09-16, compiler SHA-256
`17cf5600c05b525005cc7878e7ad1abe268d8d78b531694b6abe40b2145e66d4`
passed 164 fixed and deterministic cases with 12,744 comparisons, including
114 valid profiles. The corpus includes clockwise input ordering, wrapped arcs,
scales from small model units through survey coordinates, boundary distances,
non-finite segment lengths, axis tolerances, unsupported curves and crossing
profiles. Both implementations produced byte-identical O0/O2 output.
`build/brep-chamfer-profile-checks/summary.json` records the source, compiler,
reference-library and native-source hashes used for the run.
