# Curve clipping verification

`lib/cadkernel/clip2.dl` ports all production operations in
`src/geom2d/clip.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

The public patterns return parameter spans for breaking, trimming and clipping,
and endpoint pairs for clipped pieces. Break spans uses a null pointer for
`None`; a nonnull empty list represents `Some(Vec::new())`. Closed conics keep
the directed complement and may return parameters above one. Closed polylines
retain the source seam when both picks coincide. Trimming uses the first
matching interval, including the source tolerance behavior at a shared cut.

Curve values, boundary lists and cuts are borrowed. Returned raw lists are
caller-owned and must be freed. Span arrays and endpoint pairs contain values,
so they remain valid after the source curves are released. No result retains
an input list. `inside_pieces` returns endpoint pairs, matching the source;
it does not create new curve segments.

`main.dl` translates all 14 source tests and emits their original test names.
The verifier asserts the mapping against the pinned Rust file before running
both implementations. `clip2_ownership` checks repeated calls on managed
polylines and NURBS, balanced source and boundary ownership, unchanged inputs,
independent output lists, mutation isolation and results after full input
release.

The differential driver decodes runtime inputs and directly calls the unchanged
Rust module. It reuses the crossing driver's input decoder and numeric output
format; no geometry is reimplemented in the reference wrapper. Its 590 cases
cover all eight curve variants, equal and reversed picks, closed seams,
empty and holed boundaries, concave regions, tangencies, hatch lines, rays,
unbounded intervals, cuts at tolerance boundaries, signed zero, nonfinite
picks and cuts, degenerate speeds, and deterministic random geometry. In
particular, it exercises `trim_spans`, which has no original unit-test function.

Run from the repository root:

```text
python tests/cadkernel/clip2/verify.py --source /path/to/pinned/cadkernel
```

On Windows use the MSVC Rust toolchain for matching native math-library
behavior. Both reference and DynLex executables are built and run at O0/O2.
Counts and validity flags compare exactly. Scalar parameters use the common
relative tolerance of `3e-12`; points additionally allow 16 ULP at the recorded
geometry scale. Native O0/O2 and Rust O0/O2 outputs must each agree exactly,
including nonfinite classifications and signed zero. Invalid NURBS construction
is covered by the NURBS module; these inputs use valid curve objects and valid
positive finite tolerance values.

Verified on compiler SHA-256
`ec4f37ae0da9455353f8825f05f69caed3b3567e529d484e6037993f8c0e85bc`:
all 14 Rust tests and translated native groups pass in both modes, ownership
checks pass in both modes, and all 590 cases / 12,858 Rust scalar comparisons
pass with exact optimization parity. Local evidence is recorded in
`build/clip2-checks/summary.json` and `build/clip2-differential.log`.
This is module-level evidence; it does not establish complete main-crate coverage.
