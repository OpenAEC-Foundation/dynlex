# Rational B-rep builders

`lib/cadkernel/nurbs_builder.dl` ports the complete 232-line
`brep/nurbs_builder.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
The source has no unit tests. Eight independent native groups cover line and
conic conversion, normalized existing NURBS knots, unsupported curves,
reversal, lifting, translation, ruled surfaces and deferred validation.

`the cad rational builder of curve2` returns a validity flag and an owned
`cad rational curve2`. Lines, circles, arcs, ellipse arcs and NURBS are
supported. Polyline, ray and infinite-line variants return an invalid result.
`the cad rational unit arc with sweep angle` accepts a finite positive sweep;
quarter-turn quadratic spans preserve the source's controls and weights.
Multi-turn sweeps are retained. A request needing more than 1,073,741,821 spans
aborts before integer conversion because its knot list cannot fit the native
signed 32-bit count. Two oversized runtime inputs verify that guard at O0/O2;
near-capacity successful allocation is not claimed.

Both rational record types hold `degree`, `controls`, `knots` and `weights`.
Their constructors borrow and copy typed input lists without applying geometric
validation. Fields are read-only borrowed views. Ordinary copies retain the
three lists; explicit clone, reverse, lift and translation own independent
lists. Resetting one handle leaves retained copies intact. Empty builders and
invalid results have safe automatic lifecycle. Four compilation-refusal cases
check wrong coordinate dimensions, non-vector controls and non-binary64 knots
or weights, including empty lists.

`the cad curve of builder` calls the corresponding strict NURBS constructor.
`the cad ruled surface from first to second` compares degree, point count and
knots within the source's absolute 1e-10 tolerance, then uses the strict surface
constructor. Compatibility deliberately does not compare coordinates or
weights; it can be true while final surface construction fails. Reversal uses
`1 - reversed knot` without silently renormalizing arbitrary intermediate data.

```text
python tests/cadkernel/nurbs_builder/verify.py --source PATH_TO_PINNED_CADKERNEL
```

Executed: **515 cases / 216,098 comparisons**, all eight native groups and
ownership checks at O0/O2. Both native and Rust optimization parity are exact.
Counts, flags and knot structure are checked without tolerance; non-finite
values and zero signs are preserved. Floating-point comparison uses relative
3e-12 tolerance, with 16 geometry-scale ULPs for coordinates only. Cases cover
all curve variants, tiny/multi-turn sweeps, signed radii, non-unit axes, world
coordinates, arbitrary planes, compatibility boundaries, malformed raw
builders and strict conversion/refusal. The reference uses unchanged pinned
Rust functions and a verified MSVC host toolchain.

Evidence: `build/nurbs-builder-final-checks/summary.json`,
`build/nurbs-builder-final-differential.log`; four required-wrapper checks pass
in `build/nurbs-builder-required.log`. These checks do not establish completion
of the remaining B-rep operations.
