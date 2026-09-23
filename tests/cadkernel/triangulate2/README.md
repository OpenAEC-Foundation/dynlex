# Triangulation verification

Source: cadkernel `src/geom2d/triangulate.rs`, pinned to
[`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`](https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/geom2d/triangulate.rs).
The complete production module is `lib/cadkernel/triangulate2.dl`.

Run from the repository root:

```powershell
C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -B tests/cadkernel/triangulate2/verify.py --upstream C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546
```

The runner verifies the pinned Git blobs, compiles the original Rust source and
runs exactly its 12 triangulation unit tests. It separately compiles and executes
`tests/required/cadkernel_triangulate2/main.dl` and
`tests/required/cadkernel_triangulate2_ownership/main.dl` with their adjacent
`expected.txt` files at O0 and O2, and checks the two negative type fixtures.
Windows child processes run under
`scripts/process_error_mode.py`; `.out` files are launched through subprocess,
without shell file associations. Temporary files remain inside a checked test
subdirectory and are removed at completion.

The Rust target defaults to `x86_64-pc-windows-msvc` on Windows so circle tests use
the same C math runtime as native DynLex. Override with `--rust-target` on another
platform. `--compiler` and `--rustc` select explicit executables; repeated `--mode`
arguments select differential groups for investigation.

## Public API and ownership

All coordinates and tolerances are binary64. A ring is a borrowed pointer to a
standard list of `cad vector2`. A ring collection is a borrowed pointer to a list
of those ring pointers. Even empty collections are checked for the correct
payload type at compilation. Input lists remain unchanged.

| Pattern | Result |
| --- | --- |
| `the cad triangulation of outer with holes holes` | `cad triangulation2` with owned `points` and `triangles` |
| `the cad triangulation of rings boundaries` | Same, applying even-odd nesting and component validation |
| `the cad polygon frame of points within tolerance` | `cad polygon frame result` with `valid` and `value` |
| `the cad ring nesting depths of boundaries` | Owned standard integer list, in input order; call `free depths` |
| `the cad clone of mesh` | Deep clone of `cad triangulation2` |
| `the cad clone of frame` | Deep clone of `cad polygon frame` |
| `the cad clone of result` | Deep clone of `cad polygon frame result`, including invalid results |
| `cad frame equals other` | Content equality for frames, following Rust scalar equality |
| `free cad triangulation mesh` | Reset the caller variable and release its reference |
| `free cad polygon frame frame` | Same for a frame |
| `free cad polygon frame result result` | Same for an optional frame result |

`cad triangle indices` has zero-based integer fields `first`, `second`, `third`.
A frame has `origin` and `size` as `cad vector2`, and `points` as an owned list.
An invalid frame result still has safely managed empty point storage.

Ordinary result/frame copies retain shared storage and release it on scope exit.
Their list fields are borrowed views, valid while an owner survives. Do not free
those fields, and keep shared views read-only. Use `the cad clone of ...` before
independent mutation. Releasing one copy leaves another valid. Explicit free is
a caller-scope reset, safe to repeat; a reset value must be assigned a new result
before using its list fields. Constructors adopt only internal fresh lists;
public geometry functions borrow their inputs.

`polygon` preserves the source's permissive behavior for raw rings. Cleanup and
validation belong to `polygon_frame`, `nesting_depths` and `rings` where the source
performs them. Winding, repeated closing points, stable hole sorting, bridge
crossings, ear-score ties, triangle order, component order and omission are all
compared directly.

## Differential coverage

`differential.dl` imports the production library. `helpers.dl` is appended to an
unchanged copy of that library in a temporary file, so private helpers can be
tested without exporting them. `probe.rs` is similarly appended inside the
unchanged pinned Rust module. `verify.py` supplies all inputs at runtime.

| Mode | Cases per optimization level | Tested source behavior |
| --- | ---: | --- |
| 0 | 225 | `polygon`, including holes and point/triangle ordering |
| 1 | 423 | `polygon_frame`, cleanup, distance tolerance and rejection |
| 2 | 164 | `nesting_depths`, including arbitrary order and empty/invalid rings |
| 3 | 276 | `rings`, islands, interacting invalid components and complete-component omission |
| 4 | 227 | `signed_area_arrays`/`signed_area`, `sanitize`, `orient`, `simple`, `RingBounds::of`/`overlaps`, `rightmost`, containment, point-on-boundary, boundary intersection/proper crossing and `rings_interact` |
| 5 | 152 | Both segment intersection predicates, tolerance variant, `point_on_segment`, triangle `inside` |
| 6 | 44 | `triangulation_matches`, invalid indices, degeneracy, nonfinite values and area tolerance |
| 7 | 30 | `is_ear`, `ear_score`, `clip_ears`, `bridge_target`, `bridge`, including mutated point/index order |
| 8 | 12 | All 144 pairings of signed NaNs, infinities, finite extrema, subnormals and signed zero through the `total_cmp` port |
| 9 | 6 | `root_of`, including chains and multiple roots |
| Total | 1,559 | 3,118 native/Rust comparisons across O0 and O2 |

Numeric outputs and index ordering compare exactly, without epsilon relaxation.
NaN payloads/signs in arithmetic results are compared as NaN classes. Original
point coordinates and signed areas retain signed-zero checks. Only bounds/origin
fields produced by `f64::min`/`max` admit either sign for equal zeros, as specified
by the [Rust floating-point contract](https://doc.rust-lang.org/std/primitive.f64.html#method.min).
The total-order comparator separately distinguishes signed zero and signed NaN.

The fixtures also test content equality, independent cloning, shared-copy
lifetimes, returning owners from functions, managed-list reallocation/removal,
repeated reset and invalid optional-result lifetimes. They do not claim a heap
instrumentation or whole-kernel validation run.
