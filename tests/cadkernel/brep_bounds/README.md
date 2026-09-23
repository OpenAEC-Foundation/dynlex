# Spatial bounds

`lib/cadkernel/brep_bounds.dl` translates all of `src/brep/bounds.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The nine original test groups
are in `main.dl` and `surfaces.dl`; the combined required wrapper imports both.

The API includes point-set boxes, union, absorption, growth, centre, size,
overlap, containment, separation, face/body bounds and operation tolerance.
Boxes are scalar values. Input lists and body arenas are borrowed. Returned
optional bounds expose `valid`; missing topology or an unsafe analytic hull
produces `valid=false`. Empty bodies have no bounds.

Sphere and torus bounds use full oriented surface extents. Positive rational
surface weights permit a control hull; invalid weights/controls reject it.
Other surfaces use the original 17 samples per boundary edge. This is the
source approximation, not an exact bound for arbitrary curves. Point-set and
sampled bounds retain the source min/max behavior for NaN and infinity.

Verification on Windows, with the unchanged MSVC Rust reference and native
compiler at O0 and O2:

```sh
python tests/cadkernel/brep_bounds/verify.py --source /path/to/pinned/cadkernel
```

All nine unchanged Rust groups and all nine translated groups passed in both
modes. The differential passed **924 inputs / 49,486 comparisons** with exact
optimization parity within each language. Counts and flags are exact;
floating fields use the shared relative tolerance while retaining zero-sign
and non-finite checks. Cases cover broken arena links, empty boundaries,
surface control hulls, orientation, altered edge parameter ranges, survey
coordinates, extreme/subnormal values and operation tolerances.

The recorded run used `--libraries build/brep-make-six-checks --output
build/brep-bounds-checks`, reusing the full pinned reference libraries.
`build/brep-bounds-checks/summary.json` records compiler/source/library hashes.
Native compilation takes about 24–29 seconds on the verification host; the
required fixture has an explicit 60-second compile budget.
