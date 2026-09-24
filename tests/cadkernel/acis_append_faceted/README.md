# In-memory ACIS append: faceted bodies

`lib/cadkernel/acis_append_faceted.dl` ports a bounded branch of the
pinned `src/acis/append.rs`. It appends B-rep vertices, straight and circular
curves, planar and spherical surfaces, and their topology to a SAT document, preserving record order and
pointer links. The input body is borrowed; the document is mutated only after
topology and supported-geometry checks pass. The native implementation uses
dense arena indices for this initial slice.

The differential runner builds the pinned Rust kernel and codec offline. It
compares two successive cuboid appends with the native output at O0 and O2:
171 record lines, including record types, pointers and exact floating-point
token bits. The required fixture also confirms that a cylindrical body is
refused without changing the document.

```powershell
python tests/cadkernel/acis_append_faceted/verify.py --source PATH_TO_PINNED_KERNEL --codec PATH_TO_PINNED_CODEC
```

The separate spherical differential is in
`tests/cadkernel/acis_append_sphere/README.md`. This is an in-memory append
operation, not a SAT/SAB parser or writer. Other analytic curves and surfaces,
NURBS, pcurves and sparse arenas remain open. The full source module is still
in progress.
