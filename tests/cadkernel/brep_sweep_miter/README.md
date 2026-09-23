# Rectangular line miter sweep

`main.dl` exercises the native helper with right-angle, oblique, collinear and reversed paths, exact miter vertices, manifold edges, and rejected inputs. The required fixture compiles and runs it at O0 and O2.

`verify.py` pins `src/brep/sweep.rs` to revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and compares six valid paths through the public Rust `sweep_along` entry point and the native planar sweep entry point. For these inputs both functions take the rectangular line miter branch. It checks all nine arena counts, roots, flaws, two-coedge edges, pcurve counts, forward face counts, and normalized vertex coordinates at both optimization levels. Vertex coordinates are sorted because arena insertion order need not be stable across implementations.

Run from the repository root:

```text
python -B tests/cadkernel/verify.py --filter cadkernel_brep_sweep_miter
python -B tests/cadkernel/brep_sweep_miter/verify.py --source <pinned-cadkernel-checkout>
```

The Rust helper is private. Its rejected inputs are checked against its source guards and the native fixture; the public Rust function may fall back to a different sweep algorithm on those inputs.
