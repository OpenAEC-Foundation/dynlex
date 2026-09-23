# Native boundary pcurves

Native port of every production operation in `src/brep/pcurve.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` (683 production lines, 14 source
test groups). Production code is `lib/cadkernel/brep_pcurve.dl`; it does not
call Rust. The MPL-2.0 attribution is retained.

## API

- `the cad projection of curve onto surface within tolerance` returns the
  existing managed `cad pcurve result`.
- `the cad boundary parts of face in body within tolerance` returns
  `cad boundary parts result`: validity and a managed `parts` list whose
  elements have `coedge` and `curve` fields.
- `the cad boundary of face in body within tolerance` returns
  `cad boundary result`: validity and a managed `curves` list.
- `the cad parameter periods of surface` returns `uValid`, `vValid`,
  `u`, and `v`. Read a period only when its validity flag is true.
- `cad surface bounded by boundary contains parameter point within tolerance`
  borrows a raw typed list of `cad curve2` and requires `cad tolerance`.
- `the cad periodic band point on surface bounded by boundary within tolerance`
  returns the existing optional vector representation `cad normalization3`;
  its value is an interior **point**, not a normalized direction.
- `the cad clone of ...` deeply copies boundary parts and boundary results,
  including managed spline and polyline storage.

Projection, boundary extraction and band-point tolerances are binary64.
Face keys, 3D curves, surfaces, parameter points and list element types are
statically constrained. All solvers, sampling, trimming and chain helpers
are file-local. No additional shared API is required.

## Ownership

Inputs and body nodes are borrowed for the call. Results retain their own
managed storage and survive release of the input body, curves and lists.
Ordinary copies share read-only storage; explicit clones allocate independent
lists and nested curve payloads. The `values` fields of managed topology
lists are borrowed views: callers must not free them. Invalid results returned
by the operations have the same managed lifetime as valid results and expose
empty lists. Default/reset values can be copied, cloned and released safely;
inspect validity before using their payload.

Stored coedge pcurves are deeply cloned before chaining. Their presence
bypasses the edge-geometry lookup, matching the source even if the edge key
is dangling. A missing required node or unsupported projection refuses the
entire boundary rather than returning a partial boundary.

## Verification

Verified on 2026-09-16; machine-readable evidence and source hashes are in
[`verification.json`](verification.json):

| Check | Result |
| --- | --- |
| Original Rust test groups | 14/14 at O0 and O2 |
| Native equivalents | 14/14 at O0 and O2 |
| Ownership groups | 5/5 at O0 and O2 |
| Default/reset clone regression | Passed at O0 and O2 |
| Required fixture/mode checks | 6/6 |
| Intentional type rejections | 8/8 |
| Runtime differential cases | 877 |
| Native-to-Rust comparisons | 154,348 |
| Native and Rust O0/O2 numerical parity | Exact under the port comparator |

The default/reset clone regression was first observed failing with Windows
access violation `0xC0000005`. Absent-result cloning now returns an empty
managed result before reading list storage. Both the regression and existing
ownership cases pass after this correction. No compiler change was needed.

Run from the repository root on the verified Windows/MSVC setup:

```powershell
& 'C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -u tests/cadkernel/brep_pcurve/verify.py --source 'C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546'
```

The runner checks the pinned source revision and unchanged source files,
compiles both Rust and DynLex at O0/O2, runs the 14 original groups in each
language, runs the ownership and default-clone fixtures and compile-time type rejections, and
compares runtime-supplied cases. All native processes use the existing
`tests/cadkernel/verify.py::run_process` crash-dialog protection.

Private source operations are reached through four public test adapters
appended to an artifact copy of the Rust source. The original source bytes
are checked to remain intact. These adapters only forward calls; they do not
replace algorithms. This requires rebuilding the full reference crate;
the existing MSVC dependency libraries are reused.

Artifacts and logs live in the ignored `_artifacts/` directory. A successful
unfiltered run produces `summary.json` containing source/compiler hashes,
case counts, comparison counts and optimization-parity evidence.
`--skip-build` reuses the compiled artifacts for numerical diagnosis;
`--case TEXT` selects a subset and writes a separate filtered report.

The differential covers the surface/curve dispatch matrix, geometric
refusals, clockwise conics, exact poles, sampled sphere circles, nonfinite
inputs, arbitrary and survey frames, dangling topology, explicit pcurves,
seam chaining, periodic bands, NURBS periods and boundary containment.
Curve discriminants, counts and coedge identities are exact comparisons;
floating-point checks use the established port comparator, preserving
nonfinite classifications and signed zero. O0/O2 parity is checked separately.

## Source behavior retained

The projection preserves the point locus, not generally the original
parameterization. Unsupported projections return absence; NURBS surfaces
have no general projection in the pinned source. General sphere circles
use the source's closed 96-sample parameter-space polyline, with its seam
closure refusal. This is the source's approximation, not an exact conic.

The nine membership samples, nine trimming samples, five possible seam
shifts per periodic axis, two-band containment fallback, line-only sliding
and planar-spline pass-through all retain the pinned algorithm. The work
does not extend these algorithms' geometric robustness or supported shapes.
Other compilation targets have not been verified by this runner.

The required fixtures declare a 90-second compilation budget. Measured builds
in this shared Windows workspace took approximately 35–77 seconds; runtime
fixtures completed in less than 0.2 seconds. Compilation performance remains
visible rather than being excluded from the test contract.
