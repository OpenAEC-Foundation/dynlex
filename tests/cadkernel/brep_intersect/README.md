# Native surface intersections

Complete native port of `src/brep/intersect.rs` (449 production lines,
19 source tests) at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
Production code: [brep_intersect.dl](../../../lib/cadkernel/brep_intersect.dl).
The Rust reference links the complete unchanged pinned crate; no intersection
algorithm is reimplemented in the reference driver.

## API and ownership

`the cad meeting of firstSurface and secondSurface within tolerance`
borrows two `cad surface` values and returns a managed `cad surface meeting`.
Its `kind` identifies the active payload:

| Kind | Source variant | Payload |
| --- | --- | --- |
| 0 | None | No intersection |
| 1 | Curves | Ordered `curves` list of `cad edge curve3` |
| 2 | Points | Ordered `points` list of `cad vector3` |
| 3 | Coincident | Same supporting surface |
| 4 | Unknown | No implemented exact answer |

Named empty variants are `no cad surface meeting`,
`a coincident cad surface meeting`, and
`an unknown cad surface meeting`. Unknown is never interpreted as None.
The list fields are immutable borrowed views; do not mutate or free them.

Ordinary copies retain storage. `the cad clone of meeting` allocates
independent lists and deep-clones curve payloads. Equality preserves variant
and ordered payload semantics, including ordinary binary64 NaN inequality.
`free cad meeting value` resets that value to its null-safe None state;
retained copies stay valid. A reset value can be compared, cloned and released
again. Read list payloads only for their active variant.

## Source behavior retained

- Plane/plane, plane/sphere, sphere/sphere and plane/cylinder closed forms.
- Perpendicular plane/cone sections, including the mirrored cone half.
- Parallel-axis cylinder intersections and coaxial sphere/cylinder latitudes.
- Coaxial cone/cylinder and cone/cone intersections with both cone halves,
  reversed axes, root deduplication and early coincidence detection.
- Unsupported surface pairs, oblique cone sections and invalid required
  frames return Unknown exactly where the source does.
- Branch order, binary64 arithmetic association, signed zero, non-finite
  tolerance behavior and curve ordering remain unchanged. The fixed
  coaxial-angle threshold of `1e-9` and conic denominator threshold of
  `1e-12` are distinct from the caller's tolerance.

No numerical marching, geometric fallback or new compiler intrinsic is used.
Implementation helpers are file-local. Existing CAD geometry, plane, vector,
list and lifecycle patterns provide the shared operations.

## Verification results

Verified on 2026-09-16 with compiler SHA256
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`
and Rust 1.91.1, `x86_64-pc-windows-msvc`.

- All **19 original Rust tests**, O0 and O2.
- All **19 translated groups**, preserving every original assertion, O0 and O2.
  Curve groups sample 13 points on every returned curve against both surfaces.
- **5 additional analytic groups**, covering latitude order, reversed axes,
  both cone halves, zero-radius refusals, cone/cone solutions, coincident and
  parallel denominators, mirrored sections, cylinder tangency and degenerate
  required frames.
- **2 lifecycle groups**, including 100 repeated local returns, retained copies,
  independent clones, reset, storage in raw lists and empty-variant distinctions.
- **2,449 differential cases / 189,048 scalar and classification comparisons**.
  Each case checks both argument orders against Rust at both optimization levels.
  All 36 ordered surface-type pairs, raw frames, non-finite and subnormal
  coordinates/radii/tolerances, threshold neighbours, near-coaxial axes,
  conic denominator boundaries and deterministic random cases are included.
- Exact O0/O2 parity for the complete emitted Rust and DynLex results, including
  signed zero. Cross-language finite comparisons use relative tolerance
  `3e-12` with no absolute allowance; discrete fields, infinities, zero signs
  and NaN classification are checked separately.
- All **6 required fixture/mode combinations** passed without diagnostics.
  Their explicit compilation timeout is 60 seconds. Measured compilation was
  3–8 seconds on this host.

The test-first fixture failed because the module was missing before implementation.
The lifecycle fixture subsequently caught an inactive-list read when cloning a
reset result; cloning now accesses only the active variant. No C++ or shared
library changes were needed.

Full differential evidence: `build/brep-intersect-checks/summary.json` and
`build/brep-intersect-checks.log`. Required checks:
`build/brep-intersect-required.log`.
The summary records source-file, native-file, compiler and reference-library
hashes. These are module checks; they do not claim a new run of the entire
compiler suite or completion of the whole CAD port.

## Reproduction

From the DynLex repository root:

```powershell
& 'C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/cadkernel/brep_intersect/verify.py --source C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546 --libraries build/brep-make-six-checks --dependencies build/topology-reference-deps/target/debug/deps --output build/brep-intersect-checks
```

The runner compiles the full source test crate with the `geom2d` and `brep`
features and executes the original intersection tests. It links the reference
driver to `libcadkernel-O0.rlib` and `libcadkernel-O2.rlib` from the specified
library directory, verifies the source revision and clean source content, then
builds and runs the native fixtures and standalone probe. All executable
launches use the shared `tests/cadkernel/verify.py` process runner, which
suppresses Windows crash dialogs.

Use `--case substring` for a filtered run and `--jobs 1` for sequential
execution. `--skip-build` requires previously compiled artifacts in the same
output directory; use it only if the compiler, inputs and sources are unchanged.

Required fixtures:

- `tests/required/cadkernel_brep_intersect`
- `tests/required/cadkernel_brep_intersect_analytic`
- `tests/required/cadkernel_brep_intersect_ownership`
