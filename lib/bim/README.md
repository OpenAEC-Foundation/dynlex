# BIM geometry

Reusable, headless DynLex geometry extracted from PR #8. These files have no
window, viewer, application state, or graphics-runtime dependency.

Import the operation you need; its ordinary DynLex imports provide its geometry
and ownership dependencies:

| Entry point | Operations |
| --- | --- |
| `lib/bim/brep_sweep.dl` | Profile extrusion, tapered extrusion, open sheets, revolutions |
| `lib/bim/brep_region_solid.dl` | Solid region extrusion with holes and optional draft |
| `lib/bim/brep_make.dl` | Solid constructors |
| `lib/bim/brep_place.dl` | Body placement and transformations |
| `lib/bim/brep_bounds.dl` | Bounds |
| `lib/bim/brep_mass.dl` | Mass properties |
| `lib/bim/brep_mesh.dl` | Tessellation of supported geometry |
| `lib/bim/geom2d.dl` | 2D curves, profiles, offsets and triangulation |

The existing `cad` pattern names and binary64 numerical behavior are retained.
For example, `the cad extrusion region of profiles on plane along direction`
returns a result with `valid` and `value`; inspect `valid` before using `value`.
Invalid or unsupported geometry is not a successful solid.

The retained required fixtures exercise geometry, topology, ownership, numerical
edge cases, and rejected input. Source-comparison material remains under
`tests/cadkernel/`, preserving its upstream provenance. This is a selection of
operations and their dependency closure, not a claim of a complete CAD-kernel
port. ACIS/SAT codecs, the unfinished general-loft/path/thickening port, Dynedra
Lab, and its rendering demonstrations are outside this extraction.

## Attribution

Ported from HakanSeven12/cadkernel, revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. Per-file source references and
SPDX identifiers are retained. The port is licensed under MPL-2.0; see `LICENSE`.
