# Planar arc-chain fitting

Complete native port of `src/geom2d/arc_fit.rs` at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`, including `unit`, `arc_piece`,
`fit_arc_chain`, the vertex record and all three original test groups.

Import `lib/cadkernel/arc_fit2.dl`. Call:

```text
the cad arc chain2 through points with closure closed and directions directions
```

`points` is a pointer to a list of `cad vector2`. `directions` is a pointer
to a list of `cad arc fit direction2`, with exactly one entry per point.
These are borrowed inputs: callers retain ownership of the two input lists.

Use `an automatic cad arc fit direction2` for an unspecified tangent and
`a cad arc fit direction2 along direction` for an explicit `cad vector2`.
The direction record contains `specified: boolean` and `value: cad vector2`.
An unspecified entry ignores its payload, as Rust `None` does. Explicit
directions are normalized during fitting.

The returned managed `cad arc fit result2` has `valid`, `vertices` and
`storage`. Each `cad arc fit vertex2` contains:

| Field | Type | Meaning |
| --- | --- | --- |
| point | cad vector2 | Original point or inserted join |
| bulge | binary64 | Signed circular bulge of the outgoing piece |
| source | word-sized unsigned integer | Original span index, matching Rust usize |
| fraction | binary64 | Distance fraction within the original span |
| inserted | boolean | Whether this is an added join |

Ordinary copies retain immutable shared storage. `the cad clone of fitted`
makes an independent list, including for a refused or reset result. Inputs may
be changed or freed after fitting. No manual freeing of result lists is needed.
The raw `vertices` view must not be mutated or freed; retain the result while
using it. Reset with `set fitted to a new (the type of fitted)` releases that
reference. Retained copies remain usable. A reset result has null storage;
inspect `valid` before reading its vertices. A constructor refusal instead
contains an allocated empty list and never exposes a partial chain.

Closed fits produce the closing span without appending a duplicate final
vertex. Open fits retain the final input point with zero bulge and fraction.
Interior automatic tangents bisect adjacent unit chords; free endpoint tangents
reflect the adjacent automatic tangent. Explicit directions override that frame.

Degeneracy and finite checks match the source: fewer than two points,
mismatched direction counts, nonfinite points, nonfinite explicit directions,
unusable chords/directions or failed arc pieces refuse the entire fit.
The stricter unit threshold is `1e-12`; automatic bisectors use the existing
general vector normalization. An opposing collinear override can still produce
a straight span because the source takes its collinear fallback. No stronger
global G1 guarantee is imposed on that fallback.

Run [the shared source/native driver](../construct2/README.md#reproduce-verification)
for the original tests, adversarial differential, geometry invariants,
lifetime checks and negative type probes. This directory holds the arc-fit
type refusals; the common driver and original-source probe are in
`tests/cadkernel/construct2/`.

