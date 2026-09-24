# Transported path patch primitives

This slice exposes frame algebra, evaluation and length of line, planar-curve and NURBS-span pieces, adaptive parameter division, and the transported frame walk with optional curved banking. The Rust reference reproduces these private formulas from the pinned `src/brep/sweep_path.rs`; `verify.py` checks the source revision and hash before comparing every emitted parameter and frame at O0 and O2.

The module returns walked frames and parameters only. Division and walk results own raw lists, including nested section lists; callers release each result exactly once, even on refusal. A repeated ownership fixture and a Windows private-memory stress run cover both paths. Path expansion into Pieces and the initial-placement handoff are not wired to it yet. A separate fit module covers cubic frame fitting and sampled local regularity. Closure roll, polyline bank-roll reconciliation, miter corrections, twist/scale accumulation and B-rep body construction remain before a public `sweep_path` body can use the pieces.

```powershell
python tests/cadkernel/brep_sweep_path_patches/verify.py --source PATH_TO_PINNED_RUST_CHECKOUT
```
