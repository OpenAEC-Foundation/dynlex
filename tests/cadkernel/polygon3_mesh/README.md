# Spatial polygon triangulation verification

Pinned source: `src/space/polygon.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, production lines 79–205.
Both remaining mesh adapters are implemented in `lib/cadkernel/polygon3_mesh.dl`.
The measurement implementation remains in `lib/cadkernel/polygon3.dl`.

## Public patterns

```dynlex
import lib/cadkernel/polygon3_mesh.dl
set soup to the cad spatial triangle soup of ring within tolerance
free soup
set soup to the cad spatial triangle soup of rings boundaries within tolerance
free soup
```

- `ring` is a borrowed pointer to a standard list of `cad vector3` records.
- `boundaries` is a borrowed pointer to a standard list of those ring pointers.
- `tolerance` is `cad tolerance`, normally `a cad tolerance of linear` or
  `the default cad tolerance`. Its constructor refuses nonfinite/nonpositive values.
- Each returned soup is an independently owned standard list of `cad vector3`,
  ordered in consecutive triangle triples. Free it once, including empty results.
  Ordinary pointer copies are borrowed aliases. Inputs are unchanged.
- All filtering, plane selection, projection and lifting use binary64 operations.
  The temporary managed `cad triangulation2` is released after its points have
  been lifted; the output has no dependency on the input or temporary mesh storage.

The single-ring adapter calls the upstream-equivalent simple polygon algorithm;
the multiple-ring adapter uses its even-odd rings algorithm. They preserve the
source's different invalid-component behavior. Every nonfinite source point
refuses the entire operation, including points in rings shorter than three.
Short cleaned rings are skipped; the first retained ring determines the plane,
even if it is degenerate. A failed containment/projection rejects all rings.

## Reproduction

From the repository worktree, using the pinned checkout:

```powershell
C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -B tests/cadkernel/polygon3_mesh/verify.py --source C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546
```

The runner accepts `--compiler` and `--rustc` overrides. Its Windows default
selects the installed MSVC Rust compiler directly, avoiding the rustup shim.
All child processes use `scripts/process_error_mode.py`; `.out` files are
executed by Python subprocess, never file associations.

`verify.py` verifies all six upstream files against their pinned Git blobs,
then compiles an unchanged-source Rust wrapper with `--cfg feature="geom2d"`.
`reference.rs` and `probe.dl` parse the same runtime arguments:
`mode tolerance ringCount [vertexCount x y z ...] ...`, where mode 0 selects
`triangulate` and mode 1 selects `triangulate_rings`.
Both print the soup vertex count followed by all ordered coordinates.
No source-specific production shortcut or geometric substitute is used.

Results with compiler SHA-256
`9682cf3b88de4b6db6c8b82bc3a7ca6517437b81db9dbd7225bcc5d27eb16234`
and `rustc 1.91.1 (ed61e7d7e 2025-11-07)`:

| Check | Result |
|---|---:|
| Existing upstream polygon measurement tests | 7 passed |
| Upstream unit tests specifically for these two adapters | 0 present |
| Adapter fixture | O0 and O2 passed |
| Single-ring differential cases, per optimization level | 434 |
| Multiple-ring differential cases, per optimization level | 700 |
| Total distinct differential inputs | 1,134 |
| Rust scalar comparisons across O0 and O2 | 79,380 exact matches |
| Incorrect list element types | 4 expected compile refusals |
| Invalid tolerance runs, Rust and both native levels | 18 expected failures |

Counts, triangle order, coordinates and zero signs are exact; NaN arithmetic
results are compared by classification. The case groups are 448 shape/winding
variants, 216 ring sets, 24 nesting permutations, 16 tolerance boundaries,
32 deduplication boundaries, 24 coplanarity cases, 90 nonfinite cases,
44 scale cases and 240 deterministic randomized inputs. Cases include empty
and short rings, duplicate endpoints, collinear/degenerate first rings, holes,
even-odd islands, touching/crossing components, bridge insertion, oblique and
survey-coordinate planes, tolerance-adjacent offsets and extreme magnitudes.

The fixed fixture lives in `main.dl` and is registered through
`tests/required/cadkernel_polygon3_mesh/main.dl`. Its expected output is
`cadkernel polygon3 mesh: PASS`. `wrong-point.dl` and `wrong-rings.dl` check
that even empty lists must contain spatial binary64 vector records.

Reproducible build outputs are placed in `build/cadkernel-polygon3-mesh-checks`:
`reference-driver.rs`, `reference.out`, `source-tests.out`, `source-tests.txt`,
`fixture-O0.out`, `fixture-O2.out`, `probe-O0.out`, `probe-O2.out`, type-refusal
diagnostic files and `results.json`. A differential failure writes its exact
arguments and both outputs to `failure.json` before stopping. This is numerical
and source verification, not heap-instrumentation evidence or a compiler-wide
test-suite result.
