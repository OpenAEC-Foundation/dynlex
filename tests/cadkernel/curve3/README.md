# Spatial curve arithmetic

`lib/cadkernel/curve3.dl` translates all of `src/space/curve.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, including the angle-bisector function
after the original test module. Derived code is MPL-2.0.

| Pattern | Contract |
| --- | --- |
| `the cad bezier point of controls at t` | de Casteljau at any parameter; empty input yields zero and one control yields that point |
| `the cad bezier samples of controls with n segments` | Both endpoints; zero segments uses one segment |
| `cad segments from a to b and from c to d overlap within e` | Collinear positive-length overlap, including coordinate-scaled tolerance |
| `the cad curvature through a, b and c` | Curvature vector; zero for degenerate triples |
| `the cad angle bisector of a and b with normal n` | `cad normalization3` result; oriented antiparallel case and scaled normalization |

All coordinates use `cad vector3` and binary64 arithmetic. Lists are borrowed;
sampling returns a caller-owned list released with `free`. Negative segment counts
are outside the source unsigned-input domain and abort. Counts exceeding the
standard list length limit also abort before integer overflow. No degree cap or
parameter clamping is added to Bezier evaluation.

From the repository root:

```sh
python -B tests/cadkernel/curve3/verify.py --source /path/to/pinned/cadkernel
```

The runner checks the revision and unchanged source, runs all twelve original
Rust tests and their translated DynLex groups, then compares every public helper
over 494 cases. Result: **9,406 Rust comparisons**, with exact O0/O2 numerical
parity, including signed zeros and NaN classification. Scalar comparisons permit
relative error 3e-13 and absolute error 1e-300 against Rust. Count and Boolean
fields are exact. Both optimization levels also pass negative/overflow count
refusals. Processes have timeouts and Windows fault-dialog suppression.

Cases include arbitrary-degree/extrapolated Bezier curves, circle curvature,
nearly opposite and extreme-magnitude rays, coordinate-scale effects in segment
overlap, nonfinite coordinates and invalid tolerances. The source-test fixture
and both count refusals are registered in `tests/required/cadkernel_curve3*`.
