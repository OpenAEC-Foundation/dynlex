# Region sweep differential

`verify.py` compares the pinned Rust `extrude_surface_region`,
`extrude_surface_region_tapered`, `extrude_region`, and
`extrude_region_tapered` operations with their native DynLex counterparts.
Both probes receive the same numeric input. For valid bodies they compare all
nine arena counts, roots and shell/face grouping, validation count, vertex
coordinates, surface kinds and NURBS data, face orientation, and coedge
parameter curves. Refusals compare their validity result. Rust and DynLex are
each run at O0 and O2; native output must match exactly across modes.

The 46 cases include two and three independent sheets, solid regions with one
or two holes, circle splitting, closed-polyline splitting, full and segmented
arcs, oblique and reversed directions, positive and negative taper, growing
holes that cross the top cap or each other, and empty, touching, nested, and
collapsed inputs. The verifier checks the pinned
source revision and the hashes of both `sweep.rs` and the profile-expansion
source, then records the compiler and reference-library hashes in `summary.json`.
It requires debug and release reference libraries with the `brep` and `offset`
features under `build/cadkernel-upstream-offset-reference`.

From the DynLex repository root:

```powershell
& 'C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -B tests/cadkernel/brep_sweep_region_diff/verify.py --source 'C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546'
```

All 46 cases pass at O0 and O2: 8,416 numeric field comparisons with exact
native optimization parity. The oblique circular taper case guards the ruled
surface fallback when an analytic cone cannot represent the direction.
Filtering with `--case` or `--exclude-case` is available for diagnostics;
filtered runs write `filtered-summary.json`.
