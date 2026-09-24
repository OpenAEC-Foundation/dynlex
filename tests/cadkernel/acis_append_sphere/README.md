# In-memory spherical SAT append

The native append route now writes circular edge curves as `ellipse-curve`
records and spherical faces as `sphere-surface` records. Their positions,
normals, major axes, radii and pointers follow the pinned `src/acis/append.rs`
layout. It accepts dense B-rep arenas without pcurves and appends to an
existing document, preserving earlier records. The source body is borrowed.

The verifier pins kernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`
and codec revision `70ac6da7cf149cea6398a3d8829dd5e48b485b96`. It
compares two successive spherical-body appends against Rust at O0 and O2:
31 complete lines per mode, including every SAT token and its binary64 bits.
The earlier two-cuboid differential remains a regression check. A cylindrical
body is still refused before the document is changed because its surface
record is outside this bounded route.

Run from the repository root:

```text
python -B tests/cadkernel/acis_append_sphere/verify.py --source <pinned-source-checkout> --codec <pinned-codec-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_acis_append_sphere --filter cadkernel_acis_append_faceted
```

Conical, cylindrical, toroidal, elliptical and spline geometry, pcurves and
sparse arenas remain outside this native append route. This is in-memory SAT
record construction, not a file parser or writer.
