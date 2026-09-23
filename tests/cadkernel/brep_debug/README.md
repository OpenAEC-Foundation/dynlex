# B-rep structural diagnostics

Import `lib/cadkernel/brep_debug.dl` and request `the cad debug text of value`.
The result owns its bytes; it remains valid after the input changes or is removed
from its arena. Formatting borrows all inputs and does not allocate topology.

The formatter covers provenance, the seven nodes, bodies, all ten flaws, all
eight planar curve variants, six surface variants and five edge-curve variants.
Lists and NURBS control nets preserve order. Only active tagged payloads are
printed; reference counters and backing storage addresses are excluded.

Scalars use C `snprintf` through the ordinary variadic-call intrinsic, with 17
significant digits. This preserves binary64 information, signed zero and
non-finite values. Native labels and C numeric spelling are diagnostic output,
not a byte-for-byte reproduction of Rust's derived Debug or a persistence format.

`brep_debug` checks exact body/vertex text, removal and scalar precision.
`brep_debug_variants` exercises every geometry variant and flaw, nested control
nets, default materialized weights, adjacency lists, cloning and retained text.
Both fixtures pass at O0/O2 on compiler SHA-256
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Evidence: `build/brep-debug-checks.log` and `build/brep-debug-variants.log`.

Required-suite wrappers are `cadkernel_brep_debug`,
`cadkernel_brep_debug_variants` and `cadkernel_brep_debug_geometry`.
`cadkernel_brep_topology` and `cadkernel_brep_topology_ownership` cover geometry
and ownership independently of formatting. Shared support files contain
constructors and assertions; splitting wrappers discards no checks. The combined
topology and diagnostic programs also pass O0/O2 after the split, recorded in
`build/brep-combined-check.log`. See `brep_debug_variants/README.md` for measured
compile times and the explicit, bounded integration-test budgets.
