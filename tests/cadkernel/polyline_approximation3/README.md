# Precision-bounded NURBS polylines

`lib/cadkernel/polyline_approximation3.dl` ports the complete
`src/space/polyline_approximation.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`: 110 production lines and two tests.

`the cad spline polyline of curve at precision precision` returns `valid`,
`points` and `tolerance`. Precision is 0–99. The tolerance is the control-box
diagonal divided by `32 * (precision + 1)^2`. Exact knot insertion extracts
positive rational Bezier pieces; recursive subdivision accepts a chord only
when every projected control point is within that tolerance.

The port retains knot boundaries, endpoint order, homogeneous weight scaling,
de Casteljau operation order and all source refusals. Unsupported unclamped or
discontinuous inputs fail rather than receiving a coarser approximation.
Source limits remain: degree 64, one million control/output points, two million
knots, recursion depth 32 and sixteen million cumulative work units. The native
integer precision parameter also rejects negative values before any work.

Input lists are borrowed. Results independently own point buffers. Their
`points` fields are immutable borrowed views, valid while a result copy is
alive; callers do not free those views. Scope exit and resetting a result release
its ownership. Internal knot/control/subdivision lists use the same managed
storage convention so early refusals release partial work.

## Verification

Both original source tests pass unchanged in Rust and as translated DynLex
fixtures at O0/O2. They check monotonic precision, exact endpoints, 513 sampled
curve-to-polyline bounds, retained interior knots, discontinuity rejection and
the precision upper limit. The ownership fixture additionally checks independent
coarse/fine buffers, retained copies, safe false-result reset, returned-local
lifetimes and results outliving their source curve.

Differential result: **88 cases / 35,116 Rust scalar comparisons**, covering
precision 0–99 and refusals at 100/255, degrees 1–65, rational weights, extreme
coordinates/scales, collapsed and unclamped domains, closed curves,
discontinuities and deterministic random inputs. Every returned point and the
reported tolerance are compared. Flags/counts are exact; other scalars use
relative tolerance `3e-12` without a blanket absolute tolerance. Signed-zero and
non-finite classification are checked. Native and Rust O0/O2 parity each hold
exactly. Large resource ceilings are retained in the implementation; the corpus
does not attempt million-point allocations or claim to exhaust every ceiling.

Compiler SHA-256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Evidence: `build/polyline-approximation3-checks/summary.json`,
`build/polyline-approximation3-differential.log`,
`build/polyline-approximation3-ownership.log`.

```powershell
python tests/cadkernel/polyline_approximation3/verify.py --source <pinned-checkout>
```

The runner checks the source revision and drift and records compiler/source
hashes. Windows uses MSVC rustc for native math parity; `--rustc` overrides it.
