# Spatial curve port review

Scope: the complete `src/space/curve.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, including `angle_bisector` after the
test module; `lib/cadkernel/curve3.dl`; all three `cadkernel_curve3*` required
fixtures; and the reference driver and comparison logic in
`tests/cadkernel/curve3`.

## Findings

No concrete correctness defects found in the reviewed scope.

The source requires de Casteljau evaluation without a degree cap or parameter
clamping, empty/single-control behavior, endpoint-inclusive sampling, the exact
coordinate-scale overlap tolerance, the two curvature degeneracy checks, and
scaled angle-bisector normalization with lazy oriented-normal fallback. These
behaviors are present in `curve3.dl`. Core Vec3 normalization and interpolation
use the same thresholds and arithmetic order as the pinned vector source.

The DynLex list length is a signed standard-library integer. Refusing negative
counts and counts whose inclusive endpoint would exceed that capacity is a
documented representation boundary, not an upstream geometric tolerance.
Both refusal fixtures test explicit abort behavior before sampling.

The list header accepts a generic list, but the typed Vec3 assignment and the
Vec3 working list constrain the element type. An independent empty-f32-list
probe confirms that a zero runtime length does not bypass that type contract.

The comparison runner distinguishes discrete count/Boolean fields and checks
O0/O2 results exactly, including zero signs and NaN classes. Its Rust comparison
tolerance is explicitly `rel_tol=3e-13`, `abs_tol=1e-300`; the reported 9,406 are
scalar comparisons across both native optimization levels, not 9,406 inputs.

## Verification

Compiler: `build/dynlex.exe`, SHA-256
`9682cf3b88de4b6db6c8b82bc3a7ca6517437b81db9dbd7225bcc5d27eb16234`.
No compiler or existing library/required-fixture edits were made for this review.

- Original Rust tests: 12 passed.
- Required success fixture and negative/overflow count refusals: all six
  fixture/optimization combinations passed.
- Differential runner: 494 cases, 9,406 Rust scalar comparisons passed;
  exact O0/O2 parity.
- Additional boundary probe: seven cases, 92 exact Rust scalar comparisons
  passed. These cover signed-zero single controls with a NaN parameter,
  overflow during Bezier interpolation, oriented antiparallel/subnormal rays,
  overflow-length segments and the positive-overlap tolerance boundary.
- `tests/cadkernel/curve3-review/wrong-controls.dl`: rejected on O0/O2 with
  `requires cad vector3` for the f32 element type.

The existing reproduction command is:

```text
python -B tests/cadkernel/curve3/verify.py --source /path/to/pinned/cadkernel
```

On this Windows host, the rustup executable emitted a canonicalization warning
for the user profile, which the runner correctly rejected as unexpected compiler
output. Prepending the installed
`C:/Users/rickd/.rustup/toolchains/stable-x86_64-pc-windows-gnu/bin` to the child
process PATH selected `rustc.exe` directly and allowed the unchanged runner to
complete. No diagnostics were filtered out. All native subprocesses used the
existing Windows process-error-mode guard.

This is a source and numerical review of the curve module. It does not certify
the entire compiler suite or provide heap-instrumentation evidence.
