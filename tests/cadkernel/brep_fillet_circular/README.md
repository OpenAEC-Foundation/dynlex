# Circular B-rep fillet verification

This fixture ports the complete coaxial-body recognition and reconstruction in
`src/brep/fillet_circular.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It covers planar, cylindrical,
conical and toroidal meridian segments, axis closures, arbitrary orientations,
multiple selections, existing round rejection, interacting radii and malformed
topology.

`verify.py` compiles the unchanged private Rust module beside a small adapter and
compares every emitted topology and analytic-geometry field with DynLex at O0
and O2. Run it with:

```text
python -B tests/cadkernel/brep_fillet_circular/verify.py --source C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546 --output build/brep-fillet-circular-checks
```
