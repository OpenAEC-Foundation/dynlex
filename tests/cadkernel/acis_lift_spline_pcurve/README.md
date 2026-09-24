# Inline spline and pcurve SAT lift

This bounded lift route reads an inline, open weighted 3-D spline edge and an
inline weighted UV pcurve in a complete faceted body. It preserves the SAT
record IDs as source provenance and expands knot multiplicities as the pinned
codec does. UV offsets and a malformed pcurve that the source skips are
covered. The reference swaps record positions to ensure pointers resolve by
record ID rather than array position.

The pinned Rust kernel and codec produce 239 complete trace lines across the
weighted, reordered and invalid-pcurve cases. The DynLex O0 and O2 binaries
must match every line, including source references, NURBS control points,
weights, knots, topology and edge parameters. The fixture also validates the
body and checks its worst vertex gap.

Run from the DynLex repository root:

```powershell
python -B tests/cadkernel/acis_lift_spline_pcurve/verify.py --source <pinned-kernel-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_lift_spline_pcurve
```

Referenced spline subtypes, referenced pcurves, spline surfaces, pcurve
surface-sense reversal and closed or periodic spline edges remain outside
this route. The main ACIS lift module is still in progress.
