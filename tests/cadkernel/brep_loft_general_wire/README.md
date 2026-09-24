# General loft: section-wire preparation

This sidecar ports the `section_wire` ordering and the per-profile guard in
`prepared` from pinned `src/brep/loft_general.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It builds rational 3D curves
from planar line, circle, arc, ellipse and spline pieces, joins consecutive
pieces in either direction within the source tolerance, checks closure when
required, and groups their exact homogeneous Bézier spans into one normalized
wire parameter. An outer wire and closed hole wires are retained separately.

Inputs are borrowed. The result owns its wire and control lists after the input
lists have been changed or released. A failed construction returns no wires.
The required fixture checks reversal, hole and disconnection refusals, nested
ownership, and 2,000 retained constructions at O0 and O2.

The differential driver pins the source revision and relevant source hashes,
compiles its Rust reference at O0 and O2, and compares validity, refusal,
closure, span intervals, and homogeneous controls. It passes 56 cases and
5,060 compared fields across both modes.

Run from the repository root:

```text
python -B tests/cadkernel/brep_loft_general_wire/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_loft_general_wire
```

This covers one prepared section at a time. Alignment between profiles,
hole containment and orientation, loft patches, caps, and B-rep body assembly
remain pending.
