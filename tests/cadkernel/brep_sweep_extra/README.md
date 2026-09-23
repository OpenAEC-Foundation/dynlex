# Additional closed extrusion source tests

This fixture translates seven non-mesh groups from
`src/brep/sweep.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

It imports `tests/cadkernel/brep_sweep_extrude/main.dl`, which executes the
three existing source groups and supplies `check extrusion`,
`extrusion point` and `extrusion square of size ...`. The expected output
therefore contains ten source-group names.

The fixture calls the native
`the cad extrusion of profile on plane along direction` API. Profiles and
temporary arena-key lists are freed by the fixture; constructed bodies retain
their own topology and geometry.

## Assertion mapping

| Source group | Preserved checks |
| --- | --- |
| `a_profile_that_encloses_nothing_is_refused` | The closed collinear three-line profile is refused. |
| `a_profile_with_an_arc_in_it_sweeps_into_a_cylinder_wall` | Extrusion succeeds; validation finds no flaws; exactly one surface is a cylinder; bounds exist; maximum X differs from 15 by less than `1e-9`. |
| `an_extrusion_on_a_tilted_plane_goes_where_the_plane_points` | Orthonormal frame construction and extrusion succeed; validation finds no flaws; bounds exist; maximum Y differs from 5 by less than `1e-9`. |
| `a_sweep_along_the_profiles_own_plane_is_refused` | The square swept in its own plane is refused. |
| `a_profile_that_does_not_close_is_refused` | Both the three-line open profile and the first two sides of a unit square are refused. |
| `a_spline_profile_extrudes_as_an_exact_nurbs_wall` | The degree-2 spline with three original control points, automatic knots and unit weights is constructed; extrusion succeeds; validation finds no flaws; at least one surface is NURBS. |
| `an_extrusion_at_survey_coordinates_is_the_same_solid` | The half-unit square extrudes at the original survey origin; validation finds no flaws; Euler characteristic is 2; worst vertex gap is below `1e-6`. |

The source's `expect` and `unwrap` success requirements become explicit
validity assertions. Scalar thresholds, profile coordinates, frame axes,
extrusion vectors, surface-variant counts and existential checks are unchanged.
Mesh-dependent groups are outside this fixture.

## Verification

Compiler SHA256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.

Both this directory and
`tests/required/cadkernel_brep_sweep_extra` declare an explicit
`compile_timeout_seconds.txt` of 60. The required wrapper imports this
fixture and has the same ten-line expected output.

The seven original Rust groups passed at O0 and O2 using the existing complete
pinned-crate test binaries in `build/brep-intersect-checks`. The sweep source
hash was checked against their recorded source manifest before running them.
Evidence: `build/brep-sweep-extra-source-results.json`.

Native required-fixture results are recorded in
`build/brep-sweep-extra-required.log`. All executable launches use
`tests/cadkernel/verify.py::run_process`, including the original Rust tests.

Verified on 2026-09-16: **2/2 native fixture/mode combinations passed**, with
all ten output groups and no compiler diagnostics. O0 compiled in 49.516 seconds
and ran in 0.203 seconds; O2 compiled in 48.859 seconds and ran in 0.141 seconds.
The compiler retained the pinned hash before and after verification. The seven
source groups also passed in both modes (**14/14** original Rust checks).
Artifacts: `build/focused-qt3rp3ox`. No algorithmic failure was observed.

Run from the repository root:

```powershell
& 'C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' build/check-fixtures-local.py cadkernel_brep_sweep_extra
```

The selected runner compiles and executes O0/O2, compares the complete expected
output, rejects unexpected diagnostics, and checks that the compiler hash
remains unchanged. This is evidence for these seven added groups and the three
imported groups, not a complete sweep-module or compiler-suite verification.
