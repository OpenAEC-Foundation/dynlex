# Spline verification

Source: cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`,
`src/space/spline.rs`, SPDX MPL-2.0.

Run from the repository root with Python 3, rustc, and the built compiler:

```sh
python tests/cadkernel/spline/verify.py --source /path/to/cadkernel
```

The compiler defaults to `build/dynlex` on Unix and `build/dynlex.exe` on
Windows. Override it with `--compiler /path/to/dynlex`. Executables are
launched with Python subprocess, including Windows files ending in `.out`.
Generated drivers, LLVM files and executables belong under `build/`.

The runner checks the source revision and unchanged upstream file, compiles
the independent fixtures at O0/O2, checks tangent dimension rejection, runs
all five original Rust tests, and compares runtime numerical outputs against
the unmodified Rust module. The reference is test-only; the production port
contains no Rust calls.

## Public API

Points are fixed-size f64 arrays of any dimension. Input knot/point collections
and getter contexts are borrowed. A custom getter returns the same fixed array
type and can mutate its context through a pointer:

```text
the cad spline point at index from source
the cad de Boor value of degree degree with knots knots and points points at parameter
the cad de Boor value of degree degree with knots knots and count points from source at parameter
the cad span of degree degree with knots knots and last last at parameter
the cad clamped uniform knots of degree degree for count points
the cad open spline through points using parameterization
the cad open spline through points with start tangent start and end tangent end using parameterization
the cad periodic spline through points using parameterization
```

Getter evaluation preserves call count and ascending index order. Empty input
uses the getter's return type without calling it. Degree zero uses the last
knot at or below the parameter and clamps its index to the point count.
Degree below 16 uses the stack; other degrees use a temporary heap buffer.
Evaluation retains homogeneous coordinates without rational division.
The uniform-knot constructor returns a newly allocated list; the caller
releases it with `free knots`. de Boor returns an array value and releases
its temporary heap buffer internally.

Parameterization values are `the cad uniform parameterization`,
`the cad centripetal parameterization`, and `the cad chord parameterization`.
Tangents are `a cad spline tangent with value point` or
`no cad spline tangent matching prototype`. Their dimension must match the
fit points. Absent and squared-norm-at-most-1e-18 tangents use natural
boundary conditions.

`cad spline interpolation` has `valid`, `controls`, and `knots`.
Every result transfers manual ownership of two lists to the caller, including
an invalid result. Release it exactly once with
`free cad spline interpolation result`. Copying the result copies its raw
pointers; it does not retain, clone, transfer ownership or automatically release
the allocations. Only one copy may release them. Valid results are cubic.
The caller supplies the well-formed degree/point/knot relationship required
by upstream de Boor evaluation.

## Coverage and verification

The independent fixtures cover all five upstream tests, analytic polynomial
and rational examples, N=1/2/5 interpolation, natural and clamped
endpoints, cyclic square handles, knot and tangent thresholds, nonfinite
data, near-duplicate closure points, and mutable getter behavior.

Runtime differential cases cover N=0/1/2/3/4/5/8/17, degrees 0/1/2/3/15/16/20,
all parameterizations, start-only/end-only/both tangents, extrapolation,
coincident points, empty inputs, survey coordinates, NaN, infinities and
large/small coordinates. All controls, knots and getter traces are compared.
Numerical comparisons allow relative error 3e-13 and absolute error 1e-300;
O0/O2 parity requires equal numbers and zero signs. NaN payload bits are not
compared. Fast-math is not used.

On compiler base `332dac1385edcbe6458386a5119b4cc62d010581` with the
[zero-sized allocation correction](../../../docs/cadkernel-zero-sized-allocation.md),
the complete runner exits 0: all five original Rust tests, independent
DynLex fixtures at O0/O2, tangent-dimension rejection, 804 differential
cases and 56,876 comparisons pass. N=0 open and periodic interpolation
also pass. Fixture compile times were 1.20/1.97 seconds and differential
probe compile times 1.94/3.67 seconds for O0/O2 on Windows.

`zero_size_list.dl` retains the allocation reproducer and
`zero_dimension.dl` retains the interpolation fixture. The runner always
executes the supported N=0 interpolation cases. These results verify this
module; they do not establish completion of the entire main crate or of
the compiler regression suite.
