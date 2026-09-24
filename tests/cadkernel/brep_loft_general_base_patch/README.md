# General loft homogeneous base patches

This sidecar ports the profile-only `base_patch`, `derivative`, and
`section_derivative` preparation from pinned `src/brep/loft_general.rs` at
revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It accepts the
existing aligned profile result, computes each band length including the
source's coincident-centre fallback, subdivides corresponding wires at the
union of exact span starts, normalizes homogeneous endpoint weights, elevates
degrees, and returns one homogeneous patch per span for a chosen wire and band.

Settings cover open or cyclic section sequences; periodic smoothing on or off
for cyclic sequences; and normal modes 0 (ruled), 1 (smooth), 2 (first normal),
3 (last normal), 4 (both ends normal), 5 (all normal), and 6 (endpoint draft).
The `cyclic` flag must match the one used to construct the aligned input;
the earlier alignment result does not store that flag.
Mode 6 uses finite start/end draft angles and nonnegative start/end tangent
magnitudes; zero magnitude selects the source-derived speed. Input alignment
can be enabled or disabled in the preceding module. `surface` does not affect
base patch controls. Point-section continuity and bulge settings are outside
the supported profile-only input.

The differential harness checks the pinned source revision and file hashes,
builds the pinned Rust functions and DynLex at O0/O2, and compares refusal
status, band lengths, patch intervals, row counts, and all homogeneous
controls. The required fixture also checks 500 result-retention iterations
with nested control rows after the input and aligned result are released.

Run from the repository root:

```text
python -B tests/cadkernel/brep_loft_general_base_patch/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_loft_general_base_patch
```

This module produces patch control grids, not surfaces or a public B-rep body.
Guide/path corrections, point-ended lofts, surface validation, caps,
topological sewing, and body assembly remain outside this slice. A failed
construction returns no partial patches.
