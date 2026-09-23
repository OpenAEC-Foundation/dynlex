# Pinned cadkernel numerical reference

An independent-input Rust reference executable for the native DynLex port. It calls the unchanged cadkernel main crate at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f` with `default-features = false` and features `geom2d,brep`.

This executable does not test optional offset/ACIS operations, run the upstream 672-test suite, or establish native DynLex coverage. Those remain part of the full main-crate delivery. The separate constraint crate is outside that delivery and is not a dependency of this reference.

## Interface

From the repository root:

```sh
cargo run --quiet --offline --locked --manifest-path tests/cadkernel/reference/Cargo.toml --target-dir build/cadkernel-reference --release
```

No executable arguments are accepted. stdout is UTF-8 plain text, one unique `label=value` per line in fixed order. Scalars use scientific notation with 17 digits after the decimal point; vector components are comma-separated without brackets or spaces. Integers use decimal notation; booleans are `true` or `false`. The header includes `reference.version=1` and the upstream revision. The trailer includes `reference.checks=<number of checked result records>` and `reference.status=ok`.

Every value is checked against independently computed expected quantities before any result records are emitted. Non-finite numerical results or absolute errors greater than `1e-10` fail. Integer/boolean checks are exact. Failed checks panic with a labeled diagnostic on stderr and a nonzero exit status; invalid arguments also fail. Consumers must check the exit code and success trailer, reject duplicate/missing labels, and compare numerical values with the documented tolerance instead of requiring identical decimal rounding on every platform.

Build diagnostics go to stderr. Build artifacts belong under the ignored `build/` directory, not beside these sources. The lockfile retains the cached benchmark's enabled dependency resolutions and omits unrelated benchmark dependencies.

## Independent quantities

| Fixture | Inputs and expected quantities |
| --- | --- |
| Vec2 | a=(3,4), b=(-2,5): squared length 25, length 5, dot 14, oriented cross 23, unit direction (3/5,4/5); segment distances 4 and 5, including a collapsed segment |
| Vec3 | a=(2,-3,6), b=(-1,4,2): squared length 49, length 7, dot -2, cross (-30,-10,5); zero/tiny direction refusal, opposite-direction parallelism and non-finite detection |
| Angles | Normalize -pi/2 to 3pi/2; wrapped half turn has span pi and midpoint parameter 1/2; equal endpoints represent a full turn; inclusive endpoints and excluded opposite direction |
| Frame | Opposite corners (1200000,-4500000,8) and (1200008,-4499994,12): midpoint (1200004,-4499997,10), local half extents (4,3,2); empty input gives the identity frame |
| Plane | Origin (10,20,30), axes (2,0,0),(1,3,0), uv=(2,-1): point (13,17,30); off-plane projection retains uv and signed distance is 5; reverse orientation gives -5; collinear axes are refused |
| Box | Size 2x3x4, centered at zero, unit density: V=abc=24, area=2(ab+bc+ca)=52; inertia=(V(b^2+c^2)/12,V(a^2+c^2)/12,V(a^2+b^2)/12)=(50,40,26) |
| Box topology | Eight vertices, twelve edges, twenty-four coedges, six loops/faces, one shell/lump; V-E+F=2 and no validation flaws |
| Survey box | Same dimensions at (1200000,-4500000,10): volume 24, area 52, centroid (1200001,-4499998.5,12), no topology flaws |

Box mass comes from `brep::mesh::Mesh::inertial_properties` / `mass_properties`. The upstream `analytic_mass_properties` entry point recognizes spheres/cylinders, not boxes. These fixtures use planar box faces, whose triangulation does not introduce curved-surface approximation. Zero and negative box extents must be refused.

## Source and license

Upstream source and notices: [cadkernel at the pinned revision](https://github.com/HakanSeven12/cadkernel/tree/953d546b68aef4b6692566a1a9b077fc5bd9fb4f), [MPL-2.0 LICENSE](https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/LICENSE). Attribution is to the upstream cadkernel contributors. The harness calls their public APIs; it contains no copied kernel implementation. Cargo uses the upstream source and its notices unchanged. The harness source carries the MPL-2.0 source-form notice.

The separate `cadkernel-constraints` manifest declares `LGPL-2.1-or-later`; that solver and `nalgebra` are neither linked nor covered by these fixtures. Their license must not be replaced with the main crate's MPL-2.0 attribution.

## Recorded execution

Executed on 2026-09-12 on `x86_64-pc-windows-gnu` with `rustc 1.94.0 (4a4ef493e 2026-03-02)` and `cargo 1.94.0 (85eff7c80 2026-01-15)`. The DynLex checkout HEAD was `332dac1385edcbe6458386a5119b4cc62d010581`; this Rust reference does not invoke the DynLex compiler.

| Check | Actual result |
| --- | --- |
| Offline, locked release build and execution using the command above | Exit 0; all 80 checked result records passed |
| Offline, locked debug build and execution, same command without `--release` | Exit 0; stdout identical to the release output below |
| Repeated release executable invocation | Exit 0; stdout identical |
| Release executable with `--unexpected` | Exit 101; empty stdout; usage diagnostic on stderr |
| Output structure | 84 unique labels: 2 header records, 80 checked result records, 2 trailer records |
| `cargo fmt --manifest-path tests/cadkernel/reference/Cargo.toml -- --check` | Exit 0 |

The final incremental release build plus execution took 1.015 seconds on this host; this is a warm-cache measurement, not a clean build or a DynLex compile timing. The first release and debug builds both used the existing caches offline. No network access or permission escalation was required. Rustup emitted `warn: could not canonicalize path: 'C:\Users\rickd'` on stderr; the builds and executions still exited successfully.

Exact release stdout (debug and repeat output matched):

```text
reference.version=1
reference.upstream=953d546b68aef4b6692566a1a9b077fc5bd9fb4f
vec2.add=1.00000000000000000e0,9.00000000000000000e0
vec2.subtract=5.00000000000000000e0,-1.00000000000000000e0
vec2.scale=6.00000000000000000e0,8.00000000000000000e0
vec2.divide=1.50000000000000000e0,2.00000000000000000e0
vec2.negate=-3.00000000000000000e0,-4.00000000000000000e0
vec2.dot=1.40000000000000000e1
vec2.cross=2.30000000000000000e1
vec2.length_squared=2.50000000000000000e1
vec2.length=5.00000000000000000e0
vec2.normalize=5.99999999999999978e-1,8.00000000000000044e-1
vec2.perpendicular=-4.00000000000000000e0,3.00000000000000000e0
vec2.lerp=1.75000000000000000e0,4.25000000000000000e0
vec2.segment_interior=4.00000000000000000e0
vec2.segment_endpoint=5.00000000000000000e0
vec2.segment_collapsed=5.00000000000000000e0
vec2.zero_refused=true
vec2.tiny_refused=true
vec3.add=1.00000000000000000e0,1.00000000000000000e0,8.00000000000000000e0
vec3.subtract=3.00000000000000000e0,-7.00000000000000000e0,4.00000000000000000e0
vec3.scale=4.00000000000000000e0,-6.00000000000000000e0,1.20000000000000000e1
vec3.divide=1.00000000000000000e0,-1.50000000000000000e0,3.00000000000000000e0
vec3.negate=-2.00000000000000000e0,3.00000000000000000e0,-6.00000000000000000e0
vec3.dot=-2.00000000000000000e0
vec3.cross=-3.00000000000000000e1,-1.00000000000000000e1,5.00000000000000000e0
vec3.length_squared=4.90000000000000000e1
vec3.length=7.00000000000000000e0
vec3.normalize=2.85714285714285698e-1,-4.28571428571428548e-1,8.57142857142857095e-1
vec3.lerp=5.00000000000000000e-1,5.00000000000000000e-1,4.00000000000000000e0
vec3.segment_collapsed=7.00000000000000000e0
vec3.zero_refused=true
vec3.tiny_refused=true
vec3.antiparallel=true
vec3.zero_parallel=false
vec3.infinity_finite=false
angle.normalize_negative=4.71238898038468967e0
angle.span_wrapped=3.14159265358979312e0
angle.span_equal=6.28318530717958623e0
angle.parameter_midpoint=5.00000000000000000e-1
angle.contains_wrapped=true
angle.excludes_opposite=false
angle.contains_endpoint=true
angle.equal_is_full_turn=true
frame.origin=1.20000400000000000e6,-4.49999700000000000e6,1.00000000000000000e1
frame.lift=-4.00000000000000000e0,-3.00000000000000000e0,-2.00000000000000000e0
frame.lower=1.20000800000000000e6,-4.49999400000000000e6,1.20000000000000000e1
frame.lift_2d=-4.00000000000000000e0,-3.00000000000000000e0
frame.lower_2d=1.20000800000000000e6,-4.49999400000000000e6
frame.empty_origin=0.00000000000000000e0,0.00000000000000000e0,0.00000000000000000e0
plane.point=1.30000000000000000e1,1.70000000000000000e1,3.00000000000000000e1
plane.project=2.00000000000000000e0,-1.00000000000000000e0
plane.normal=0.00000000000000000e0,0.00000000000000000e0,1.00000000000000000e0
plane.distance=5.00000000000000000e0
plane.sheared_orthonormal=false
plane.contains=true
plane.reversed_distance=-5.00000000000000000e0
plane.collapsed_normal_refused=true
plane.collapsed_project_refused=true
plane.orthonormal_x=1.00000000000000000e0,0.00000000000000000e0,0.00000000000000000e0
plane.orthonormal_y=0.00000000000000000e0,1.00000000000000000e0,0.00000000000000000e0
box.vertices=8
box.edges=12
box.coedges=24
box.loops=6
box.faces=6
box.shells=1
box.lumps=1
box.validation_flaws=0
box.euler=2
box.bounds_min=-1.00000000000000000e0,-1.50000000000000000e0,-2.00000000000000000e0
box.bounds_max=1.00000000000000000e0,1.50000000000000000e0,2.00000000000000000e0
box.volume=2.40000000000000000e1
box.centroid=0.00000000000000000e0,0.00000000000000000e0,0.00000000000000000e0
box.surface_area=5.20000000000000000e1
box.inertia=5.00000000000000000e1,4.00000000000000000e1,2.60000000000000000e1
box.survey_validation_flaws=0
box.survey_volume=2.40000000000000000e1
box.survey_centroid=1.20000100000000000e6,-4.49999850000000000e6,1.20000000000000000e1
box.survey_surface_area=5.20000000000000000e1
box.zero_size_refused=true
box.negative_size_refused=true
reference.checks=80
reference.status=ok
```

