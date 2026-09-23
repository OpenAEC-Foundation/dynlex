# Curve crossings and containment

`lib/cadkernel/cross2.dl` ports all 416 production lines and 23 source test
groups of `src/geom2d/cross.rs`. `lib/cadkernel/containment2.dl` ports all 173
production lines and 16 source test groups of `src/geom2d/containment.rs`.
Both use cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

Crossings use the source's analytic straight/conic dispatch, exact polyline
pieces, adaptive subdivision and iterative refinement. The depth and iteration
limits remain 40. Results preserve stable ordering by the first parameter and
adjacent spatial deduplication. Coincident curves do not represent a finite
list of crossings; the source's existing behavior is retained.

Closest-point queries respect infinite, forward and bounded parameter extents;
bounded curves also compare both endpoints. Nearest-curve ties select the
earliest input. Containment counts a boundary point as inside, retries the
source's four ray directions after ambiguous endpoint or duplicate crossings,
and returns false when all directions are ambiguous. Empty boundaries are
outside. Nonfinite coordinates retain the source behavior; they are not
silently normalized. Tolerances must be finite and positive.

The public patterns are:

```text
the cad crossings of first and second within tolerance
the cad closest point on shape to target
the cad distance to shape from point
the cad nearest of curves to point
cad boundary curves contains point within tolerance
```

Curve and boundary inputs are borrowed. Crossing lists are separately owned
raw lists of scalar records and must be freed by the caller. Closest and nearest
results contain scalar records only. The ownership fixture checks repeated
calls, balanced managed payload references, unchanged inputs, independent
result lists and result validity after all source-curve owners are released.

## Verification

```sh
python tests/cadkernel/cross2/verify.py --source /path/to/pinned/cadkernel
```

On Windows the default Rust reference is the installed stable MSVC toolchain;
use `--rustc` and `--compiler` to select explicit executables. The script checks
the upstream revision and source blobs, compiles the unchanged Rust modules,
runs their original tests, runs both native source fixtures and the ownership
fixture, and compares runtime input cases at O0 and O2.

Verified with compiler SHA-256
`9ff4beef41a1cb2595e50bcf1ca8afed2cbd3238bd7512de99c7bfd13c8e63eb` and
MSVC Rust 1.91.1:

- All 39 original Rust test groups and all 39 translated native groups pass
  at O0 and O2.
- The initial matrix of 263 cases passes 9,888 scalar comparisons with Rust.
  A separate 30-case nonfinite-input run passes another 720 comparisons.
- Both native optimization modes agree exactly, as do both Rust modes,
  including signed zero and NaN classification.
- The ownership fixture passes at O0 and O2.

Counts and flags are exact. Other scalars use relative tolerance `3e-12`;
coordinate pairs additionally allow 16 ULPs of the input geometry magnitude
for platform math differences near zero. These comparison limits do not change
the geometric tolerance supplied to either implementation.

The matrix covers all 64 ordered curve-type pairs, analytic tangencies,
negative and zero radii, degenerate direction vectors, empty and concave
boundaries, duplicate boundaries, nested loops, ties, bulged segments and
deterministic random pairs. `--case` filters cases and `--skip-build` reuses
existing binaries in the explicit `--output` directory. A filtered report is
kept separate from a complete report.
