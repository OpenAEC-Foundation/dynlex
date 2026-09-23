# Planar path sweep differential

`verify.py` compares native DynLex line/arc path sweeps with `src/brep/sweep.rs::sweep_along` at pinned cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It verifies the source hash and builds O0/O2 Rust and DynLex probes. Eleven cases cover forward/reverse straight extrusion, quarter-turn arcs, connected collinear and bent lines, and invalid/disconnected/open paths. Valid bodies are compared through arena counts, roots, validation and every vertex coordinate.

The current cases use a closed triangular section, so they exercise the ordinary segment-and-union path. Rectangular line-path mitering has a separate native module and fixture. Deformation controls remain outside this operation.

Run from the DynLex repository root:

```text
python tests/cadkernel/brep_sweep_planar/verify.py --source <pinned-cadkernel-checkout>
```
