# Plane verification

This directory verifies `lib/cadkernel/plane.dl` against the complete
`src/space/plane.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, with its unchanged `space/vec.rs`
dependency. The reference driver imports those files by path; it neither
copies their algorithms nor removes their tests. Inspection includes the end
of the file: there are no production definitions after its test module.

From the repository root, with Python 3, Rust and the built native compiler:

```text
python tests/cadkernel/plane/verify.py --source PATH_TO_PINNED_CHECKOUT
```

Use `--compiler PATH` to select a DynLex compiler and `--rustc PATH` to select
the actual Rust compiler. The latter also permits bypassing a Rust toolchain
launcher whose home-directory lookup is unavailable in a restricted shell.
No Cargo dependencies or network access are required.

The runner checks the source revision and absence of changes to both imported
Rust files, executes all 13 original Rust tests, then verifies the unchanged
[`cadkernel_plane` fixture](../../required/cadkernel_plane/main.dl) at O0 and O2.
It compiles `probe.dl` at both levels and supplies every differential input as
command-line data. Test inputs cannot disappear into constant folding.
Every child process uses the existing guarded `run_process`/`verify_fixture`
utilities, including compiler and mutation executions.

Generated source drivers, binaries, inputs, the Rust test log and `results.json`
live in `build/cadkernel-plane-checks`. A stale success record is removed at
startup. A new record is written only after all comparisons and mutation checks
pass, with SHA-256 hashes of the compiler, module, core/list/std dependencies
and test drivers. These inputs must remain unchanged throughout the run.

## Complete function map

| Pinned source operation | Native pattern or local implementation | Evidence |
| --- | --- | --- |
| `Plane` fields / `from_axes` | `a cad plane with origin …, x axis … and y axis …` | All nine stored components; scaled, skewed, reversed and nonfinite frames. |
| `Plane::XY` | `the cad xy plane` | Constant mode 3 and original XY assertions. |
| `Default::default` | `the cad default plane` | Constant mode 4 and original default assertion. |
| `Plane::orthonormal` | `the cad orthonormal plane with origin …, x axis … and normal …` | Mode 1; normal projection, right-handed second axis, failures and retained origin. |
| `Plane::normal` | `the cad normal of …` | Cross product followed by the source's normalization contract. |
| `Plane::is_orthonormal` | `cad … is orthonormal` | Both squared-length errors and dot product use strict `< 1e-9`. |
| `Plane::point_at` | `the cad point at … on …` | Origin plus the sum of scaled axes; original and random coordinates. |
| `Plane::vector_at` | `the cad vector at … on …` | Sum of scaled axes without origin. |
| `Plane::project` | `the cad projection of … onto …` | Subtract origin, delegate to the Gram solve; off-plane and round-trip queries. |
| `Plane::project_vector` | `the cad vector projection of … onto …` | Same determinant, relative floor and ordered numerators, including skewed frames. |
| `Plane::coordinates_at_xy` | `the cad coordinates at … on …` | World-Z intersection, input/determinant finiteness and inclusive determinant floor. |
| `Plane::distance_to` | `the cad distance from … to …` | Optional signed distance, reversed normals and collapsed axes. |
| `Plane::contains` | `cad … contains … within …` | Optional distance and inclusive tolerance, including negative/NaN/infinite tolerance. |
| `Plane::is_xy` | `cad … is xy` | Exact origin plus basis equality; signed zeros compare equal. |
| `Plane::is_xy_aligned` | `cad … is xy aligned` | Exact axes independent of origin; near-XY is distinguished. |
| `coplanarity_tolerance` | `the cad coplanarity tolerance of …` | Empty floor; extent and coordinate roundoff; nonfinite inputs and finite overflow. |
| `are_coplanar` | `cad … and … are coplanar` | Rank, order, points and directions, threshold-adjacent data and randomized cases. |
| `COPLANARITY_TOLERANCE`, local orthonormal tolerance | Local `cad plane epsilon` | Same `1e-9`, with the distinct strict/inclusive comparisons above. |
| `f64::MIN_POSITIVE` in both solvers | Local `cad plane minimum positive` | Exact binary64 minimum normal, underflow and tiny-frame cases. |
| Finite iterator predicates | Local `cad plane list … is finite` | Every coordinate, including invalid directions with no points. |
| First-axis `find_map` | Local `the cad coplanarity normal of …` | First useful cross in original order; no axis/no independent axis paths. |
| Offset/direction `all` predicates | Local `cad plane offsets … and directions … fit normal … within …` | Inclusive distance, relative direction tolerance and squared-length underflow. |

Record copying replaces Rust's `Copy`/`Clone`; no geometry mutates caller
records. `is_xy` implements the equality needed by the source method directly.
Rust debug formatting is not an additional geometric operation.
The two temporary coplanarity lists are freed on their common exit; input lists
are borrowed, compared again after the call and freed only by the probe.

## Original assertion audit

The section comments in the existing required fixture retain every source test
name below. The audit compares assertions and iteration inputs, not just labels.

| Original test | Assertions preserved in the DynLex fixture |
| --- | --- |
| `the_xy_plane_carries_coordinates_through_unchanged` | Point, optional projection, optional normal, XY identity and default XY. |
| `projecting_inverts_point_at_on_an_orthonormal_frame` | Successful construction; both coordinates for all three UV inputs, `< 1e-12`. |
| `projecting_inverts_point_at_on_a_skewed_frame_too` | Both coordinates for all four UV inputs, `< 1e-12`. |
| `dot_products_would_have_disagreed_on_the_skewed_frame` | Naive U equals 2 within `1e-12`; valid projection rounds U to exactly 3. |
| `a_direction_projects_without_the_origin` | XZ Y-axis equals −Z; vector and optional inverse exactly match; skewed vector inverse also matches. |
| `a_point_off_the_plane_projects_along_the_normal` | Optional projection, exact +94/−4 distances, included and excluded points. |
| `orthonormal_squares_up_an_axis_that_leans_off_the_plane` | Orthonormal flag, X component near 1, Z near 0, exact optional normal. |
| `orthonormal_is_right_handed` | Successful construction and exact +Y second axis. |
| `orthonormal_refuses_an_axis_along_the_normal` | Parallel axis, zero normal and zero axis all refuse. |
| `a_collapsed_frame_reports_itself_rather_than_answering` | Normal, projection and distance refuse; containment is false. |
| `is_orthonormal_tells_the_two_kinds_of_frame_apart` | XY true; skewed/scaled false; scaled frame still has exact unit normal. |
| `a_tilted_plane_places_its_own_axes_at_unit_distance` | Exact unit X; Y/Z within `1e-12` of ±1/√2; resulting point contained. |
| `survey_coordinates_survive_the_round_trip` | Same survey origin and millimetre UV; both errors `< 1e-9`. |

Additional existing fixture assertions check XY intersection, scale thresholds,
exact basis, distance boundaries, coplanarity, nonfinite behavior and ownership.
The differential suite supplements these independent expected-value assertions.

## Precision, refusals and test sensitivity

Both reference and native probes print enough digits to round-trip binary64.
Numeric fields allow relative error `3e-13` with **zero absolute tolerance**.
Flags, copied input values and output field order match exactly. A zero cannot
match a nonzero value; zero signs must agree. Infinities must have the same sign.
NaNs compare by classification, without requiring a payload or sign.
O0 and O2 require exact numeric equality with the same signed-zero/NaN rules;
this is not a bytewise comparison of NaN payloads.

The suite intentionally preserves source behavior: `project_vector` can return
`Some(NaN)` when its determinant is NaN; XY coordinates additionally reject a
nonfinite query/determinant but do not validate origin; orthonormal construction
also retains origin. Finite points whose extent calculation overflows can yield
infinite tolerance and no usable coplanarity axis. Squared-length underflow can
classify a very small direction as zero. These are source semantics, not added
robustness guarantees.

Each full run also compiles four deliberately faulty **build-directory copies**:
a dot product replacing the Gram solve, strict containment, unsigned distance,
and unscaled direction tolerance. Each must produce a mismatch in its named
numeric/Boolean field. A compiler error does not count as a detected mutation.
Production files are never modified by these checks.

Verified dataset: **947 cases, 61,962 Rust comparisons and 30,981 exact O0/O2
pairs**, plus 13 unchanged Rust tests and both required-fixture modes. Counts
include echoed input coordinates used to check the borrowing contract. The
runner reports separate mutation evidence and input hashes in `results.json`.
Source review found no algorithmic discrepancy requiring a production edit.
This result covers this module, not completion of the entire CAD port.
