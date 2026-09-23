# cadkernel main-crate port coverage

## Delivery scope

One pull request delivers the complete native DynLex port of the cadkernel main crate at upstream revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, including the optional `offset`, `brep` and `acis` modules, the validation application, and verification. The source inventory is 83 Rust files (82 declared modules plus the crate root) and 672 upstream unit-test functions. Foundation work alone does not fulfill this scope. Rust wrappers and successful Rust reference runs do not count as native DynLex implementations.

The separate `crates/cadkernel-constraints` workspace member is outside this main-crate delivery: 19 files, 4,935 production lines, and 89 tests. It has a distinct `LGPL-2.1-or-later` manifest license and its own LICENSE file. Those files and tests are not included in the 83/672 completion denominator.

Upstream: [cadkernel at the pinned revision](https://github.com/HakanSeven12/cadkernel/tree/953d546b68aef4b6692566a1a9b077fc5bd9fb4f). Compiler base in the specification: `332dac1385edcbe6458386a5119b4cc62d010581`.

## Counting and implementation state

Production LOC counts physical source lines, including comments and blank lines, outside complete `#[cfg(test)]` blocks. Test LOC includes the attribute and the entire test module, including helpers. Production code after a test module is retained. This matters in `geom2d/mod.rs`, `space/curve.rs`, `space/endpoint_join.rs`, `space/lengthen.rs`, and `space/planar.rs`. Source paths are relative to the pinned upstream checkout. Counts are independent of which Cargo features a particular build enables.

| Source group | Files | Production LOC | Test LOC | Physical LOC | Unit tests |
| --- | ---: | ---: | ---: | ---: | ---: |
| Crate root and shared tessellation | 2 | 170 | 0 | 170 | 0 |
| space | 18 | 3,944 | 1,671 | 5,615 | 107 |
| geom2d | 28 | 8,293 | 4,839 | 13,132 | 314 |
| brep | 30 | 24,238 | 4,825 | 29,063 | 251 |
| acis | 5 | 2,976 | 0 | 2,976 | 0 |
| **Main-crate total** | **83** | **39,621** | **11,335** | **50,956** | **672** |

Native implementation snapshot: **72 verified, 2 in progress, 9 unported**. `in-progress` means implementation has started, not that the whole source module passes. `unported` means no native implementation is recorded. Advance a module to `verified` only with the target files, translated test coverage, actual commands/results, and explicit unsupported-case behavior recorded. The Rust reference is evidence about the upstream implementation only. Dynedra Lab compiles at O0/O2; seven curve, surface, planar-face, variable-width-band and B-rep scenarios pass model/state and combined input/graphics checks with drawable hidden-window frames. Its B-rep scenario now exercises eight solids, including native circular-rim fillet and chamfer results; a checked-in SVG/PNG comparison is regenerated from the verified O2 scene export. The B-rep mesh port covers all 17 source groups through fourteen required fixtures and 40 differential cases / 3,360 O0/O2 comparisons, including NURBS singularities and periodic surfaces. The last complete required-suite baseline passes all 125 then-listed fixtures at O0 and O2 (**250 passed, 0 failed**); the subsequently added prism-chamfer, wedge, prism-fillet, full-turn revolution, circular-fillet, circular-chamfer, public blend, press-pull, split, imprint, boolean, slice, shell, constrained-triangulation, mesh and offset fixtures pass targeted runs. The full port remains incomplete.

## Feature and dependency scope

| Feature/dependency | Upstream declaration | Native work required | Reference executable |
| --- | --- | --- | --- |
| geom2d | Default feature; no feature-specific external crate | Every 2D module and associated spatial operations | Enabled explicitly, with default features disabled |
| offset | geom2d + cavalier_contours =0.7.0 | Native conditioned line/arc offsets, sharp and circular joins, major-arc splitting, collapse handling and tapered-sweep callers | Enabled in the pinned all-features differential reference |
| brep | geom2d + spade =2.15.1 | B-rep operations and constrained Delaunay triangulation | Enabled |
| acis | brep + cadcodec, package acadrust | Record/token/pointer model, loss/provenance handling, lift/lower/append and operation history | Not enabled; still in full-port scope |
| cadcodec | Git revision 70ac6da7cf149cea6398a3d8829dd5e48b485b96 | Implement the required codec dependency scope natively; a foreign wrapper does not establish native-port coverage | Not built |
| rustc-hash | Version requirement 2; unconditional dependency | Equivalent map key/equality behavior used by meshing; hash algorithm need not define geometric output | Locked to 2.1.3 |
| constraint solver / nalgebra | Separate workspace member; nalgebra 0.33 | Outside this delivery | Not built |

The reference Cargo.lock fixes the enabled dependency closure: cadkernel, rustc-hash, spade, hashbrown, allocator-api2, equivalent, foldhash, num-traits, autocfg, robust, and smallvec. It does not resolve or validate the excluded offset/codec dependencies. The upstream checkout contains no Cargo.lock. No external dependency implementation is included in the main-crate LOC counts.

## Complete source manifest

`cadkernel` denotes the crate root. All module paths below belong to the main crate. Each row records its own native implementation and verification state.

| Upstream source path | Rust module | Production LOC | Physical LOC | Unit tests | Native state | Target / verification |
| --- | --- | ---: | ---: | ---: | --- | --- |
| `src/acis/append.rs` | `cadkernel::acis::append` | 576 | 576 | 0 | unported | Not recorded |
| `src/acis/history.rs` | `cadkernel::acis::history` | 1545 | 1545 | 0 | unported | Not recorded |
| `src/acis/lift.rs` | `cadkernel::acis::lift` | 635 | 635 | 0 | unported | Not recorded |
| `src/acis/lower.rs` | `cadkernel::acis::lower` | 206 | 206 | 0 | unported | Not recorded |
| `src/acis/mod.rs` | `cadkernel::acis` | 14 | 14 | 0 | unported | Not recorded |
| `src/brep/arena.rs` | `cadkernel::brep::arena` | 256 | 389 | 10 | verified | `arena.dl`; all ten source tests; 26 O0/O2 fixture combinations; 1,888 Rust trace lines match in both modes, including managed nested payloads, typed clone/equality/hash and refusal cases; see `docs/cadkernel-arena.md` |
| `src/brep/blend.rs` | `cadkernel::brep::blend` | 1547 | 1582 | 2 | verified | `brep_blend.dl`; complete fillet/chamfer validation and dispatch, closed convex and open-sheet reconstruction, ordered deduplication, single-edge overloads, repeated cylindrical/spherical blend restoration, exact elliptical seams, spherical corner patches and press-pull forwarding; eleven public fixture groups and 748 cases / 34,182 O0/O2 comparisons pass, with 267 valid results per mode; `tests/cadkernel/brep_blend/README.md` |
| `src/brep/boolean.rs` | `cadkernel::brep::boolean` | 654 | 985 | 21 | verified | `brep_boolean.dl`; complete source-preserving imprint/classification, exact topology copying, orientation, disjoint lump regrouping and inward cavity shells; all 21 source tests are covered, including box/cylinder curves, coincident and partially shared walls, sphere cavities and the exact five-sided wall/door cut; 96 cases / 244,716 complete-arena O0/O2 comparisons pass with exact optimization parity; `tests/cadkernel/brep_boolean/README.md` |
| `src/brep/bounds.rs` | `cadkernel::brep::bounds` | 210 | 394 | 9 | verified | `brep_bounds.dl`; complete source and all nine groups, invalid topology and analytic hulls; 924 cases / 49,486 comparisons pass exact O0/O2 parity; `tests/cadkernel/brep_bounds/README.md` |
| `src/brep/chamfer_circular.rs` | `cadkernel::brep::chamfer_circular` | 317 | 317 | 0 | verified | `brep_chamfer_circular.dl`; complete coaxial plane/cylinder/cone/torus recognition, ordered meridian reconstruction, face-specific trimming, full revolution and validation; four native groups and 172 cases / 123,892 comparisons pass with exact native O0/O2 parity, including malformed topology and arbitrary frames; `tests/cadkernel/brep_chamfer_circular/README.md` |
| `src/brep/chamfer_prismatic.rs` | `cadkernel::brep::chamfer_prismatic` | 308 | 308 | 0 | verified | `brep_chamfer_prismatic.dl`; complete prism recognition, profile mapping, extrusion and validation; five native groups and 905 cases / 194,760 comparisons pass with exact O0/O2 parity, including 389 recognized and 107 valid results; `tests/cadkernel/brep_chamfer_prismatic/README.md` |
| `src/brep/chamfer_profile.rs` | `cadkernel::brep::chamfer_profile` | 140 | 140 | 0 | verified | `brep_chamfer_profile.dl`; complete line/arc trimming, face-specific distances, axis and self-intersection checks; eleven native groups and 164 cases / 12,744 comparisons pass with exact O0/O2 parity; `tests/cadkernel/brep_chamfer_profile/README.md` |
| `src/brep/classify.rs` | `cadkernel::brep::classify` | 188 | 409 | 14 | verified | `brep_classify.dl`; all fourteen source groups, boundary priority, bounded-edge hit testing, four-direction ambiguity handling and explicit unsupported-surface `Unknown`; 248 cases / 992 exact comparisons pass O0/O2 and the required fixture passes 2/2; `tests/cadkernel/brep_classify/README.md` |
| `src/brep/fillet_circular.rs` | `cadkernel::brep::fillet_circular` | 248 | 248 | 0 | verified | `brep_fillet_circular.dl`, `brep_fillet_profile.dl`; complete coaxial plane/cylinder/cone/torus recognition, preservation of existing torus arcs, exact profile rounding, full revolution and validation; four native groups and 139 cases / 105,760 comparisons pass with exact native O0/O2 parity, including malformed topology and arbitrary frames; `tests/cadkernel/brep_fillet_circular/README.md` |
| `src/brep/fillet_prismatic.rs` | `cadkernel::brep::fillet_prismatic` | 172 | 172 | 0 | verified | `brep_fillet_prismatic.dl`, `brep_fillet_profile.dl`; complete prism recognition, exact profile rounding, extrusion and validation; five native groups and 392 cases / 272,502 comparisons pass with exact O0/O2 parity, including 244 recognized and 160 valid results; `tests/cadkernel/brep_fillet_prismatic/README.md` |
| `src/brep/geometry.rs` | `cadkernel::brep::geometry` | 595 | 807 | 12 | verified | brep_geometry.dl; twelve source groups, ownership and 2,561 cases / 103,684 Rust comparisons pass with exact O0/O2 parity; independent review adds 191 ray cases and managed-variant/IR checks |
| `src/brep/imprint.rs` | `cadkernel::brep::imprint` | 308 | 477 | 9 | verified | `brep_imprint.dl`; complete bounds prefilter, supported exact surface meetings, coincident handling, two-body cutting and line/circle/arc/ellipse/NURBS edge alignment with explicit snag values; all nine source behaviors in six native groups and 91 cases / 390,952 complete-arena O0/O2 comparisons pass with exact optimization parity; `tests/cadkernel/brep_imprint/README.md` |
| `src/brep/intersect.rs` | `cadkernel::brep::intersect` | 449 | 819 | 19 | verified | `brep_intersect.dl`; all 19 source groups, five analytic and two lifecycle groups; 2,449 cases / 189,048 comparisons pass O0/O2, with six required checks and explicit `Unknown` behavior; `tests/cadkernel/brep_intersect/README.md` |
| `src/brep/loft_general.rs` | `cadkernel::brep::loft_general` | 1133 | 1133 | 0 | unported | Not recorded |
| `src/brep/loft.rs` | `cadkernel::brep::loft` | 575 | 575 | 0 | verified | `brep_loft.dl`, `brep_profile.dl`; complete polygonal and rational loft source; 23 native groups in four O0/O2 fixtures and 449 cases / 562,638 Rust comparisons pass with exact optimization parity; `tests/cadkernel/brep_loft/README.md` |
| `src/brep/make.rs` | `cadkernel::brep::make` | 1358 | 1921 | 36 | verified | `brep_make.dl`; every public constructor is native, including faceted solids, wedges, circular/elliptical cylinders and frusta, pointed cones, polygonal pyramids and ring/horn/spindle tori; all 36 source groups have direct native coverage through construction, topology, validation and mesh integration; 585 cases / 325,472 complete-arena comparisons pass O0/O2 with exact optimization parity, the separate faceted corpus passes 413 cases / 260,294 comparisons, and the required fixtures pass both modes; `tests/cadkernel/brep_make/README.md` |
| `src/brep/mass.rs` | `cadkernel::brep::mass` | 349 | 405 | 3 | verified | `brep_mass.dl`; all three source groups, analytic solids and shells, sector recognition, translation and unsupported-surface refusal; 247 cases / 17,488 comparisons pass O0/O2 and the required fixture passes 2/2; `tests/cadkernel/brep_mass/README.md` |
| `src/brep/mesh.rs` | `cadkernel::brep::mesh` | 4681 | 5071 | 17 | verified | `brep_mesh.dl`; all 17 source groups plus mass, periodic seams and isolines, holes, booleans, missing faces, chordal tolerance, ownership, wireframes, sweep/loft surfaces, silhouettes and NURBS singularities; fourteen required fixtures pass O0/O2 and 40 differential cases / 3,360 semantic comparisons pass with exact native optimization parity; `tests/cadkernel/brep_mesh/README.md` |
| `src/brep/mod.rs` | `cadkernel::brep` | 154 | 189 | 4 | in-progress | Provenance, source references and all four source tests pass at O0/O2; remaining operation modules/reexports are not yet complete |
| `src/brep/nurbs_builder.rs` | `cadkernel::brep::nurbs_builder` | 232 | 232 | 0 | verified | `nurbs_builder.dl`; complete source, eight native groups, ownership, typed/resource refusals; 515 cases / 216,098 comparisons pass exact O0/O2 parity; `tests/cadkernel/nurbs_builder/README.md` |
| `src/brep/pcurve.rs` | `cadkernel::brep::pcurve` | 683 | 993 | 14 | verified | `brep_pcurve.dl`; all 14 source groups, six ownership groups, default/reset cloning and eight typed refusals; 877 cases / 154,348 comparisons pass exact O0/O2 parity; `tests/cadkernel/brep_pcurve/README.md` |
| `src/brep/place.rs` | `cadkernel::brep::place` | 396 | 584 | 7 | verified | `brep_place.dl`; all seven source groups plus lifecycle and edge cases; 800 cases / 13,426,840 field comparisons pass with exact O0/O2 parity, and all three required fixtures pass in both modes; `tests/cadkernel/brep_place/README.md` |
| `src/brep/presspull.rs` | `cadkernel::brep::presspull` | 958 | 1020 | 2 | verified | `brep_presspull.dl`; complete source tests and public offset, extrude, planar-region construction, union, intersection and subtraction paths; exact nested profiles, trim-aware picking, conic splitting, source independence, contact classification and hole-preserving solid reconstruction; eight required checks, 292 general cases / 16,928 comparisons and 69 planar-Boolean cases / 16,370 comparisons pass across O0/O2 with exact planar optimization parity; overlapping or contained analytic-circle solids retain an explicit 120-second performance limit; `tests/cadkernel/brep_presspull/README.md` |
| `src/brep/shell.rs` | `cadkernel::brep::shell` | 465 | 495 | 2 | verified | `brep_shell.dl`; complete box, circular-cylinder and sphere recognition, positive/negative shelling, public face picking and all five error classes; both source tests plus three native groups and 72 differential cases / 65,434 complete-arena comparisons pass in each of O0 and O2 (130,868 total); `tests/cadkernel/brep_shell/README.md` |
| `src/brep/slice.rs` | `cadkernel::brep::slice` | 350 | 408 | 2 | verified | `brep_slice.dl`; complete closed/open plane slicing and oriented analytic surface slicing, both source tests, nine native groups, public signed-side checks for all five surface kinds, closed cylinder/cone cuts, open sphere cut and explicit intersecting-torus `NoClosedForm`; 146 cases / 253,856 complete-arena O0/O2 comparisons pass with exact optimization parity; `tests/cadkernel/brep_slice/README.md` |
| `src/brep/split.rs` | `cadkernel::brep::split` | 1016 | 1438 | 23 | verified | `brep_split.dl`; complete edge and transactional face splitting, all 23 source tests, closed planar circles/ellipses, multi-landing selection, periodic multi-loop bands and axis-aligned/oblique full-sphere sections; sixteen native groups and 329 differential cases / 385,550 complete-arena O0/O2 comparisons pass with 310 valid cases and exact optimization parity; `tests/cadkernel/brep_split/README.md` |
| `src/brep/sweep_path.rs` | `cadkernel::brep::sweep_path` | 938 | 938 | 0 | unported | Not recorded |
| `src/brep/sweep.rs` | `cadkernel::brep::sweep` | 3802 | 4602 | 35 | in-progress | `brep_sweep.dl`; closed/open extrusion, revolution, tapered polygon/circle solids and closed/open tapered sheets pass native O0/O2 fixtures; circular draft retains an analytic cone; three open-taper cases match 674 pinned-source scalar comparisons with exact native O0/O2 parity; region/path operations, remaining source groups and full-module differential comparison pending |
| `src/brep/thicken.rs` | `cadkernel::brep::thicken` | 1194 | 1471 | 10 | unported | Not recorded |
| `src/brep/topology.rs` | `cadkernel::brep::topology` | 562 | 562 | 0 | verified | `brep_topology.dl`, `brep_debug.dl`; all node/body operations, deep ownership, diagnostics, 210 graph cases / 19,372 Rust trace lines, ten flaw variants and twelve messages pass O0/O2 with exact optimization parity; `tests/cadkernel/brep_topology/README.md` |
| `src/geom2d/angle.rs` | `cadkernel::geom2d::angle` | 76 | 147 | 9 | verified | `angle.dl`; all nine groups plus boundaries, 83 checks at O0/O2 |
| `src/geom2d/arc_fit.rs` | `cadkernel::geom2d::arc_fit` | 112 | 152 | 3 | verified | arc_fit2.dl; all three source groups, ownership and 1,557 valid-chain invariants; combined 2,670 cases / 131,434 Rust comparisons with construct2 pass exact O0/O2 parity; tests/cadkernel/arc_fit2/README.md |
| `src/geom2d/arclength.rs` | `cadkernel::geom2d::arclength` | 315 | 647 | 14 | verified | arclength2.dl and shared Curve2 tangents; 14 source groups, 446 cases / 6,656 Rust comparisons, refusal and ownership checks pass O0/O2; tests/cadkernel/arclength2/README.md |
| `src/geom2d/area.rs` | `cadkernel::geom2d::area` | 306 | 525 | 12 | verified | `area2.dl`; all 12 source groups, both area and centroid conventions, scalar result lifetimes and unchanged borrowed inputs; 218 cases / 3,456 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/area2/README.md` |
| `src/geom2d/arrangement.rs` | `cadkernel::geom2d::arrangement` | 638 | 897 | 18 | verified | `arrangement2.dl` and native `pair_index.dl`; all 18 source groups, tagged/untagged splitting, spatial welding, directed-face traversal and retained nested storage; 597 cases / 139,260 Rust comparisons at O0/O2. One source-dependent overlap zero-sign change is reproduced per mode; `tests/cadkernel/arrangement2/README.md` |
| `src/geom2d/band.rs` | `cadkernel::geom2d::band` | 502 | 502 | 0 | verified | `band2.dl`; complete variable-width rail construction, corner joins, two-pass cleanup and source stations; six independent groups, retained storage and typed refusals; 441 cases / 200,826 comparisons with exact native O0/O2 parity and documented Rust hash-order rounding; `tests/cadkernel/band2/README.md` |
| `src/geom2d/bounds.rs` | `cadkernel::geom2d::bounds` | 130 | 223 | 3 | verified | bounds2.dl; three source groups and 1,172 cases / 7,152 Rust comparisons pass with exact O0/O2 parity; tests/cadkernel/bounds2/README.md |
| `src/geom2d/centerline.rs` | `cadkernel::geom2d::centerline` | 126 | 126 | 0 | verified | `centerline2.dl`; complete source construction and value lifecycle, six independent native groups; 653 cases / 10,432 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/centerline2/README.md` |
| `src/geom2d/clip.rs` | `cadkernel::geom2d::clip` | 218 | 442 | 14 | verified | `clip2.dl`; all 14 translated source tests, borrowed-input/result ownership checks, 590 differential cases / 12,858 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/clip2/README.md` |
| `src/geom2d/constrained.rs` | `cadkernel::geom2d::constrained` | 171 | 171 | 0 | verified | `constrained2.dl`; complete constructor, insertion, split-constraint and filtered-triangle contract with normalized conditioning, even-odd holes and managed shared storage; 68 scenarios / 1,842 semantic O0/O2 comparisons and four required fixture-mode runs pass, including crossing refusals, subnormal/survey domains, hole-cavity cleanup and 64 lifecycle cycles; `tests/cadkernel/constrained2/README.md` |
| `src/geom2d/construct.rs` | `cadkernel::geom2d::construct` | 195 | 195 | 0 | verified | construct2.dl; all source functions, eight combined native fixture-mode checks and fourteen refusal checks; combined 2,670 cases / 131,434 Rust comparisons with arc_fit2 pass exact O0/O2 parity; tests/cadkernel/construct2/README.md |
| `src/geom2d/containment.rs` | `cadkernel::geom2d::containment` | 173 | 372 | 16 | verified | `containment2.dl`; 16 source groups at O0/O2, shared 293-case crossing/containment Rust comparison and ownership fixture; see `tests/cadkernel/cross2/README.md` |
| `src/geom2d/cross.rs` | `cadkernel::geom2d::cross` | 416 | 762 | 23 | verified | `cross2.dl`; 23 source groups at O0/O2, shared 10,608 Rust scalar comparisons and ownership fixture; see `tests/cadkernel/cross2/README.md` |
| `src/geom2d/curve.rs` | `cadkernel::geom2d::curve` | 510 | 726 | 12 | verified | curve2.dl and polyline2.dl; all 24 source groups, ownership, twelve refusals and independent review of 881 cases / 1,150,054 Rust comparisons; helper visibility integration verified at O0/O2; docs/reviews/2026-09-13-cadkernel-curve2-polyline2.md |
| `src/geom2d/deviation.rs` | `cadkernel::geom2d::deviation` | 400 | 684 | 16 | verified | deviation2.dl and shared Curve2 angular sampling; 16 source groups, 431 cases / 7,517,464 Rust comparisons and ownership checks pass O0/O2; tests/cadkernel/deviation2/README.md |
| `src/geom2d/fillet.rs` | `cadkernel::geom2d::fillet` | 280 | 596 | 20 | verified | `fillet2.dl`; both ray and general offset-intersection constructions, all 20 source tests, ordered deduplication and scalar/list lifetime; 565 cases / 17,982 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/fillet2/README.md` |
| `src/geom2d/frame.rs` | `cadkernel::geom2d::frame` | 136 | 199 | 5 | verified | `frame.dl`; five invariants, 2D lift/lower and NaN bounds at O0/O2 |
| `src/geom2d/gradient.rs` | `cadkernel::geom2d::gradient` | 66 | 66 | 0 | verified | gradient.dl; complete source function, independent boundary/type fixtures and 770 differential cases per O0/O2 pass; tests/required/cadkernel_gradient/verify.py |
| `src/geom2d/intersect.rs` | `cadkernel::geom2d::intersect` | 214 | 455 | 19 | verified | `intersect2.dl`; translated groups and 811 differential cases at each of O0/O2 |
| `src/geom2d/mod.rs` | `cadkernel::geom2d` | 170 | 187 | 2 | verified | `geom2d.dl` imports all verified 2D modules; `geometry2.dl` preserves ellipse/tolerance behavior and the required public-facade fixture passes O0/O2 |
| `src/geom2d/nurbs.rs` | `cadkernel::geom2d::nurbs` | 930 | 1551 | 35 | verified | nurbs2.dl; all 35 source groups including restored control/endpoint assertions, ownership, ten refusals and 567 cases / 61,796 Rust comparisons pass O0/O2; independent review closed |
| `src/geom2d/offset.rs` | `cadkernel::geom2d::offset` | 296 | 424 | 10 | verified | `offset2.dl`; all ten source groups, conditioned survey coordinates, open and closed profiles, sharp joins, circular arcs, major-arc splitting and collapse handling; 140 cases / 3,956 complete-list O0/O2 comparisons pass with exact native optimization parity; `tests/cadkernel/offset2/README.md` |
| `src/geom2d/polyline.rs` | `cadkernel::geom2d::polyline` | 248 | 447 | 12 | verified | polyline2.dl and curve2.dl; all 24 source groups and independent 881-case differential review; explicit arc-validity and closed-seam provenance assertions restored and pass O0/O2; docs/reviews/2026-09-13-cadkernel-curve2-polyline2.md |
| `src/geom2d/snap.rs` | `cadkernel::geom2d::snap` | 306 | 559 | 16 | verified | `snap2.dl`; all four operations, six typed categories, all 16 source tests and owned-list/scalar lifetimes; 754 cases / 30,170 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/snap2/README.md` |
| `src/geom2d/tessellate.rs` | `cadkernel::geom2d::tessellate` | 107 | 278 | 11 | verified | `tessellate2.dl` and imported `core.dl` lerp; all 11 source groups, exact elevation and explicit native collection-capacity refusal; 118 cases / 38,018 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/tessellate2/README.md` |
| `src/geom2d/transform.rs` | `cadkernel::geom2d::transform` | 340 | 574 | 18 | verified | `transform2.dl`; all 18 source groups, copy/default/equality, diagnostic text and independent result ownership; 354 cases / 24,230 Rust scalar comparisons at O0/O2, exact optimization parity; `tests/cadkernel/transform2/README.md` |
| `src/geom2d/triangulate.rs` | `cadkernel::geom2d::triangulate` | 735 | 925 | 12 | verified | triangulate2.dl; all twelve source groups, ownership and 1,559 differential cases per O0/O2 pass; tests/cadkernel/triangulate2 |
| `src/geom2d/vec.rs` | `cadkernel::geom2d::vec` | 177 | 300 | 14 | verified | `core.dl`; binary64 record API, translated fixtures and runtime Rust comparisons at O0/O2 |
| `src/lib.rs` | `cadkernel` | 42 | 42 | 0 | unported | Not recorded |
| `src/space/alignment.rs` | `cadkernel::space::alignment` | 80 | 96 | 1 | verified | alignment.dl; source test, 782 cases / 49,806 Rust comparisons, exact O0/O2 parity and four sensitivity mutations pass; tests/cadkernel/alignment |
| `src/space/arc_union.rs` | `cadkernel::space::arc_union` | 75 | 108 | 2 | verified | arc_union3.dl; both source arc-union groups and the combined 1,868-case / 12,550-comparison spatial union differential pass O0/O2 |
| `src/space/arclength.rs` | `cadkernel::space::arclength` | 150 | 168 | 1 | verified | arclength3.dl; source group, ownership, refusals, 144 independent assertions and 207 cases / 6,014 Rust comparisons pass O0/O2; numerical source limitations documented in tests/cadkernel/arclength3/README.md |
| `src/space/curve.rs` | `cadkernel::space::curve` | 144 | 323 | 12 | verified | curve3.dl; twelve source groups, count refusals and 494 cases / 9,406 Rust comparisons pass O0/O2; independent review found no defects; docs/reviews/2026-09-12-cadkernel-curve3.md |
| `src/space/endpoint_join.rs` | `cadkernel::space::endpoint_join` | 118 | 215 | 8 | verified | endpoint_join3.dl; eight source tests, ten native groups and 442 cases / 2,766 exact Rust comparisons pass O0/O2; tests/cadkernel/endpoint_join3/README.md |
| `src/space/helix.rs` | `cadkernel::space::helix` | 216 | 250 | 2 | verified | helix.dl; two source groups, shared managed NURBS3 ownership, six refusals and 341 cases / 162,520 Rust comparisons pass O0/O2; tests/cadkernel/helix |
| `src/space/knot_compaction.rs` | `cadkernel::space::knot_compaction` | 64 | 116 | 2 | verified | knot_compaction.dl; two source tests, eight fixture groups and 229 cases / 46,292 Rust comparisons pass with exact O0/O2 parity; tests/cadkernel/knot_compaction/README.md |
| `src/space/lengthen.rs` | `cadkernel::space::lengthen` | 186 | 326 | 5 | verified | `lengthen3.dl`; all five source groups and all four operations including code after the test block; source metadata, both endpoints and managed result lifetime; 1,382 cases / 15,496 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/lengthen3/README.md` |
| `src/space/line_union.rs` | `cadkernel::space::line_union` | 81 | 95 | 1 | verified | line_union3.dl; source line-union group and the combined 1,868-case / 12,550-comparison spatial union differential pass O0/O2 |
| `src/space/mod.rs` | `cadkernel::space` | 45 | 45 | 0 | verified | `space.dl` imports all spatial implementations and planar operations; combined construction, approximation, measurement, endpoint editing, alignment, helix and mesh checks pass O0/O2 through the single import; `tests/cadkernel/space/README.md` |
| `src/space/nurbs.rs` | `cadkernel::space::nurbs` | 1262 | 1607 | 17 | verified | `nurbs3.dl`; all 17 source groups, ownership fixtures, eight type/visibility rejections and 704 differential cases / 105,220 comparisons pass O0/O2; see `tests/cadkernel/nurbs3/README.md` |
| `src/space/planar.rs` | `cadkernel::space::planar` | 204 | 446 | 16 | verified | planar3.dl; all 16 source tests, helper/ownership groups and 453 cases / 2,578,514 Rust comparisons pass O0/O2; tests/cadkernel/planar3/README.md |
| `src/space/plane.rs` | `cadkernel::space::plane` | 297 | 454 | 13 | verified | plane.dl; 13 source tests, 947 cases / 61,962 Rust comparisons, exact O0/O2 parity and four sensitivity mutations pass; tests/cadkernel/plane |
| `src/space/polygon.rs` | `cadkernel::space::polygon` | 206 | 286 | 7 | verified | polygon3.dl and polygon3_mesh.dl; all seven source groups, 212 measurement cases / 4,072 comparisons and 1,134 triangle-soup cases / 79,380 comparisons pass with exact O0/O2 parity |
| `src/space/polyline_approximation.rs` | `cadkernel::space::polyline_approximation` | 110 | 189 | 2 | verified | `polyline_approximation3.dl`; both source tests, knot-preserving rational subdivision, original resource/refusal policy and managed result lifetimes; 88 cases / 35,116 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/polyline_approximation3/README.md` |
| `src/space/source_join.rs` | `cadkernel::space::source_join` | 109 | 133 | 2 | verified | `source_join3.dl`; all five public operations, both source tests, rational prepend and independent result ownership; 289 cases / 10,554 Rust scalar comparisons at O0/O2 with exact optimization parity; `tests/cadkernel/source_join3/README.md` |
| `src/space/spline.rs` | `cadkernel::space::spline` | 394 | 460 | 5 | verified | `spline.dl`; all five source tests, 804 differential cases / 56,876 comparisons, O0/O2 parity, tangent refusal and N=0 interpolation pass; see `tests/cadkernel/spline/README.md` |
| `src/space/vec.rs` | `cadkernel::space::vec` | 203 | 298 | 11 | verified | `core.dl`; binary64 record API, translated fixtures and runtime Rust comparisons at O0/O2 |
| `src/tessellation.rs` | `cadkernel::tessellation` | 128 | 128 | 0 | verified | tessellation.dl; 372 cases / 827,664 Rust comparisons, eight type refusals and NURBS integration pass O0/O2; included in the passing 570-test compiler suite |

## Dependency-ordered implementation and verification

1. Establish binary64 arithmetic, explicit result variants, collections and typed generational keys; vector/angle/frame/plane helpers; shared spline/tessellation policies; `Tolerance`, `Ellipse`, and `Provenance`.
2. Port the complete spatial and 2D curve layers: NURBS evaluation and editing, adaptive tessellation and intersection refinement, construction, containment, arclength/area, clipping, snapping, arrangements, bands and polygon triangulation.
3. Supply the external offset and constrained-triangulation algorithms required by `geom2d/offset.rs` and `geom2d/constrained.rs`. Their small adapters do not contain the underlying algorithms.
4. Port B-rep geometry, topology validation, pcurves, bounds, placement and rational builders.
5. Implement the connected B-rep operation group: intersections, splitting, imprinting, classification, mesh generation, booleans, constructors, lofts, sweeps, face editing, blends/chamfers, slicing, shelling, thickening and mass properties. Extract shared helpers or declare interfaces together: `boolean.rs` uses `sweep::face_components` and mesh tessellation, while `sweep.rs` calls booleans; `make.rs` calls loft/sweep, and `mass.rs` calls a thicken helper.
6. Complete optional ACIS conversion/history and codec dependencies, then exercise full-feature round trips and the integrated validation application.
7. Run all translated main-crate tests and additional integrated fixtures. The 672 upstream tests include feature-gated cases and are not equivalent to a default-feature test run. Modules without local tests, particularly ACIS, still require behavioral validation. Verify DynLex `-O2` execution and optimization parity as required by the specification.

General NURBS/torus surface intersections and several non-coaxial analytic cases return `Meeting::Unknown` upstream (`brep/intersect.rs`). Shelling accepts recognized boxes, circular cylinders and spheres (`brep/shell.rs`). These explicit upstream limitations must remain distinguishable from missing port implementations. Generalizing them requires algorithms beyond translation; no unsupported case may be silently reported as disjoint or successful.

## Reference evidence and attribution

Target names in the manifest are relative to `lib/cadkernel/`.
Vector interfaces use typed coordinate records in place of Rust array/tuple
conversion traits; invalid normalization results expose `valid=false`.
Heap-allocated list results belong to their caller.

Native commands executed on 2026-09-12:

```sh
python -B tests/cadkernel/verify.py --filter cadkernel_core --filter cadkernel_angle --filter cadkernel_frame --filter cadkernel_arena --filter cadkernel_arena_wrong_key
python -B tests/cadkernel/core/verify.py --source /path/to/pinned/cadkernel
python -B tests/required/cadkernel_intersect2/verify.py --upstream /path/to/pinned/cadkernel
```

The first command passes all ten selected fixture/optimization combinations.
The core differential passes 135 inputs Ã— 58 fields Ã— two optimization levels
(15,660 Rust comparisons), with exact O0/O2 numerical parity including zero
sign and NaN classification. The intersection differential passes 811 cases
per optimization level, runs 21 unchanged Rust tests, and rejects six invalid
tolerances at both levels. These commands do not establish full-port completion.

The unchanged upstream main crate was separately built from a copy under
`build/cadkernel-upstream-source` and tested with all optional features:

```sh
cargo test --locked --manifest-path build/cadkernel-upstream-source/Cargo.toml --target-dir build/cadkernel-upstream-tests -p cadkernel --all-features --lib
```

Result: **672 passed, 0 failed, 0 ignored**, exit 0, 74.49 seconds test time
on Windows with rustc 1.94.0. The copy retained its workspace manifests and
license; source code was unchanged. Its generated Cargo.lock and build products
are local artifacts. This is an upstream baseline, not 672 translated DynLex
tests. The separate constraint crate was not tested by this command.

The repaired sampling compiler failure and its eleven-line reproducer are
documented in [the regression record](../tests/cadkernel/compiler-regressions/README.md).

The independent fixture inputs, execution interface and recorded Rust results are documented in [the reference README](../tests/cadkernel/reference/README.md). They check selected vector, angle, frame, plane and box behaviors, not all 672 tests and not native-port completion.

The main-crate source is published by the upstream cadkernel contributors under MPL-2.0; see its [manifest](https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/Cargo.toml) and [LICENSE](https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/LICENSE). Preserve original source notices and source attribution in translated files. This inventory contains metadata; the numerical executable calls the unchanged upstream crate and does not vendor or copy kernel implementations.

The separate solver's manifest declares LGPL-2.1-or-later and its own source notices must be preserved independently if it is ported in another delivery. Do not label that crate as MPL-2.0 or include its 89 tests in main-crate completion claims.
