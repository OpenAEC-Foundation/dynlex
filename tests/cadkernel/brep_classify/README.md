# Native B-rep solid containment

`lib/cadkernel/brep_classify.dl` ports the complete production implementation
from `src/brep/classify.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It classifies a point as inside,
outside, on the boundary or unknown by testing face distance first and then
counting unambiguous ray crossings along four unrelated directions.

The implementation borrows its body. Face-key snapshots, parameter-space
boundaries and ray-hit lists are temporary owned values. A cast is rejected
when a surface or pcurve cannot answer, a hit lies at the origin, or two faces
are reached at the same distance. Unsupported surfaces therefore return
`the unknown cad containment`; they are never silently treated as outside.

The native fixture translates all fourteen source test groups, including box
and cylinder containment, boundary priority, survey coordinates, unsupported
surface behavior, analytic ray hits and parameter inversion. It also checks
that a point on the extension of a bounded box edge remains outside. Run it at O0/O2
through the required fixture:

```powershell
python -B tests/cadkernel/verify.py --filter cadkernel_brep_classify --compile-timeout 120
```

The differential runner compares the public containment result for fixed and
deterministic randomized cuboids, cylinders, spheres, cones, pyramids and
frustums against independently linked pinned Rust libraries:

```powershell
python -B tests/cadkernel/brep_classify/verify.py --source C:/path/to/pinned/cadkernel
```

It compiles both implementations at O0 and O2, requires exact classification
and optimization parity, records source/compiler hashes, and uses the shared
guarded process runner for every executable.

Verified on 2026-09-16 with compiler SHA-256
`8ed4ee0e491a9adb5dcec892c09ca158484ae98405f4700065800e1d5650c59d`.
The required fixture passed at O0 and O2. The differential passed all 248
cases and 992 exact comparisons, with exact optimization parity in both
implementations. The generated evidence is written to
`build/brep-classify-checks/summary.json`.
