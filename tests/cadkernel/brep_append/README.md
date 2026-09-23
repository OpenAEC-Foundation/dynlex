# Body append verification

The required `cadkernel_brep_append` fixture exercises the native key-remapping
helper used by multi-loop sweeps. It appends two independent open sheets into one
body and repeats the append, checking every arena count, roots, and full topology
validation at O0 and O2. It also checks source independence, provenance,
adjacency ownership, nested NURBS storage, and rejection of a dangling owner
without modifying the target. A final case appends 512 separate sheets with
more than two thousand linked vertices and edges, exercising the indexed key
tables used for large bodies.

This verifies body copying and key remapping; it does not establish complete
`src/brep/sweep.rs` parity.
