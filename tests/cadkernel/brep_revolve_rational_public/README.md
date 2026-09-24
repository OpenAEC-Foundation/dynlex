# Public rational revolution differential

The required fixture exercises partial and full sheet/solid revolutions of ellipse and NURBS profiles, including topology validity and pcurves. `verify.py` compares public B-rep arena counts, valid/refused cases, NURBS surface counts, coedge orientation and pcurves, vertex positions, and face orientation with the pinned Rust implementation at O0 and O2. Exact NURBS control nets are covered by the separate rational surface differential. The pinned source refuses partial rational solids with a profile endpoint on the axis because its trim circuit has fewer edges than pcurves.

Run from the repository root:

```powershell
python tests/cadkernel/brep_revolve_rational_public/verify.py --source PATH_TO_PINNED_RUST_CHECKOUT
```
