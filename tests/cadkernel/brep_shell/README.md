# B-rep shell verification

This directory verifies the native port of `src/brep/shell.rs` from cadkernel
revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

The public operation recognizes exact rectangular boxes, circular cylinders
and spheres. Positive distances retain the source exterior and subtract an
inward cavity. Negative distances retain the source as the cavity and grow a
new exterior. Selected faces extend the cavity through the corresponding side
or cap. Invalid distances, duplicate or unknown faces, exhausted material,
unsupported topology and kernel snags have distinct result codes, and the
source body remains borrowed and unchanged.

`main.dl` translates both source tests without weakening their assertions. It
also verifies annular cylinder mass, an opened cylinder cap and the public
refusal paths. The required fixture imports that program. The separate split
fixture checks the successive periodic ring cuts needed by a cylindrical
cavity.

`verify.py` builds placed source solids, resolves removal picks through the
public API, and compares the complete emitted body arenas with the pinned Rust
implementation. The matrix covers positive and negative distances, every box
and cylinder removal mask, spheres, no-material boundaries, invalid input,
three placements, survey coordinates and deterministic random scales. The
recorded O0 and O2 runs each pass 72 cases and 65,434 field comparisons,
including 59 valid shell results per mode: 144 executions and 130,868 field
comparisons in total. Native O0 probe compilation took 174.187 seconds. The
compiler SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.

One deliberately large random cylindrical shell is also useful as a focused
runtime sample. The same 532 parsed fields matched in every implementation:
DynLex O0 completed in 18.516 seconds, pinned Rust O0 in 223.422 seconds and
pinned Rust O2 in 22.485 seconds on the verification host. This is an
algorithm-path sample, not a general language benchmark; the constructor
throughput benchmark remains in `docs/cadkernel-performance.md`.

Run the differential check with:

```powershell
python -B tests/cadkernel/brep_shell/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-shell-checks `
  --run-timeout 600
```
