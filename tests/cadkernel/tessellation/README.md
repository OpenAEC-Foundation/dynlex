# Shared tessellation policy

Source: cadkernel `src/tessellation.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
The source has no unit tests; the native port adds analytic, differential
and type-contract checks. The reference imports the original module.

Run from the repository root:

```sh
python -B tests/cadkernel/tessellation/verify.py --source /path/to/cadkernel
```

The source checkout must have the pinned revision and an unchanged
`src/tessellation.rs`. Python, Git, rustc and the built DynLex compiler
are required. Use `--compiler` to override the platform default in `build/`.
The Windows subprocess guard preserves native failure statuses and prevents
modal crash dialogs. Generated files remain under `build/`.

Directions use nominal `cad vector2`/`cad vector3` values or fixed arrays
of binary64 coordinates. Array pairs must have identical dimensions,
including N=0. Binary32 arrays and mismatched dimensions are rejected.
The maximum-angle operation borrows a list of such directions.

An evaluator implements `the cad curve point of evaluator at parameter`
and `the cad curve tangent of evaluator at parameter`, both returning
`cad vector3`. Its record can contain borrowed captured state.
`the cad angular samples of evaluator with maximum angle limit`
returns an owned list; the caller frees it once. Sampling preserves the
source's evaluation order, nine tangent evaluations per node, minimum
depth 2 and maximum depth 16.

DynLex resolves these open evaluator patterns during pattern resolution,
before deciding which functions are used. Their definitions must therefore
be available when this module is imported, including policy-only callers.
`support.dl` supplies a typed line evaluator for the isolated refusal
tests. Standard curve adapters remain part of the curve-layer integration.

Verification on Windows at O0/O2 covers 372 cases and 827,664 comparisons
with Rust, with exact numerical optimization parity. Cases cover N=0/1/2/3/5/17,
threshold boundaries, underflow/overflow, infinities, NaNs, signed zero,
random directions, lines, helices, zero tangents and NaN tangents.
Every sampled point, callback count and first/last tangent parameter is
compared, including the 65,537-point limit cases. Rust comparisons allow
relative error 3e-13 and absolute error 1e-300; counts and parameter endpoints
are exact. NaN payload bits and fast-math are outside this verification.

Four invalid programs are also checked at both optimization levels.
A failed compile must exit 1, include its specific type diagnostic and
produce no executable. The 2D point evaluator was accepted before the
explicit 3D contract was added; the positive fixed-array fixture previously
failed for lack of an applicable dot-product overload.
