# Prismatic chamfer verification

This directory verifies the complete private `brep/chamfer_prismatic.rs` source
module from revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The native implementation is
`lib/cadkernel/brep_chamfer_prismatic.dl`; it recognizes constant analytic
prisms, maps longitudinal edge selections onto the cap profile, applies the
profile chamfer and rebuilds a validated extrusion.

The required fixture contains five independent groups. It checks one and two
box edges, the recognized/unrecognized distinction, base-face errors,
distance interaction, exact topology counts, validation, vertex gap and
borrowed input preservation. It passes at both O0 and O2.

The differential harness compiles unchanged copies of
`chamfer_profile.rs` and `chamfer_prismatic.rs` beside a small adapter. Its 905
cases cover all box axes and face choices, repeated and multiple selections,
boundary and nonfinite distances, scales and large coordinates, wedges,
regular polygonal prisms, rounded extrusions in both directions, round solids,
empty input and ten malformed-topology variants. The final run records:

- 389 recognized cases;
- 107 successful body results;
- 194,760 compared fields;
- exact native O0/O2 parity;
- exact Rust O0/O2 parity;
- compiler SHA-256
  `17cf5600c05b525005cc7878e7ad1abe268d8d78b531694b6abe40b2145e66d4`.

Every successful result comparison includes all topology arenas and keys,
adjacency, pcurves, analytic curves and surfaces, provenance, Euler
characteristic, validation flaw count and worst vertex gap. Numeric geometry
uses the same tight binary64 comparison policy as the neighboring B-rep
harnesses. Recognition, validity and error kind are exact.

The source stores selected profile nodes in a randomized map. When several
selected edges are outside the requested base face, which one is attached to
the error is intentionally iteration-order dependent. The harness therefore
canonicalizes only that field to “the reported edge belongs to the input
selection.” Single-edge identity is also asserted by the required fixture. No
successful body field is canonicalized.

Run the evidence from the repository root:

```sh
python -B tests/cadkernel/verify.py \
  --compiler build/dynlex.exe \
  --filter cadkernel_brep_chamfer_prismatic \
  --compile-timeout 120

python -B tests/cadkernel/brep_chamfer_prismatic/verify.py \
  --source /path/to/cadkernel-at-953d546 \
  --compiler build/dynlex.exe \
  --libraries build/brep-make-six-checks \
  --output build/brep-chamfer-prismatic-checks
```

The machine-readable result is
`build/brep-chamfer-prismatic-checks/summary.json`.
