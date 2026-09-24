# General loft patch surfaces

This sidecar converts one homogeneous base-patch column into a strict
`cad nurbs surface3`. It ports pinned `src/brep/loft_general.rs::Patch::surface()`
at revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`: project each
homogeneous control, keep its weight, form clamped Bézier knots on both axes,
and use the strict surface constructor. An optional midpoint check mirrors
the `assemble()` refusal for a locally degenerate patch. Callers iterate the
base result's columns and receive one independently owned surface per call.

The preceding base-patch module supports profile-only open/cyclic lofts,
periodicity and normal modes 0–6. This module adds only the midpoint-check
switch. Error codes distinguish invalid upstream data/column (1), strict
rational refusal (2), and a missing midpoint normal (3). The failed result
contains a safely owned empty surface. The required fixture checks all three
refusal paths and 500 result-retention iterations after base input release.

The differential harness checks the pinned source revision and hashes for
loft preparation and strict NURBS construction, builds Rust and DynLex at
O0/O2, and compares each patch's status, interval, degree, periodicity,
knots, control points, weights, midpoint normal and evaluated point.

Run from the repository root:

```text
python -B tests/cadkernel/brep_loft_general_surface/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_loft_general_surface
```

This slice does not assemble faces or a public B-rep body. Guide/path
corrections, shared-edge checks, rim and rail topology, pcurves, face
orientation, caps, sewing and final body validation remain to be ported.
