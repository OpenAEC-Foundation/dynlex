# Native B-rep loft verification

`lib/cadkernel/brep_loft.dl` ports the complete `src/brep/loft.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`: public loft, ordered polygon
loft, polygon and rational alignment, caps, analytic and ruled side surfaces,
pcurves, topology linking, and final validation. The 575-line source module
contains no unit tests. All implementation arithmetic and topology allocation
run in DynLex; Rust is used only by the differential test reference.

## API and ownership

```text
a cad loft section on {cad plane:plane} with profile {list<cad curve2>:profile}
the cad loft through {list<cad loft section>:sections}
the ordered cad polygon loft through {list<cad loft section>:sections}
the cad clone of {cad loft section:section}
```

The two loft functions return `cad body result` (`valid`, `value`). Their inputs
are borrowed. A section owns a managed `cad topology list` in `profile`, with the
borrowed list accessible as `profile's values`. Construction copies the input
list and retains the immutable curve payloads. Ordinary section copies retain
the profile list; cloning duplicates the list and each managed curve payload.
Resetting a variable to a new value of its type releases its ownership. Returned
bodies survive all section and temporary lifetimes; ordinary body copies share
the body arenas, and `the cad clone of body` copies them independently.

`lib/cadkernel/brep_profile.dl` contains precisely the shared source
`path_senses`, `profile_senses` and `meets` dependencies. Its APIs are
`the cad profile senses of profile`, `the cad path senses of path`, and
`a cad profile meets b`. Both sense functions return `cad profile senses result`
with `valid` and a managed Boolean topology list in `value`.
`value's values` is borrowed. Joining uses the source's absolute `1e-9` threshold
and `hypot`. The path function accepts open chains; the profile function requires
closure. Both retry the first segment's opposite direction.

## Preserved behavior

- Polygonal loft aligns each ring to the preceding ring by minimum squared
  corner distance, trying forward shifts before reversed shifts. Equal costs
  retain the first candidate. The ordered entry does not align and rejects
  non-line input before attempting curve evaluation.
- Polygonal sections need equal counts of at least three pieces and distinct
  adjacent centres. Curved lofts additionally require nonzero finite first area,
  compatible rational bases, and monotone centre progression along the overall
  direction. These are the original limits; no extra supported shape family is
  implied by the API.
- Analytic polygon sides remain planes within the scale-dependent tolerance.
  Twisted sides use bilinear NURBS. Curved sides preserve the rational builders,
  control points, knots, weights, surface sense and unit-square edge pcurves.
  Caps remain planes without pcurves, as in the source.
- Vertex, rim, rail, surface, face and coedge insertion order matches the source.
  All generated provenance is synthesized and each result passes the original
  topology validation before it is returned.
- A permissively constructed NURBS with inverted active domain aborts in the
  existing evaluator, as the Rust evaluator panics. This case is checked
  separately. The polygon-only entry rejects that curve without evaluating it.

## Independent fixtures

The four fixtures contain 23 groups, run at both `-O0` and `-O2`:

- `brep_loft`: topology counts and continuity, ordered correspondence, cyclic
  and reverse alignment, first-segment retry, shared middle rims, minimum and
  coincident sections, open versus closed chains, Euclidean join threshold.
- `brep_loft_geometry`: analytic versus bilinear sides, five boundary samples
  per pcurve, rational ellipse samples on a 5-by-5 grid, outward normals,
  progression rejection, mixed line/arc profiles, minimum piece count.
- `brep_loft_ownership`: copied input lists, retained section copies, deep
  curve clones, local returns, body clones and repeated nested destruction.
- `brep_loft_ordered_invalid`: polygon-only rejection precedes evaluation of a
  malformed curved domain; its red run aborted before the port was corrected.

The corresponding `tests/required/cadkernel_brep_loft*` wrappers carry the same
expected output. Their 120-second compile budget accommodates the full imported
geometry and topology implementations. `wrong_profile.dl` and `wrong_sections.dl`
also verify compile-time type refusal in both modes.

## Differential runner

From the repository root:

```powershell
python tests/cadkernel/brep_loft/verify.py --source PATH_TO_PINNED_SOURCE --output build/brep-loft-checks
```

The runner defaults to the verified MSVC libraries in `build/brep-make-six-checks`
and dependency artifacts in `build/topology-reference-deps/target/debug/deps`;
override these with `--libraries` and `--dependencies`. `--rustc` and `--compiler`
select the toolchains. The source checkout must be clean at the pinned revision.

The reference includes the original loft and rational-builder source files
unchanged. The profile/path dependency is an exact source excerpt with test-only
visibility shims. Public types and geometry come from the complete pinned Rust
crate. Both optimization levels are independently compiled and exercised.

The corpus includes empty, mismatched and degenerate input; cyclic shifts and
reversed orientations; mixed and rational curves; planar and twisted sections;
monotone and reversing progression; near-threshold joints; translated, sheared,
scaled and degenerate frames; NaN, infinity, signed zero and tiny/huge magnitudes;
and deterministic randomized multi-section solids.

Every accepted body's complete nine-arena graph is compared: keys, provenance,
owners, adjacency, edge parameters, senses, pcurve endpoints, analytic frames,
all NURBS degrees, control points, knots and weights, roots, Euler characteristic
and validation result. Integer and Boolean fields are exact; binary64 values use
the shared `3e-12` relative comparison with no absolute tolerance, preserving
signed zero, infinity and NaN classification. Native and Rust optimization parity
are each checked exactly. Four checked process terminations cover the malformed
domain separately.

`summary.json` records compiler and source hashes, toolchain, case and comparison
counts, optimization parity and the explicit source-panic case. Failed inputs
and both reference/native outputs remain beside it for reproduction. Every native
process uses the common guarded runner, including expected aborts.
