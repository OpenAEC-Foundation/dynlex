# Native B-rep placement and edge sampling

The implementation is `lib/cadkernel/brep_place.dl`, ported from the complete
`src/brep/place.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
All geometry and topology operations execute in DynLex. Rust is used only by
the independent reference executables.

## API and source correspondence

| Source operation | DynLex pattern |
| --- | --- |
| `Placement::IDENTITY` | `the cad identity placement` |
| `Placement::at` | `a cad placement at origin` |
| Placement fields | `a cad placement with axes x, y and z at origin` |
| `Placement::scale` | `the cad scale of place` (`valid`, `value`) |
| `Placement::reflects` | `place reflects cad orientation` |
| `Placement::point` | `the cad image of point under place` |
| `Placement::vector` | `the cad direction of direction under place` |
| `transform` | `the cad transformed body by place` (`valid`, `value`) |
| `edge_samples` | `the cad edge samples of key in body within sag` |
| `edge_points` | `the cad edge points of key in body within sag` |
| `edge_polylines` | `the cad edge polylines of body within sag` |

Placement and edge samples support `the cad clone of value` and `=`.
Internal frame, curve, surface, pcurve and subdivision operations remain local
patterns. Binary64 evaluation order, permissive spline reconstruction,
periodicity, V reversal, provenance and topology keys are retained.

A transformation accepts only the source's similarity test. Curve frames map
both axes independently; analytic surface frames carry the mapped normal.
Only reflected NURBS faces reverse face sense. Pcurves receive the appropriate
surface-dependent scale and handedness; an unsupported pcurve is removed,
as in the source. The complete ordered topology-flaw list must remain equal
to that of the input, which can already contain flaws.

Sampling retains parameters and order, including reversed and zero intervals.
Circular sampling uses the original 32-chord fallback and 2–256 chord clamp,
including Rust's saturating float-to-integer behavior for nonfinite values.
Spline subdivision retains the midpoint criterion, `1e-12` sag floor and
depth limit of 16. It deliberately preserves the source's midpoint-only
sampling behavior.

## Ownership

Inputs are borrowed. A successful body transformation owns independent arenas,
adjacency, roots and spline data. Its ordinary copies retain managed storage;
explicit body clone is deep. Failed transformations release partial clones
without consuming or changing the source.

Sample/point results have `valid` and `value`; `value` is a managed
`cad topology list`, with a borrowed `values` list. Invalid results own an empty
list and need no special cleanup. Polylines are a managed list of managed point
lists. Ordinary copies retain the owners, including copies returned from local
scopes or stored in lists. **Do not manually free the borrowed `values` fields.**
Assigning a zero value of the same type releases that variable's reference.

## Verification

`verify.py` checks the pinned revision and unchanged Rust source, runs the seven
original source tests at both optimization levels, then compares the native
probe against separately compiled Rust O0 and O2 references. Existing full-crate
MSVC libraries can be supplied with `--libraries`; their hashes are recorded.
The reference retains its real geometry and meshing dependencies.

```powershell
& 'C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/cadkernel/brep_place/verify.py --source 'C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546' --output build/brep-place-checks
```

Each fixture and probe has a 60-second compilation limit. All native and Rust
executables run through `tests/cadkernel/verify.py::run_process`, including
crash-dialog suppression on Windows. `--skip-build` reuses binaries in the
specified output directory; only use it when their sources are unchanged.
`--case` selects a diagnostic subset and writes a separate filtered summary.

The seven native groups retain the source test names and check placement,
radii, outward orientation, similarity refusals, survey coordinates, straight
and curved sampling, and tolerance refinement. Test-only boundary integration
checks signed volumes. It is an independent geometric oracle, **not a port of
the production B-rep mesher**. The original Rust tests run their unchanged mesher.

Additional fixtures cover independent arenas and spline lists, managed
copies/local returns/list insertion/reset, failed-clone cleanup, missing keys,
reflection parameters and pcurves, pcurve refusal, and the depth-16 limit.

The differential corpus includes all five edge-curve and six surface variants,
all eight pcurve variants, primitive and intentionally flawed bodies, topology
removals, dangling owners, duplicate coedges, zero/negative/reversed intervals,
similarity threshold boundaries, random similarities, signed zero, subnormals,
infinities and NaNs. It compares stored topology, provenance, geometry, spline
controls/knots/weights/flags, pcurves, validity, every sample parameter/point and
the complete wireframe. Counts and flags compare exactly. Other finite numbers
use relative tolerance `3e-12`; zero signs and nonfinite classifications are
checked. O0/O2 parity is checked exactly within each language.

Artifacts include source/compiler/library hashes, original test output,
compiled probes, the current differential input and outputs, and `summary.json`.
The required fixtures are `cadkernel_brep_place`,
`cadkernel_brep_place_ownership`, and `cadkernel_brep_place_edges`.

This verification targets the Windows MSVC reference and native CPU backend.
It does not establish behavior on WebAssembly, GPU or other C runtimes.

## File inventory

All paths below are relative to the repository root.

```text
lib/cadkernel/brep_place.dl
tests/cadkernel/brep_place/README.md
tests/cadkernel/brep_place/checks.dl
tests/cadkernel/brep_place/compile_timeout_seconds.txt
tests/cadkernel/brep_place/edge_cases.dl
tests/cadkernel/brep_place/emit.dl
tests/cadkernel/brep_place/expected.txt
tests/cadkernel/brep_place/main.dl
tests/cadkernel/brep_place/oracles.dl
tests/cadkernel/brep_place/probe.dl
tests/cadkernel/brep_place/reference.rs
tests/cadkernel/brep_place/support.dl
tests/cadkernel/brep_place/verify.py
tests/cadkernel/brep_place_ownership/compile_timeout_seconds.txt
tests/cadkernel/brep_place_ownership/expected.txt
tests/cadkernel/brep_place_ownership/main.dl
tests/required/cadkernel_brep_place/compile_timeout_seconds.txt
tests/required/cadkernel_brep_place/expected.txt
tests/required/cadkernel_brep_place/main.dl
tests/required/cadkernel_brep_place_edges/compile_timeout_seconds.txt
tests/required/cadkernel_brep_place_edges/expected.txt
tests/required/cadkernel_brep_place_edges/main.dl
tests/required/cadkernel_brep_place_ownership/compile_timeout_seconds.txt
tests/required/cadkernel_brep_place_ownership/expected.txt
tests/required/cadkernel_brep_place_ownership/main.dl
```
