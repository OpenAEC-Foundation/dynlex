# Knot compaction

Source: `src/space/knot_compaction.rs` in
[`cadkernel` revision 953d546b68aef4b6692566a1a9b077fc5bd9fb4f](https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/space/knot_compaction.rs).
Production port: [knot_compaction.dl](../../../lib/cadkernel/knot_compaction.dl).
The port and translated tests carry SPDX MPL-2.0 attribution.

## API and ownership

```text
the cad knot compaction of {cad nurbs curve3:shape} within {64 bit floating-point number:tolerance}
    -> cad nurbs curve result (valid: boolean, value: cad nurbs curve3)
```

The pattern borrows `shape`; it never changes its control, knot or weight lists.
A finite, nonnegative tolerance is required. Negative zero is accepted. Invalid
tolerance returns `valid=false` with a null-safe zero curve, without allocating
NURBS storage. Do not inspect the geometry of an invalid result.

A successful result owns independent managed NURBS storage, including when no
knot can be removed or adjacent weights differ. Ordinary copies share that
result's storage through the existing retain/release hooks. The result needs no
manual destruction. Its list accessors are read-only borrowed views; never
modify or free those lists. `the cad clone of shape` deep-copies a curve, and
`free cad nurbs shape` resets a curve variable while other owners remain valid.

Interior knot occurrences are removed through inverse homogeneous knot
insertion, retaining at least one occurrence per distinct interior knot.
Adjacent weights are compared with exact binary64 equality, rather than the
epsilon-based NURBS rationality predicate. Each recovered weight is reset to
the original first weight to preserve the exact polynomial representation.
The distance comparison rejects only values strictly greater than tolerance.
Degree, domain and periodicity are preserved on accepted removals.

A refused candidate is skipped; it does not turn the whole operation into an
invalid result. Later distinct knots are still processed. Each candidate uses
the existing strict NURBS constructor, including its finite-value and positive
weight checks. The operation does not introduce an extra up-front validation
of curves produced by the permissive constructor. This preserves the source
behavior, including overflow refusals. All helper patterns are file-local.

## Verification

Run from the repository root with the pinned source checkout:

```text
python -B tests/cadkernel/knot_compaction/verify.py --source /path/to/cadkernel
```

The runner checks the source revision and unchanged contents of all included
Rust modules, then compiles those modules directly with `rustc`. It runs both
original knot-compaction tests, the DynLex fixture at O0/O2, and runtime-input
differential probes. Native processes use the existing guarded CAD runner,
which suppresses Windows error dialogs and enforces process timeouts. Every
run uses a fresh build directory to exclude stale binaries.

The [required fixture](../../required/cadkernel_knot_compaction/main.dl)
translates both upstream tests and includes eight assertion groups:
polynomial shape and exact weights; rational independent clones; exact
tolerance boundaries; invalid tolerances and safe empty result copies;
one-ULP unequal weights; periodicity/domain/idempotence and release lifetime;
multiple repeated knots on a nonunit domain with an affine-line oracle; and
strict rejection of non-finite recovered controls.

`verify.py` supplies 229 differential cases, including arbitrary curves built
by independent forward knot insertion, several degrees and weight scales,
candidate refusals, permissive invalid weights, and repeated removal at
multiple interior parameters. Status, counts, flags, knots, weights and domains
are compared exactly, including signed zero. Control and sample coordinates
allow `3e-12` relative error or `3e-12` times the reference geometry scale;
NaNs are compared by classification. O0/O2 numerical output must match exactly
(with NaNs compared by classification).

Verified on 2026-09-12:

- Both original Rust tests passed.
- The fixture passed all eight groups at O0 and O2, without compiler diagnostics.
- All 229 differential cases passed: 46,292 Rust field comparisons across both modes.
- Exact O0/O2 numerical parity passed.
- Fixture compile times: O0 1.609 s, O2 1.968 s; probe times: O0 1.641 s, O2 1.875 s.
- Compiler executable SHA-256: `574f0f563453ab49cf771fe06dad11a2e6098067355bf86d46647485ea1ff1e3`.
- Evidence directory: `build/knot-compaction-verify-geb4twuz/`, including the
  original-test output, compile logs and replayable `reference-cases.json`.
- Required-suite registration was independently verified at O0 and O2:
  both matched the eight expected output lines without diagnostics. Compile
  times were 1.671 s and 1.828 s; artifacts are in
  `build/cadkernel-verify/run-qjj7rig6/`.
- A further required-fixture run passed at both levels with compiler SHA-256
  `9682cf3b88de4b6db6c8b82bc3a7ca6517437b81db9dbd7225bcc5d27eb16234`.
  Compile times were 2.250 s and 3.625 s; the binaries are in
  `build/cadkernel-verify/run-aqh25ysk/`. The 229-case differential result above
  belongs to the explicitly identified earlier executable.

The fixture is registered in `tests/required/cadkernel_knot_compaction/` as
`main.dl` plus `expected.txt`, independently runnable with the guarded verifier:

```text
python -B tests/cadkernel/verify.py --filter cadkernel_knot_compaction
```
