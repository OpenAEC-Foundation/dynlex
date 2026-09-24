# Open multi-section general loft B-rep differential

`verify.py` compares the public B-rep route for two through eight single-wire open profiles
against pinned `src/brep/loft_general.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It checks the source
revision and source-file hash, then runs O0 and O2 for both implementations.

The comparison includes success/refusal, all nine arena counts, roots, flaws,
vertex coordinates, edge connectivity and sampled geometry, coedge senses and
pcurves, loop/face/shell/lump adjacency keys, and surface samples. Both implementations insert
vertices and edges in the same order, so no coordinate normalization is used.
The required fixture also checks an interior edge's two coedges, cyclic
closure, and ownership after copying and replacing the original body. The
differential includes sections that collapse to one vertex at either end. The
verified run covers 37 cases and 15,908 fields across O0/O2.

Run with `python verify.py --source <pinned-source-root>`. An existing verified
Rust target can be passed with `--reference-root <target-dir>`.

Supported: two or more planar, single-wire, open profile sections; cyclic
lofts require at least three. Both periodic and nonperiodic seams are covered.
Profile-only lofts without guides or path support direction alignment and
normal modes 0–6 including draft angles and magnitudes. This entry point
produces an open sheet. Point sections, holes, closed profiles/solids, guides,
path, and circular analytic specialization remain outside this slice.
