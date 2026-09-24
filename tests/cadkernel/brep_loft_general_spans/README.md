# General loft: rational span preparation

This sidecar ports the `rational_spans` stage of pinned `src/brep/loft_general.rs`
at revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It converts a
borrowed `cad rational curve3` into exact homogeneous Bézier pieces. It validates
the source degree, control and knot counts, homogeneous weights, and active knot
domain; inserts knots to the required multiplicity, including the active ends of
unclamped splines; and returns normalized span intervals and four-component
homogeneous controls.

`the cad loft general decomposition of curve` returns `valid`, `errorCode`, and
owned `spans`. Error codes 1–6 correspond, in source order, to malformed spline,
invalid weight, invalid knot domain, failed refinement search, zero refinement
width, and absence of nonzero spans. A failure has no spans. The function copies
its working data and does not mutate the input. Result data remains usable after
the input curve is released. A copied result retains the span lists.

The differential driver checks both revision and SHA-256 of the source file,
extracts the four relevant Rust function bodies verbatim, and compiles the Rust
reference at O0 and O2. It compares validity, refusal code, span intervals, and
every homogeneous control coordinate against the DynLex probe. The required
fixture also checks input independence, source release, result retention, and
2,000 repeated constructions on both optimization levels.

Run from the repository root:

```text
python -B tests/cadkernel/brep_loft_general_spans/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_loft_general_spans
```

This is a preparation stage only. It does not order section wires, align
profiles, build loft patches or caps, or create B-rep topology. Those operations
belong to later slices of the general loft port.
It mirrors the pinned function's validation boundary: an interior nonfinite
knot may propagate nonfinite span data, so a later geometry consumer must
validate its own requirements before building topology.
