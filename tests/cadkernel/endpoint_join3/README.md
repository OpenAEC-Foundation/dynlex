# Endpoint joining in space

Native port of `src/space/endpoint_join.rs`, including every function after its
test module, at revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
The port and translated tests use SPDX MPL-2.0 attribution.

## API

`lib/cadkernel/endpoint_join3.dl` imports `plane.dl` and its core dependency.
A terminal is a fixed array of two `cad vector3` values, ordered
`[fixed interior point, movable endpoint]`. Every scalar argument is binary64.

```text
the cad extend join kind
the cad add join kind
the cad both join kind
    -> cad endpoint join kind (code: integer)
cad {cad endpoint join kind:left} equals {cad endpoint join kind:right}
    -> boolean

the cad extension joint of first and second within distance
    -> cad endpoint joint (valid: boolean, value: cad vector3)

the cad planar connector distance of points with fuzz fuzz
    -> cad connector distance result (valid: boolean, value: f64)

the cad endpoint join of first and second with fuzz fuzz,
    connector distance connectorDistance and kind kind
    -> cad endpoint join result (valid: boolean, value: cad endpoint joint)

the cad closest endpoint join of first and second with fuzz fuzz,
    connector distance connectorDistance and kind kind
    -> cad endpoint selection result (valid: boolean, value: cad endpoint selection)
cad endpoint selection:
    first: integer; second: integer; joint: cad endpoint joint
```

For closest selection, `first` and `second` are borrowed raw lists of terminal
arrays. For connector distance, `points` is a borrowed raw list of `cad vector2`.
No input is changed or retained. Outputs contain no allocations. The temporary
coplanarity list is freed internally. Callers free only the input lists they own.

The nested result preserves the three Rust states: outer `valid=false` means
no join; outer true with inner false means add a connector without moving either
endpoint; both true provide the shared endpoint. A closest result also identifies
zero-based terminal indices. Do not interpret invalid result payloads.

The three kind constructors have codes 0/1/2. Arbitrarily constructed other
codes are invalid and abort when passed to joining/selection. For valid kinds,
malformed geometry and tolerance inputs return an invalid result.

Extension accepts finite nonnegative distance, including negative zero.
Joining requires positive finite fuzz and nonnegative finite connector distance.
The source angular cutoff is inclusive at squared cross length `1e-24`.
Shortening cannot reach or pass the fixed interior point. Extension returns the
point computed on the first terminal, without averaging the two intersections.
An Add connector needs two nondegenerate segments and a strictly positive gap.
Both examines all extension pairs before considering any connector. Within a
phase, smallest original endpoint gap wins; an exact tie keeps the first pair.
Connector scale is the largest absolute selected 2D coordinate, not a span.

## Verification

```text
python -B tests/cadkernel/endpoint_join3/verify.py --source /path/to/pinned/cadkernel
```

The runner checks the revision and unchanged source modules, compiles the
original Rust sources directly, runs their eight tests, and verifies native
DynLex at O0 and O2. All native processes use the guarded CAD runner and fresh
output directories. Runtime-input probes avoid substituting compile-time
evaluation for generated machine code.

Verified on 2026-09-12 with compiler SHA-256
`9682cf3b88de4b6db6c8b82bc3a7ca6517437b81db9dbd7225bcc5d27eb16234`:

- All eight original Rust tests passed.
- All ten DynLex groups matched at O0 and O2 with no compiler diagnostics.
- 442 differential cases passed, with 2,766 exact Rust field comparisons.
- Binary64 comparisons include signed zero; all O0/O2 results matched exactly.
- Cases cover every nonfinite input coordinate, both displacement boundaries,
  fixed-point reversal, angular degeneracy, underflow/overflow, large-coordinate
  roundoff, skew residuals, connector scaling, phase priority, empty selections,
  ties and constructed spatial intersections.
- Fixture compilation: O0 0.891 s; O2 0.984 s. Probe compilation: O0 0.813 s;
  O2 1.093 s.
- Evidence: `build/endpoint-join-verify-r80_4722/`, including `result.json`,
  original test output, compile logs and replayable `reference-cases.json`.
- Test-first missing-module failure: `build/endpoint-join-red-qrmnaqfq/red.log`.
- The complete fixture is registered under
  `tests/required/cadkernel_endpoint_join3/`. Its registration was verified again
  at O0/O2, with matching output and no diagnostics, in
  `build/cadkernel-verify/run-o5y7l0xa/` (compile 1.156 s / 1.360 s).

Revalidated on 2026-09-13 with compiler SHA-256
`91d137351179e189166b3b39b038e09896ad837e346b1600f3a33d10a392eec9`:
all eight Rust tests, ten fixture groups at each optimization level, and all
442 differential cases passed again (2,766 exact field comparisons and exact
O0/O2 parity). The fixture compiled in 1.391 s / 1.703 s, and the probes in
1.281 s / 1.313 s. Evidence: `build/endpoint-join-verify-7t_aq4do/`.
