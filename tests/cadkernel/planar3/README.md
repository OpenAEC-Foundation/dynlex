# Planar curves in space

Source: `src/space/planar.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, including `common_curve_plane`
after the first test module. Native implementation: `lib/cadkernel/planar3.dl`.
The port and translated tests carry SPDX MPL-2.0 attribution.

All sixteen translated source tests and five helper/ownership groups pass
at O0/O2. Measurement delegates to the integrated `arclength2.dl` and
`deviation2.dl` modules. The combined fixture is registered as
`tests/required/cadkernel_planar3`.

## API

All scalar arguments below are `a 64 bit floating-point number`. Constructors
take a `cad curve2` and a `cad plane`. Other operations take `cad planar curve3`.

```text
a cad planar curve3 of curve on plane
a flat cad planar curve3 of curve
    -> cad planar curve3 (plane: cad plane, curve: cad curve2)

the cad clone of shape                  -> cad planar curve3
free cad planar curve shape             -> action resetting the caller's value
left = right                            -> boolean (structural equality)

the cad point on shape at parameter     -> cad vector3
the cad parameter on shape nearest point
    -> cad planar parameter result (valid: boolean, value: f64)
the cad tangent of shape at parameter   -> cad vector3
the cad normal of shape                 -> cad normalization3 (valid, value: cad vector3)
the cad extent of shape                 -> cad curve2 extent
shape is cad closed                     -> boolean

the cad samples of shape with density density
the cad angular samples of shape within angle
    -> caller-owned raw list of cad vector3

the cad curve point of shape at parameter   -> cad vector3
the cad curve tangent of shape at parameter -> cad vector3

the cad common plane of curves within tolerance
    -> cad plane result (valid: boolean, value: cad plane)
```

The last operation borrows a raw list of `cad planar curve3` values. It preserves
the source's exact affine-support selection: two endpoints for Line/Ray/XLine,
every vertex for straight polylines, every NURBS control point, and the three
storage-basis points for conics or nonzero-bulge polylines. The last maximum wins
on ties, using Rust binary64 total ordering. Empty, collinear, noncoplanar and
nonfinite support is refused. Tolerance must be finite and strictly positive.
Conic support does not validate the conic's separate radius/centre fields.

## Measurement operations

The implementation already contains the following forwarding patterns:

```text
the cad length of shape                          -> f64
the cad point on shape at distance distance      -> cad vector3
the cad parameter on shape at distance distance  -> f64
the cad samples of shape within tolerance        -> caller-owned Vec3 list
```

These forward to the corresponding `cad curve2` operations in the imported
measurement modules. No separate sampling or length algorithm is embedded in
the planar wrapper.

## Numerical behavior and ownership

The frame is preserved exactly, including scale and shear. It never changes
the 2D parameter. XY-aligned lowering returns the stored elevation directly;
aligned lifting subtracts only origin X/Y and ignores world Z, including a
nonfinite Z. General lifting projects through the full skew frame. A failed
projection preserves `None` as an invalid typed result; a valid result may
still carry the source's nonfinite scalar answer. No extra validation is added.

Lengths and linear sampling tolerances remain in the plane's own coordinates.
The source assumes a length-preserving frame when interpreting them as world
units; this wrapper neither rescales them nor rejects a scaled frame.

The wrapper has ordinary managed fields and uses automatic nested lifecycle
traversal. Construction borrows/retains a curve. Ordinary copies share its
immutable NURBS/polyline allocations; explicit `the cad clone of shape`
deep-copies those allocations through the typed `cad curve2` clone operation.
Free/reset releases only that variable's ownership. Shape list fields remain
borrowed and read-only. A returned sampling list is a separate caller-owned
allocation and must be freed once. Common-plane inference frees its temporary
support list on every non-aborting path.

## Verification

```text
python -B tests/cadkernel/planar3/verify.py --source /path/to/pinned/cadkernel
```

The runner checks the revision and unmodified Rust source, executes all sixteen
original Rust tests, and compares native results with compiled Rust output.
All compiler and program executions use guarded processes, bounded timeouts
and fresh binary paths. Input and compiler hashes must remain stable.

Verified with compiler SHA-256
`1d89c3a8a361349105e797c31eccde8b1e84c9865953aeaadb7684f322bb714b`:

- All sixteen original Rust tests passed.
- All ten fixture/mode combinations passed: basic (13 source tests),
  measurement (3 source tests), helpers, NURBS ownership, and their combined
  integration fixture, each at O0/O2.
- The ownership checks cover shared and deep copies, independent frames,
  retained storage after reset, nested knot insertion, and balanced reference
  counts after growing and freeing lists of managed planar curves.
- 453 differential cases passed, with 2,578,514 Rust field comparisons.
  These cover all eight variants, scaled/skew/degenerate/large frames,
  nonfinite coordinates, total-order support selection, parameter inversion,
  length and distance mapping, and all sampling modes.
- Common-plane results and discrete fields match exactly, including signed
  zero. Other scalar/vector results allow `2e-12` relative error or `2e-12`
  times the largest finite output magnitude (minimum scale 1). NaNs are
  compared by classification. Native O0/O2 outputs match exactly.

Local evidence is in `build/planar3-verify-jsybj436/`: `result.json`,
`inputs.json`, original-test output, compile logs and replayable reference
cases. Windows verification used the MSVC Rust toolchain. The source remains
the authority for numerical limitations and behavior on degenerate input.
