# Spatial union verification

Complete production ports of `src/space/arc_union.rs` and
`src/space/line_union.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` live in
`lib/cadkernel/arc_union3.dl` and `lib/cadkernel/line_union3.dl`.

## Public API

```text
a cad circular arc at centre with normal normal and radius radius from startAngle to endAngle
cad circle at centre with normal normal and radius radius contains shape
the cad circular arc union of first and second within tolerance
the cad line union from firstStart to firstEnd and from secondStart to secondEnd within tolerance
the cad retained indices of linear chain points within tolerance
the cad duplicate arc union kind
the cad overlapping arc union kind
the cad end to end arc union kind
the cad duplicate line union kind
the cad overlapping line union kind
the cad end to end line union kind
cad left equals right
```

Coordinates/normals use `cad vector3`; radii, angles and tolerances use f64.
The last equality pattern has three overloads: `cad arc union kind`,
`cad line union kind`, and `cad line union` (matching upstream derived equality).
Union functions return allocation-free `valid`/`value` records. Read payloads
only when valid. `cad arc union` has `startAngle`, `endAngle`, `fullCircle`,
`kind`; `cad line union` has spatial `start`, `end`, `kind`.
Kinds carry codes 0 duplicate, 1 overlap, 2 end-to-end.
`cad circular arc` has `centre`, `normal`, `radius`, `startAngle`, `endAngle`.

The chain simplifier borrows a standard list of `cad vector3` and returns an
owned standard list of retained integer indices; free that result once. Invalid
inputs/tolerances retain all indices, as upstream. Union tolerances permit zero
and reject nonfinite/negative values; these functions do not take `cad tolerance`.

Arc support must match exactly; tolerance applies only along the shared circle.
Arc reduction preserves Rust `rem_euclid`, including negative zero and tiny
negative remainders that round up to one turn. It deliberately does not reuse
the angle module's numerically different double-remainder normalization.
Line unions preserve the first segment's direction and source floating-point
operations, including overflow behavior for finite extreme coordinates.

## Reproduction and results

```powershell
C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -B tests/cadkernel/union3/verify.py --source C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546
```

`verify.py` accepts `--compiler` and `--rustc`. It reuses guarded subprocess,
pinned-Git-blob and exact numeric comparison helpers from
`tests/cadkernel/polygon3_mesh/verify.py`. The Rust wrapper includes the unchanged
three source modules `space/vec.rs`, `space/arc_union.rs`, `space/line_union.rs`.
`reference.rs` and `probe.dl` call the public operations using runtime arguments.

Verified with compiler SHA-256
`9682cf3b88de4b6db6c8b82bc3a7ca6517437b81db9dbd7225bcc5d27eb16234`
and `rustc 1.91.1 (ed61e7d7e 2025-11-07)`:

- All three source tests passed: two arc-union tests and one line-union test.
- Translated source tests and boundary fixture passed at O0 and O2.
- 1,868 distinct differential cases, each checked at O0 and O2; 12,550 Rust
  scalar comparisons passed exactly, including zero signs and NaN classification.
- Mode counts: circular arc union 1,191; circle containment 66; line union 337;
  chain simplification 229; derived equality 45.
- Empty f32 chain lists were refused at compile time at both optimization levels.

Cases cover support mismatch, degeneracy, negative/multiple turns, tiny angles,
duplicate/overlapping/touching/gapped geometry, tolerance boundaries, nonfinite
inputs, extreme finite inputs, reversals, source-index preservation, corners,
repeated points and deterministic randomized inputs.

The complete success fixture is registered as `tests/required/cadkernel_union3`.
Build evidence is under `build/cadkernel-union3-checks`: `results.json`,
`source-tests.txt`, `reference-driver.rs`, Rust reference/test executables,
`fixture-O0.out`, `fixture-O2.out`, `probe-O0.out`, `probe-O2.out` and type-refusal
diagnostics. Failures record exact arguments and outputs in `failure.json`.
These results cover the two union source modules, not `geom2d/arc_fit.rs`.
