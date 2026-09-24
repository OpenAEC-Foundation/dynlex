# In-memory faceted ACIS lift

`acis_lift_faceted.dl` reads SAT records and their pointers into native B-rep
topology. This first slice accepts planar faces, straight edges, point records
and closed coedge rings. It preserves shared vertices and edges, orientation
and source provenance. The input document is borrowed. The required fixture
creates its in-memory document through the existing native append operation;
the lift itself reads the SAT records directly.

The differential compares 160 trace lines with the pinned Rust kernel and
codec on O0 and O2, including swapped record slots, orientation and a
point-tolerance token. Both source revisions are checked before the run.

```powershell
python -B tests/cadkernel/acis_lift_faceted/verify.py --source PATH_TO_PINNED_KERNEL --codec PATH_TO_PINNED_CODEC
```

Unsupported or malformed records are refused with an empty result body. The
source implementation can instead return a partial body with a `Loss` report;
that behavior, analytic and spline geometry, pcurves and general SAT entity
views remain outside this slice. This is an in-memory operation, without a
native SAT/SAB file parser or writer.
