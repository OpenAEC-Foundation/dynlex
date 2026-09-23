# Helix source and ownership verification

The implementation in [helix.dl](../../../lib/cadkernel/helix.dl) ports the
entire `src/space/helix.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It uses the actual managed
`cad nurbs curve3` from [nurbs3.dl](../../../lib/cadkernel/nurbs3.dl) for
generated geometry. Its five public operations preserve the source algorithms,
including the different zero-turn validation order in reversal.

## Public contract

`a cad helix with center ..., axis ..., start ..., base radius ..., top radius
..., height ..., turns ... and direction ...` stores the parameters. Center,
axis and start use `cad vector3`; scalar inputs are converted to binary64.
Construction itself does not validate. Use `the cad clockwise helix direction`
or `the cad counterclockwise helix direction` for winding.

| Rust operation | DynLex operation and result |
| --- | --- |
| `HelixCurve::frame_from_points` | `the cad helix frame from base ..., axis ..., start ... and tangent ...`; `cad helix generating frame result`, with `valid` and `value` containing normalized `axis`, `startDirection` and `radius`. |
| `HelixCurve::reversed` | `the cad reversal of ...`; `cad helix result`. Swaps radii, moves the base, reverses the axis and preserves winding and turns. Zero turns return the original parameters before frame validation. |
| `HelixCurve::nurbs` | `the cad helix nurbs of ...`; **`cad nurbs curve result`**. Its `value` is the ordinary managed **`cad nurbs curve3`**. |
| `HelixCurve::length` | `the cad length of ...`; `cad helix scalar result`. Exact mathematical length, including cylindrical, nearly cylindrical and conical formulas. |
| `HelixCurve::turn_slope` | `the cad turn slope of ...`; `cad helix scalar result`. Signed taper angle in radians, using the absolute height. |

NURBS point/tangent evaluation uses the shared `the cad curve point of ... at
...` and `the cad curve tangent of ... at ...` patterns. Parameters are clamped
to `[0, 1]`. Tangents are finite differences per **knot parameter**, not unit
vectors or derivatives rescaled to the normalized parameter. The ordinary
NURBS domain, knot evaluation, clone, reversal and tessellation APIs accept the
result without a helix-specific adapter.

Validation requires finite parameters, nonnegative radii with at least one
positive radius, and nonnegative turns. Zero turns require zero height. Except
for the early zero-turn reversal, operations must also recover a valid axial
frame. `frame_from_points` uses the projected tangent for radius `<= 1e-12`.
Axis and radial normalization retain the source vector thresholds.

Positive-turn NURBS construction uses `ceil(turns * 6)` cubic Hermite spans,
at most 100,000. The controls, triple interior knots, quadruple endpoint knots
and unit weights are passed to the shared **strict** NURBS constructor. Zero
turns produce a degree-one curve with two identical controls and domain
`[0, 1]`. The segment limit applies only to NURBS generation; analytic length
and reversal retain their own finite-result checks.

Internal generation buffers are owned locally. The strict constructor borrows
and copies them; the helix helper frees all three buffers after that call,
including when generated nonfinite controls are refused. The explicitly
fallible control allocation preserves `try_reserve_exact` refusal. General
allocation exhaustion is not fault-injected by this harness.

## Managed lifetime

Ordinary copies of a NURBS result or value retain shared storage automatically.
Scope exit, replacement and container cleanup release their references. Raw
`controls`, `knots` and `weights` fields are read-only borrowed views; they
remain usable while any owning value is retained. Never free those views.
`the cad clone of value` independently copies every list. `free cad nurbs value`
resets one caller value and is repeat-safe; retained copies remain usable.
Reset values must not be evaluated. Resetting a result's `value` does not change
its separate `valid` flag.

Check `valid` before evaluating a result. Refused results own safe, empty
managed NURBS storage and can be copied, returned, cloned and put in lists.
There is no helix-specific public NURBS type or cleanup operation.

## Reproduction

Run from the repository root with an already-built compiler:

```text
python -B tests/cadkernel/helix/verify.py --source /path/to/pinned/cadkernel
```

`--compiler` selects the DynLex binary. On Windows the default Rustup toolchain
is `stable-x86_64-pc-windows-msvc`, matching the native Windows math runtime;
`--rust-toolchain` can select another installed toolchain explicitly. The
driver records `rustc -vV`, the compiler hash and the source hashes. It checks
the Git revision and rejects changes to every included Rust module. It
includes whole source files through `#[path]`, without extraction, rewriting
or an independently reimplemented geometry algorithm.

All compiler, Rust and native child processes use the shared guarded
`run_process` helper, including Windows error-dialog suppression, timeouts and
process-tree cleanup. `--reference-only` runs only Rust; `--cases-only` omits
the required fixtures. These partial modes do not certify the omitted checks.

The complete command:

1. Builds and runs both original Rust helix tests without changing them.
2. Compiles and runs [cadkernel_helix](../../required/cadkernel_helix/main.dl)
   and [cadkernel_helix_ownership](../../required/cadkernel_helix_ownership/main.dl)
   at O0 and O2, checking output and rejecting unexpected diagnostics.
3. Compares 341 deterministic cases against the same Rust implementation at
   both optimization levels. Refusal flags, complete reversal parameters,
   analytic quantities, NURBS degrees/counts/periodicity, control data and
   point/tangent evaluations are emitted. Lists with at most 128 elements are
   compared completely; larger lists emit their first/last four elements and
   midpoint, plus exact counts. Eleven evaluation parameters include both
   endpoints, negative zero, interior points and clamping outside the domain.
4. Checks exact O0/O2 numeric parity. Rust comparisons use componentwise
   `rel_tol=3e-12`, `abs_tol=3e-12`, with matching NaN/infinity classifications
   and zero signs. There is no enlarged geometric tolerance for huge radii.

Cases cover both windings, rising/falling cylinders and cones, zero-radius
tips, zero turns, nearly cylindrical length, vanishing radial rate, skewed
frames, threshold neighbors, nonfinite values in every input component,
overflow/underflow, segment-cap neighbors and seeded random frames/curves.

Four trigonometric runtime checks explain the Windows toolchain choice. For
angle `6283185307179586`, this host's Rust GNU runtime returns cosine
`0.8884142795779659`; the Rust MSVC and DynLex native runtimes both return
`0.8884105663238324`. This is reproducible with probe/reference mode `2` and
one angle argument. Selecting the GNU toolchain makes that comparison fail;
the driver does not relax tolerances to accept it.

## Source test and ownership coverage

| Original test | Native assertions |
| --- | --- |
| `a_zero_base_radius_reverses_between_the_same_endpoints` | Both endpoint equalities at `1e-9`, valid original/reversed NURBS; additional whole-locus samples and literal endpoint checks. |
| `a_zero_turn_zero_height_helix_is_one_exact_point` | Exact zero length and identical endpoints, all original helix fields unchanged by reversal; additional degree, control count and tangent assertions. |

The ownership fixture first consumes a helix result through a function typed
as `cad nurbs curve result`, proving native NURBS interoperability. It checks
shared copies, independent clones, list retention, returned results, repeated
reset and retained geometry after regeneration. Six refusal paths include
early validation, invalid frame, excessive segments and strict-constructor
rejections after cubic or zero-turn buffer construction. Retained result
reference counts return to one after all other aliases are released.

Verified: both original Rust tests passed; 341 cases produced
81,260 reference fields and **162,520 Rust comparisons**, with exact O0/O2
parity. Both required fixtures passed at both levels. The driver writes the
current report, case manifest and failure reproduction data under
`build/cadkernel-helix-checks/`; these generated files are not expectations.
This verifies the helix module and its NURBS integration, not the whole crate.
