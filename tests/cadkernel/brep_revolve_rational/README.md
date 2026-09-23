# Exact rational revolution geometry

`lib/cadkernel/brep_revolve_rational.dl` ports the profile-side `Turn` checks and
ellipse/NURBS `piece_surface` construction from the pinned `src/brep/sweep.rs`.
It accepts a profile of supported curve pieces, determines a frame and scaled
tolerance, encloses ellipse extrema analytically, and refines positive-weight
NURBS control-hull bounds by subdivision. A radial hull that cannot be proven
safe within the source limits is refused. The resulting surface is a strict
rational tensor product with the source profile's knots and the exact angular
conic's knots and weights.

The module returns geometry only. It does not build edges, pcurves, faces,
shells, or solids. A later B-rep caller can use `the cad rational revolution
turn of ...` during profile validation and `the cad rational revolution surface
of ...` when creating an ellipse/NURBS face. The surface call assumes the turn
was computed from the same profile; it refuses a separately supplied piece
that extends to the opposite side of the chosen radial direction.

The required fixture is `tests/required/cadkernel_brep_revolve_rational`.
`verify.py` compares 13 ellipse/NURBS cases at O0 and O2 against the pinned
Rust library through its public sheet-revolution entry point. It compares
validity, degrees, control net, knots, weights, and periodicity. A full closed
ellipse profile is exercised only by the native required fixture because the
public reference B-rep entry point does not produce a body for that case.

Run the focused checks from the DynLex repository root:

```text
python tests/cadkernel/verify.py --filter cadkernel_brep_revolve_rational
python tests/cadkernel/brep_revolve_rational/verify.py --source <pinned-source-directory>
```
