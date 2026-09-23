# Source-directed spatial joins

`lib/cadkernel/source_join3.dl` ports the complete `src/space/source_join.rs`
at revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`: 109 production lines,
five public operations and two source tests.

| Source operation | Native pattern |
| --- | --- |
| `line_as_nurbs` | `the cad line nurbs from start to end of degree degree` |
| `join_nurbs_curves` | `the cad joined nurbs of source and other within tolerance` |
| `join_collinear_lines` | `the cad joined line from start to end with otherStart to otherEnd within tolerance` |
| `join_counterclockwise_spans` | `the cad joined span from start to end with otherStart to otherEnd` |
| `join_cocircular_arcs` | `the cad joined arc span of source and other within tolerance` |

Scalar line/span results expose `valid`, `start` and `end`. NURBS results use
the existing managed `cad nurbs curve result`. Arc inputs hold a center,
normal, radius and angular endpoints. Invalid results are safe to copy/reset.

Line joins preserve source direction and include a collinear gap. Span joins
retain the source start, use the source Euclidean remainder and cap the end
at one revolution. Arc joins require compatible centers, positive radii and
normalized normals; opposite normals are rejected. NURBS joins preserve
endpoint preference, exact degree elevation, clamped-domain validation,
reversal, append/prepend behavior and rational seam scaling. When endpoints
differ within tolerance, the candidate endpoint moves to the source endpoint.
The original source controls and weights are preserved. Inputs are borrowed;
returned NURBS buffers are independent.

## Verification

Both original tests run unchanged in Rust and are translated in `main.dl`.
`source_join3_ownership` checks rational prepending, exact preservation of
source coordinates and weights, independent output buffers, retained copies,
returned-local results, reversal and safe refusal lifetimes. Both pass O0/O2.

The differential corpus covers **289 cases / 10,554 Rust scalar comparisons**:
all five operations, all four endpoint orientations, unequal degrees, rational
weights, near endpoints, disjoint/closed/unclamped curves, invalid weights,
degree boundaries, reversed/zero/extreme lines, periodic spans, incompatible
arc normals/radii, NaN/infinity and finite arithmetic overflow.
Flags/counts are exact; floating values use relative tolerance `3e-12` with
no blanket absolute tolerance. Signed-zero and non-finite classification are
checked. Rust and native O0/O2 results each agree exactly.

The runner imports complete unchanged source files for joins, NURBS, splines,
vectors and tessellation. It checks the pinned revision and source drift and
records compiler/source hashes. No reference geometry is reimplemented.

Compiler SHA-256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Evidence: `build/source-join3-checks/summary.json`,
`build/source-join3-differential.log`, `build/source-join3-ownership.log`.

```powershell
python tests/cadkernel/source_join3/verify.py --source <pinned-checkout>
```

Windows verification uses MSVC rustc to match the native math runtime.
`--rustc` selects another compatible compiler path.
