# Spatial module composition

`lib/cadkernel/space.dl` is the import surface corresponding to all declarations
and reexports in pinned `src/space/mod.rs`. Native patterns retain their `cad`
prefix and individual modules remain directly importable. The import includes
the optional planar functions and both source polygon measurement/meshing parts.

This integration fixture combines endpoint changes, source-directed NURBS
construction, adaptive approximation, arclength queries, Bezier sampling,
line union, alignment, helix conversion and polygon triangulation. It catches
pattern or lifecycle conflicts that isolated imports cannot establish.
Both O0 and O2 pass (`build/space-fixtures.log`). Individual algorithm modules
retain their own source and differential verification; this smoke test is not
a replacement for them. The complete kernel and application remain in progress.
