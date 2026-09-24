# In-memory ACIS geometry lowering

`lib/cadkernel/acis_lower_geometry.dl` ports the bounded geometry-writing
branch of pinned `src/acis/lower.rs`. It counts pending nodes and writes dirty
planar faces, straight edges and vertices through their provenance record IDs.
It follows the topology record's geometry pointer and resolves the target by
record ID, which may differ from its array position. Other document records
and record metadata remain intact.

The pinned Rust/codec differential compares 15 output lines at O0 and O2,
including clean and dirty nodes, differing IDs and slots, changed geometry,
wrong target type, and missing source or pointer. The required fixture checks
the same cases with the native compiler.

```powershell
python tests/cadkernel/acis_lower_geometry/verify.py --source PATH_TO_PINNED_KERNEL --codec PATH_TO_PINNED_CODEC
```

This slice writes existing geometry records only. Other analytic and NURBS
forms, newly synthesized topology, full lifting, and history remain open.
