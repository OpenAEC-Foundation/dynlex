# Spatial polygon measurements

The measurement functions in `lib/cadkernel/polygon3.dl` translate
`src/space/polygon.rs` at `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
Derived code is MPL-2.0. The source's planar triangulation adapters are still
pending integration with the native 2D triangulator.

The public patterns are `the cad ring area vector/area/normal/perimeter of ring`
and `the cad chain length of points`. They borrow lists of `cad vector3`.
Normal returns the core `cad normalization3` result. Area uses the first point
as its origin to retain precision at large coordinates. Open-chain summation
preserves the source's negative zero for an empty sum. A repeated closing point
does not alter area or perimeter.

```sh
python -B tests/cadkernel/polygon3/verify.py --source /path/to/pinned/cadkernel
```

All seven translated source tests pass O0/O2. The differential runner checks
212 cases and **4,072 Rust comparisons**, with exact O0/O2 numerical parity.
It checks the source revision and unchanged modules, compiles the original
Rust measurement functions and verifies empty/degenerate rings, winding,
survey coordinates, duplicate points, extreme magnitudes and nonfinite inputs.
The triangulation adapters are not included in this measurement result.
