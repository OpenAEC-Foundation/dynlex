# NURBS in three dimensions

Native DynLex port of `src/space/nurbs.rs` at cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, licensed MPL-2.0.
The source has 1,262 lines before its test module and **17 tests**.
The implementation imports the binary64 CAD records, generic spline evaluator
and shared angular policy.

## API

All point inputs and outputs use `cad vector3`. Curves own lists of points,
knots and weights. Surfaces own rectangular lists of point rows and weight rows.
Constructors and fallible transformations return a typed `valid/value` record.

| Operation | Pattern |
| --- | --- |
| Permissive curve | `the cad nurbs curve3 of degree D with controls P, knots K and weights W` |
| Strict curve | `the strict cad nurbs curve3 of degree D with controls P, knots K and weights W` |
| Control polygon | `the cad nurbs curve3 of degree D through control polygon P with periodicity C` |
| Weighted polygon | `the cad nurbs curve3 of degree D through control polygon P with weights W and periodicity C` |
| Open fitting | `the cad nurbs fit through P using S` |
| Constrained fitting | `the cad nurbs fit through P with start tangent A and end tangent B using S` |
| Periodic fitting | `the cad periodic nurbs fit through P using S` |
| Reversal | `the cad reversal of C` |
| Degree elevation | `the cad elevation of C by N` |
| Control removal | `the cad reduction of C without control vertex I` |
| Curve evaluation | `the cad point on C at T`, `the cad point on C at knot U` |
| Analytic derivative | `the cad derivative of C at knot U` |
| Finite differences | `the cad tangent of C at T`, `the cad tangent of C at knot U`, `the cad acceleration of C at knot U` |
| Closest parameter | `the cad parameter on C nearest P` |
| Sampling | `the cad samples of C within E`, `the cad samples of C with maximum angle A` |
| Curve properties | `the cad degree/domain/periodicity/control points/knots/weights of C`, `C is cad rational`, `C is cad closed` |
| Curve periodicity | `C with cad periodicity B` |
| Permissive surface | `the cad nurbs surface3 of degrees U and V with controls P, knots UK and VK and weights W` |
| Strict surface | `the strict cad nurbs surface3 of degrees U and V with controls P, knots UK and VK and weights W` |
| Control net | `the cad nurbs surface3 of degrees U and V through control net P with periodicity UC and VC` |
| Surface evaluation | `the cad point on S at parameters U and V`, `the cad point on S at knots U and V` |
| Tangents | `the cad tangents of S at knots U and V` returns `valid/u/v` |
| Normal | `the cad normal of S at parameters U and V`, `the cad normal of S at knots U and V` returns `cad normalization3` |
| Isocurve | `the cad isocurve of S on axis A at P` |
| Surface properties | `the cad degrees/domain/periodicity/control points/knots/weights of S`, `the cad v reversal of S` |
| Surface options | `S with cad u periodicity U and v periodicity V`, `S with cad v reversal B` |
| Value equality | `A = B`, `A != B` compares all source fields, excluding allocation identity |
| Independent clone | `the cad clone of C`, `the cad clone of S` |
| Early release | `free cad nurbs VARIABLE` |

Spacing values and optional tangents use the existing spline types:
`the cad uniform/centripetal/chord parameterization`,
`a cad spline tangent with value [X,Y,Z]`,
`no cad spline tangent matching [0.0,0.0,0.0]`.
The explicit bridges are `the cad nurbs coordinates of VECTOR` and
`the cad nurbs vector of ARRAY3`.

A curve's domain has `start/end`; a surface's domain has `u/v` intervals.
Surface degrees, periodicity and knots return two-element arrays.
Axis 0 fixes the surface's u parameter; axis 1 fixes v.
Curve evaluator adapters implement the shared normalized-parameter
`the cad curve point/tangent of C at T` protocol.

There is no independent public knot-removal or arbitrary reparameterization
method in this pinned module. It inserts knots privately during periodic
extraction and Bezier decomposition, removes a control vertex with the associated
knot adjustment, reverses the parameter domain, and maps normalized parameters
to knot domains. Those source operations are translated without adding a
different algorithm.

## Ownership

- Constructors borrow inputs and copy them. Callers may modify or free input
  lists immediately after construction.
- Ordinary record assignment retains shared storage. This is a safe shared
  lifetime, not an independent mutable copy. The `owner` field is internal.
- Exposed list pointers are **read-only borrowed views**. Do not free them,
  mutate them, replace their fields, or assume that copying a raw pointer retains
  the owner.
- `the cad clone of ...` duplicates every owned list and every surface row.
  Geometric transformations allocate independent storage. Periodicity and
  v-reversal flag updates retain the same read-only lists with a safe lifetime.
- Paired retain/release hooks release all owned children once on the final
  reference. Invalid results have the same lifetime contract.
- `free cad nurbs VARIABLE` is a caller-scope replacement assigning a zero value.
  It safely releases that reference; other copies remain alive. Repeated reset is
  safe. Do not apply `destroy at` to these managed locals.
- Tessellation returns a manually owned standard list; call `free` once.

## Verification

From the worktree root, with a built native compiler and Rust available:

```text
python -B tests/cadkernel/nurbs3/verify.py --source /path/to/pinned/cadkernel
```

The default compiler is `build/dynlex.exe` on Windows and `build/dynlex`
elsewhere; override with `--compiler PATH`. The source path is required.
The driver checks the revision and unchanged source modules. Every child process
uses `scripts/process_error_mode.py`; failures keep their original status.
Generated drivers, logs and binaries remain in `build/cadkernel-nurbs3-checks/`.

The default verification runs:

1. All 17 translated upstream groups and independent rational/Bernstein values
   at O0 and O2.
2. Ownership cases: borrowed input detachment, copied handles surviving resets,
   list element retain/release, deep curve/surface-row clones, invalid results,
   and repeated explicit release.
3. All 17 original Rust tests, directly including the unchanged pinned modules.
4. Runtime curve/surface probes with complete data and numerical output against
   Rust, plus exact O0/O2 parity including signed zero and NaN classification.
5. Wrong bridge dimensions/precision, wrong fit tangent dimensions and private
   helper visibility are rejected for the intended diagnostic at both levels.

The cases cover all constructors, option combinations, strict refusals,
permissive fallback dimensions, weighted periodic extraction, elevation and
removal, all interpolation parameterizations and endpoint tangent combinations,
zero/repeated/short knot spans, weight projection thresholds, nonfinite values,
domain clamping/wrapping, seam precision, surface reversals and both isocurve
axes. Degrees cross the spline stack/heap boundary and extend to 24 for curves
and 17 for surfaces. Seeded cases supplement the explicit branches.

`--reference-only` runs the upstream tests and all Rust cases without compiling
DynLex. It is not a differential or DynLex pass.
The complete verification passed on the Windows native compiler at O0 and O2:

| Check | Result |
| --- | --- |
| Translated upstream groups | 17 groups at each optimization level |
| Required numerical and ownership fixtures | 4 fixture/mode combinations, exact output and no diagnostics |
| Original pinned Rust tests | 17 passed |
| Runtime differential | 704 cases; 105,220 numeric comparisons against Rust |
| Cross-optimization comparison | Exact O0/O2 agreement, including signed zero and NaN classification |
| Type and visibility refusals | 8 expected rejections with diagnostic checks |

The differential permits relative error `3e-12` and absolute error `1e-300`
against the separately compiled source. It requires matching infinities,
NaN classifications and zero signs. All DynLex fixture/probe compilations in
the complete run took 1.607–2.625 seconds. The output log is
`build/cadkernel-nurbs3-checks/verification.log`.

The required fixtures were also checked through the repository's strict runner:

```text
python -B tests/cadkernel/verify.py --filter "cadkernel_nurbs3*"
```

These checks cover this module on native Windows. They do not establish a
complete-kernel port, other target support, exhaustive floating-point coverage,
or an allocation-failure/leak-sanitizer audit. Degrees and list sizes retain the
standard library's integer/index representation; no algorithmic degree cap is
introduced.

## Compiler regression

`lifecycle_scope_direct.dl` is an import-free regression for a variable-scope defect:
a release hook and main use the same local identifier. The declaration binder's
`enclosingVariableFunctionScope` in
`src/cpp/compiler/patternResolutionVariableDeclarations.inl` previously omitted
retain/release sections. `highestImplicitVariableSection` could therefore merge
the two references. The generated release function used main's stack allocation,
failing LLVM verification with
`Referring to an instruction in another function!`.
`lifecycle_scope.dl` also exercises this through a section replacement.

The scope repair recognizes lifecycle sections as function boundaries and
includes the boundary itself in variable lookup. Four required
`lifecycle_local_scope*` regressions plus six existing global/lifecycle fixtures
passed at O0 and O2 (20 fixture/mode combinations).

`nested_loop_exit.dl` is a separate 13-line import-free regression. A while
condition lazily generates a helper containing another loop. Taking a reference
to the active element of the `sectionFlexBodyFrames` vector before generating
that condition allowed reallocation to invalidate the reference. Writes to the
old frame left the caller's `while_exit` block without a terminator. The codegen
repair acquires the reference after generating the condition.

Both defects were reproduced at O0 and O2 before their repairs. The final
NURBS validation above uses the repaired compiler; production NURBS algorithms
and ownership contain no compiler workarounds.
