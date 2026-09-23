# Planar NURBS

Native DynLex port of `src/geom2d/nurbs.rs` from cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, licensed MPL-2.0.
The source contains 930 lines before its test module and 35 tests.
All public points use the explicit binary64 `cad vector2` record.

## API

`P` is a borrowed standard list of `cad vector2`; `K` and `W` are
borrowed lists of binary64 values. Constructors copy their inputs.
Empty weights supply the source's absent-weight behavior.

| Operation | Pattern | Result |
| --- | --- | --- |
| Permissive constructor | `the cad nurbs curve2 of degree D with controls P, knots K and weights W` | `cad nurbs2 result` |
| Strict constructor | `the strict cad nurbs curve2 of degree D with controls P, knots K and weights W` | `cad nurbs2 result` |
| Open interpolation | `the cad nurbs2 fit through P using S` | `cad nurbs2 result` |
| Endpoint tangents | `the cad nurbs2 fit through P with start tangent A and end tangent B using S` | `cad nurbs2 result` |
| Periodic interpolation | `the cad periodic nurbs2 fit through P using S` | `cad nurbs2 result` |
| Polyline fitting | `the cad nurbs2 fit to polyline P within E` | `cad nurbs2 result` |
| Point | `the cad point on C at T`, `the cad point on C at knot U` | `cad vector2` |
| Analytic derivative | `the cad derivative of C at T`, `the cad derivative of C at knot U` | `cad vector2` |
| Nearest normalized parameter | `the cad parameter on C nearest P` | binary64 |
| Samples per knot span | `the cad samples of C with N pieces per span` | manually owned point list |
| Reversal | `the cad reversal of C` | `cad nurbs curve2` |
| Degree elevation | `the cad elevation of C by N` | `cad nurbs2 result` |
| Insertion as a value | `the cad knot insertion of U into C` | `cad nurbs curve2` |
| Insertion action | `insert cad nurbs knot U into C` | nothing; replaces caller variable |
| Split | `the cad split of C at T` | `cad nurbs2 split` |
| Trim | `the cad trim of C from A to B` | `cad nurbs2 result` |
| Degree | `the cad degree of C` | integer |
| Domain | `the cad domain of C` | `cad nurbs2 interval`, binary64 `start/end` |
| Borrowed data | `the cad control points/knots/weights of C` | read-only borrowed lists |
| Predicates | `C is cad rational`, `C is cad closed` | boolean |
| Content equality | `A = B`, `A != B` | boolean |
| Deep clone | `the cad clone of C` | independent `cad nurbs curve2` |
| Early release | `free cad nurbs C` | nothing; resets caller variable |
| Array bridges | `the cad nurbs2 coordinates of P`, `the cad nurbs2 vector of A` | binary64 array2 / `cad vector2` |

Fallible results expose `valid/value`; split results expose
`valid/left/right`. Check `valid` before using geometric payloads.
All payloads, including invalid ones, have a safe managed lifetime.

Interpolation uses the existing `cad spline parameterization` values:
`the cad uniform parameterization`, `the cad centripetal parameterization`,
and `the cad chord parameterization`. Optional tangents are
`cad spline tangent` values containing binary64 arrays of exactly two items:
`a cad spline tangent with value [X,Y]` or
`no cad spline tangent matching [0.0,0.0]`.

## Source distinctions and reuse

The generic spline module supplies de Boor evaluation, span lookup, uniform
knots and periodic interpolation. Planar algorithms whose contracts differ from
the spatial module are implemented directly:

- The permissive constructor replaces any nonpositive or NaN weight vector
  with unit weights, while positive infinity remains accepted. Strict
  construction requires finite coordinates, positive finite weights and a
  finite nondecreasing knot vector with distinct outer endpoints.
- Evaluation clamps to the domain. A curve created by periodic interpolation
  still clamps; this type has no periodicity flag that enables wrapping.
- The homogeneous derivative is not rescaled by the maximum weight. A knot
  width with absolute value below `1e-15` gives a zero derivative control
  factor; tiny homogeneous output weight returns the raw planar slope.
  The normalized derivative multiplies by the knot-domain width.
- Parameter search uses at least 64 intervals, or eight per control point,
  followed by at most 60 ternary rounds. It returns a normalized parameter.
- Reversal mirrors the entire knot vector about its outer endpoints.
  Elevation by zero is invalid. Degree elevation replaces a tiny homogeneous
  weight with one before projecting its control point.
- Open interpolation accumulates chord-derived knot positions before deriving
  interval widths. Its axiswise Thomas solver has no extra pivot or finiteness
  validation. Delegating this operation to the generic open interpolator would
  change numerical behavior and refusal semantics.
- Polyline fitting deduplicates adjacent points, preserves direction reversals
  during simplification, and constructs rounded cubic segments within the
  source allowance. Zero tolerance returns the original degree-one polyline.
- Splitting and trimming use exact homogeneous knot insertion, preserving
  weights. Reversed trim limits reverse the resulting curve.

No degree cap, dimensional padding, native geometry wrapper or compiler
workaround is used.

## Ownership

Each curve owns its control, knot and weight lists through a paired
retain/release lifecycle. Ordinary copies retain shared read-only storage.
`the cad clone of C` duplicates every list. Constructor inputs remain borrowed;
the caller may free or mutate them after construction.

Returned raw list pointers do not retain their owner. Do not free them, mutate
them or replace raw fields. Their validity ends when the last owning curve
reference is released. The allocation counter is internal.

Knot insertion creates an independent modified curve and replaces the caller's
variable. Copies made before insertion keep their original contents. Reacquire
borrowed list views after replacing the owner. Splits, trims, reversals and
elevations have independent owned storage; managed result fields retain it
across assignment, return and parent-result destruction.

`free cad nurbs VARIABLE` is a caller-scope replacement assigning a zero value.
Repeated reset is safe and other retained copies remain valid. Do not manually
destroy a managed local that will receive automatic cleanup.

Tessellation alone returns a manually owned standard list. Free that list once.

## Verification

Run from the worktree root with a built DynLex compiler and Rust available:

```text
python -B tests/cadkernel/nurbs2/verify.py --source /path/to/pinned/cadkernel
python -B tests/cadkernel/verify.py --filter "cadkernel_nurbs2*"
```

The source path is required. The compiler defaults to `build/dynlex.exe` on
Windows and `build/dynlex` elsewhere; override with `--compiler PATH`.
Every native subprocess uses `scripts/process_error_mode.py`. Reference modules
are included unchanged after revision and dirty-source checks.
Generated artifacts remain in `build/cadkernel-nurbs2-checks/`.

Verified on native Windows at O0 and O2:

| Check | Result |
| --- | --- |
| Original pinned tests | 35 Rust tests passed |
| Translated source tests | All 35 groups at each optimization level |
| Ownership | Copies, deep clone, detached insertion, list retention, nested split lifetime and invalid resets passed at each level |
| Combined imports | Ownership fixture imports both planar and spatial NURBS |
| Refusals | 10 intended rejections for bridge dimension/precision, tangent dimension, weight precision and local visibility |
| Differential | 567 runtime cases; 61,796 comparisons with Rust |
| O0/O2 | Exact agreement, including signed zero and NaN classification |
| Required fixtures | 4 fixture/mode combinations, exact output and no diagnostics |

The comparison allows relative error `3e-12` and absolute error `1e-300` against
Rust; infinities, NaN classifications and zero signs must agree. The cases cover
every public operation, strict/permissive constructors, high degrees through 24,
nonfinite and tiny weights, repeated and tiny knot spans, unclamped knots,
endpoint/outside/NaN parameters, insertion, splits and trims, all interpolation
spacings and optional tangents, repeated/nonfinite fit points, polyline
simplification reversals, degenerate rounding and seeded curves.

`--reference-only` runs the 35 Rust tests and all reference cases without
compiling DynLex. It does not establish a differential pass.

These results establish this module on native Windows. Other targets,
exhaustive floating-point coverage, allocation-failure injection and a
leak-sanitizer audit remain outside these checks. Standard list indexing retains
the language library's integer representation. This module does not establish
completion of the whole main-crate port.
