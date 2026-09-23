# Path-sweep base and placement comparison

The native helper module follows the public anchor, path-start and initial
placement functions in pinned `src/brep/sweep_path.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The reference probe calls
the source library; the native probe calls `brep_sweep_path_base.dl`.

Run the required native fixture in both optimization modes:

```text
python -B tests/cadkernel/verify.py --filter cadkernel_brep_sweep_path_base
```

Run the pinned differential probe with a reference build containing `brep`:

```text
python -B tests/cadkernel/brep_sweep_path_base_diff/verify.py --source C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546
```

The comparison covers line chains, circle, arc, ellipse and clockwise-bulge
anchors, scaled planes, spline profiles, multiple wires and profiles, three
path representations, invalid inputs, alignment, rotation, parallel-axis fallback
and an explicit base point. All input lists are borrowed.
