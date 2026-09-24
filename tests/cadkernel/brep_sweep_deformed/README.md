# Deformed planar sweep

The native operation follows pinned `src/brep/sweep.rs::sweep_along_deformed` and `deformation_stations` at revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. Identity parameters delegate to the planar sweep. Non-identity line profiles are projected into the first station, rotated, twisted, scaled and mitered at the same source-defined stations, then passed to the ordered polygon loft. Arc paths use the source's angular station spacing; their section path is sampled rather than replaced by a separate approximation scheme.

The required fixture covers the main valid operations and refusals at O0 and O2. `verify.py` pins the Rust source revision and file hash, then compares validity, all nine arena counts, roots, flaws, two-coedge edges, pcurve counts, face direction counts, and normalized vertex coordinates. Coordinates are sorted because arena insertion order may differ.

`expression_reproducer.dl` isolates the former compiler grouping discrepancy: separately evaluating `ceiling` and `maximum` printed `3`, while the equivalent nested expression printed `1` without diagnostics. The repaired compiler now prints `3` for both at O0/O2. The sweep uses the source's nested expressions directly for arc and twist subdivision counts; the required expression fixture preserves that behavior.

Run from the repository root:

```text
python -B tests/cadkernel/verify.py --filter cadkernel_brep_sweep_deformed
python -B tests/cadkernel/brep_sweep_deformed/verify.py --source <pinned-cadkernel-checkout>
```
