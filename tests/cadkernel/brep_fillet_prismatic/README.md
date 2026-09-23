# Prismatic fillet verification

This directory verifies the complete private `brep/fillet_prismatic.rs` source
module from revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The native implementation is
`lib/cadkernel/brep_fillet_prismatic.dl`. Its shared profile helper in
`lib/cadkernel/brep_fillet_profile.dl` translates `round_profile` from
`fillet_circular.rs`; the remainder of the circular-body recognizer is tracked
separately.

The required fixture has five independent groups. It checks one and two
longitudinal box edges, the recognized/unrecognized distinction, radius
exhaustion, exact topology counts, validation, vertex gap and borrowed input
preservation. It passes at O0 and O2.

The differential adapter compiles unchanged copies of `fillet_circular.rs`
and `fillet_prismatic.rs`. This keeps the source `round_profile` implementation
inside the oracle rather than reproducing it in test code. Its 392 cases cover
all box edge directions, repeated and multiple selections, radii around
geometric limits, scales and large coordinates, wedges, regular polygonal
prisms, rounded extrusions, non-prismatic round solids, empty input and ten
malformed-topology variants. The verified run records:

- 244 recognized cases;
- 160 successful body results;
- 272,502 compared fields;
- exact native O0/O2 parity;
- exact Rust O0/O2 parity;
- compiler SHA-256
  `17cf5600c05b525005cc7878e7ad1abe268d8d78b531694b6abe40b2145e66d4`.

Every successful result comparison includes all topology arenas and keys,
adjacency, pcurves, analytic curves and surfaces, provenance, Euler
characteristic, validation flaw count and worst vertex gap. This means the
generated arcs and cylinders are checked directly, together with their cap
and side parameter-space curves.

Run the evidence from the repository root:

```sh
python -B tests/cadkernel/verify.py \
  --compiler build/dynlex.exe \
  --filter cadkernel_brep_fillet_prismatic \
  --compile-timeout 120

python -B tests/cadkernel/brep_fillet_prismatic/verify.py \
  --source /path/to/cadkernel-at-953d546 \
  --compiler build/dynlex.exe \
  --libraries build/brep-make-six-checks \
  --output build/brep-fillet-prismatic-checks
```

The machine-readable result is
`build/brep-fillet-prismatic-checks/summary.json`.
