# Planar circular construction

Native port of all seven public functions and the stored-arc helper in
`src/geom2d/construct.rs`, pinned at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The complete source has no tests.
The shared verification driver also includes the complete planar arc-fit module
and its three original tests, including everything after `#[cfg(test)]`.

Import `lib/cadkernel/construct2.dl`. Points are `cad vector2`; all scalar
arguments are binary64, with angles in radians. Construction returns
`cad arc2 result` with `valid` and `value: cad arc2`. A failed result has a
zero payload; inspect `valid` before evaluating it. Results are plain values.

| Original function | Native pattern |
| --- | --- |
| bounded_arc | `the bounded cad arc2 at centre with radius radius from first to last` |
| arc_from_endpoints_angle | `the cad arc2 from start to end with included angle included` |
| arc_from_endpoints_radius | `the cad arc2 from start to end with signed radius signedRadius` |
| arc_from_sagitta | `the cad arc2 from start to end with sagitta sagitta` |
| arc_from_start_tangent | `the cad arc2 from start along tangent direction to end flipped flip` |
| arc_through_points | `the cad arc2 through start then middle to end` |
| arc_sweep_from_chord | `the cad arc2 sweep for radius radius and signed chord signedChord` |

The last pattern returns `cad arc2 sweep result` with `valid` and a binary64
`value`. The returned arc is the existing Curve2 payload. Convert it with
`the cad curve2 of result's value`, then use ordinary Curve2 point/tangent
evaluation.

The original conventions are intentional:

- Arcs are stored counterclockwise. Negative included angles and some tangent
  choices exchange stored endpoints. A negative radius selects the major arc;
  negative sagitta chooses the opposite side of the chord.
- Bounded-arc rejection uses a single Euclidean remainder of the angle
  difference. This is distinct from the existing arc evaluator's angle
  normalization, both in rounding and signed-zero behavior.
- Validation is function-specific. Nonfinite coordinates are not universally
  rejected by the source, and overflow can produce a present arc with NaN
  fields. The native port preserves these results. Arc fitting applies its
  own stricter finite-input checks.
- Radius, chord and angular thresholds preserve the source inequalities and
  evaluation order. No coordinate sanitization or tolerance substitution is
  added.

## Reproduce verification

From the repository root, using a checkout at the pinned revision:

```text
python tests/cadkernel/construct2/verify.py --source /path/to/pinned/cadkernel
```

The driver verifies the source revision and unchanged files, includes the
original Rust modules by path, runs their three arc-fit tests, compiles native
probes at O0/O2, checks the required fixtures and rejected type probes, and
compares all returned fields plus constructed-arc point/tangent evaluation.
The sole extracted dependency is the unchanged Ellipse struct/implementation
from `geom2d/mod.rs`; neither module under test is extracted or rewritten.

Cases cover sign/orientation changes, nextafter boundaries, repeated points,
collinearity, major/minor selection, tiny/huge coordinates, NaNs/infinities,
overflow, signed zero, tangent overrides, closure, both knee formulas and
refusal after an already-computed span. Seeded cases are deterministic.
All output chains also undergo independent checks of original-point retention,
source indices, finite fields and inserted fractions.

Comparison requires equal result presence, record lengths, special-value
classification and zero signs. Finite Rust/native values use absolute and
relative tolerances of `3e-12`; O0/O2 comparison is exact apart from NaN payloads.
On Windows, Rust uses the installed MSVC toolchain to share the native UCRT
math runtime. All child processes use the guarded runner that suppresses
Windows crash dialogs.

Generated inputs, per-case names, compiler logs, reference/native outputs,
source/compiler hashes and `report.json` are under
`build/cadkernel-construct2-checks/`. They are reproducible build artifacts.
Use `--reference-only` for source checks and `--cases-only` to omit required
fixtures and type probes during focused differential work.

Required fixtures:

- `tests/required/cadkernel_construct2`: endpoint geometry, minor/major
  sweeps, tangent flip and degeneracy refusals.
- `tests/required/cadkernel_arc_fit2`: all three original source groups.
- `tests/required/cadkernel_arc_fit2_invariants`: circle geometry, G1
  joins, reversal, closure, length fractions, inflection and collinear fallback.
- `tests/required/cadkernel_arc_fit2_ownership`: source type, input independence,
  copies, deep clone, returned refusals, list retention, repeated reset and
  regeneration.

See [the arc-fit API](../arc_fit2/README.md) for input and lifetime contracts.

