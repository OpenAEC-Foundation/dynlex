# Owned topology foundation

Source: `src/brep/topology.rs` and the provenance definitions and tests in
`src/brep/mod.rs`, revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
The entire two files, including content after test declarations, were reviewed.
`topology.rs` has no original tests; `mod.rs` has four.

The implementation is in [`brep_topology.dl`](../../../lib/cadkernel/brep_topology.dl),
imported by [`brep.dl`](../../../lib/cadkernel/brep.dl). This is the topology and
provenance foundation. The module declarations and reexports in Rust `mod.rs`
refer to additional geometry, editing, construction and format operations;
those operations are not implemented by this foundation import.

## Dependency and interface contract

| Dependency | Contract used here |
| --- | --- |
| `arena.dl` | Existing `cad arena<T>`, typed `cad key<T>`, generation checks, borrowed lookup pointers, managed removal results and explicit payload clone protocol. |
| `brep_geometry.dl` | `cad surface`: plane, cylinder, cone, sphere, torus, NURBS surface; `cad edge curve3`: line, circle, ellipse, planar NURBS, spatial NURBS. Geometry evaluation, equality and deep clone remain in that module. |
| `curve2.dl` | The full native eight-variant `cad curve2` is the payload of `cad pcurve result`. Its managed payloads participate in retain/release and explicit deep clone. |
| Provenance | `cad source reference` is exactly a u32 record index. `cad provenance` distinguishes synthesized, clean and dirty values while preserving the original reference when dirtied. No record parser, serializer or pcurve-building algorithm is implied. |

All nine public key types are nominal records with u32 `index` and `generation`:
`cad vertex key`, `cad edge key`, `cad coedge key`, `cad loop key`, `cad face key`,
`cad shell key`, `cad lump key`, `cad surface key`, `cad curve key`.
Their private adapters construct the corresponding typed arena key; the public
body API does not accept a different node kind accidentally. Keys contain no
body identity, as in the source. Same-kind keys with the same slot and generation
can therefore resolve in another body.

Node field names follow the source, using `startParameter` / `endParameter` for
edge parameters and vector3 `point` values for vertices. Each adjacency vector is
a `cad topology list`; its `values` field exposes a mutable typed list while its
owner is alive. Geometry's own list views remain read-only under their modules'
contracts. Do not free any borrowed view or modify reference counters.

`an empty cad body` creates all nine arenas and the root list. Ordinary copies
retain shared storage. `the cad clone of body` independently clones every live
payload, adjacency list, root list, arena generation and vacancy sequence.
`the cad clone of node` implements the seven source node Clone contracts.

The common access patterns are:

```text
the cad body key after inserting value into body
the cad lookup of key in body
the cad node pointer for key in body
the cad removal of key from body
replace cad node key in body with value
body contains cad key key
```

Insertion is overloaded on all seven node payload types, surfaces and curves.
Lookup returns the existing arena result with `valid` and a borrowed `value`
pointer. `the cad value of result` checks validity before reading. Removal returns
the arena's managed owning result; copies retain its payload. A borrowed node
pointer must not outlive removal/replacement or destruction of its body storage.
Use a lookup result for uncertain keys; direct pointer lookup aborts on failure.

Body arena fields are typed raw arena views owned by `storage`. Direct arena
iteration yields the arena's typed keys, while body query snapshots return the
nominal public keys. Every returned raw snapshot/flaw list is caller-owned and
must be freed exactly once.

## Full source operation map

| Rust operation or data | Native implementation |
| --- | --- |
| `SourceRef::new`, `index`, Copy/Clone, equality, Hash | `a cad source reference for index`; `the cad index of reference`; ordinary copy / `the cad clone of reference`; equality; `hash cad source reference reference into state` emits one u32. |
| `Provenance::{Clean,Dirty,Synthesized}` | `the clean cad provenance of reference`, `the dirty cad provenance of reference`, `the synthesized cad provenance`. |
| `Provenance::source`, `is_reusable`, `soil` | `the cad source of value`, `cad value is reusable`, `soil cad provenance value`; source option distinguishes absence from index zero. |
| `Provenance` Copy/Clone/equality | Ordinary copy, explicit clone and variant-aware equality. |
| Nine `Key<T>` aliases | Nine nominal key records, typed arena adapters, slot, equality/inequality, clone, ordered u32 hashing and exact `#slotvgeneration` debug text. |
| `Vertex`, `Edge`, `Coedge`, `Loop`, `Face`, `Shell`, `Lump` | Seven typed node records, constructors, complete field equality and explicit deep clone. |
| `Coedge::pcurve` | Native `cad pcurve result`, actual curve2 payload, optional equality and deep clone. |
| `Body::new`, `Default` | `an empty cad body`, `the cad default body`; synthesized provenance. |
| `Body::clone` | `the cad clone of body`; clones all nine arenas and roots. |
| `Body::partner` | `the cad partner of coedgeKey in body`; succeeds only for a two-entry edge use list containing that key. |
| `Body::coedge_vertices` | `the cad coedge vertices of key in body`; respects coedge direction. |
| `Body::face_keys`, `edge_keys` | `the cad face keys of body`, `the cad edge keys of body`; live keys in arena slot order. |
| `Body::face_coedges` | `the cad face coedges of faceKey in body`; loop order preserved, dead loops skipped. |
| `Body::edge_endpoints` | `the cad edge endpoints of edgeKey in body`; delegates to the actual curve at both edge parameters. |
| `Body::soil_vertex` | `soil cad vertex key in body`; soils incident edges even when the supplied vertex key is already dead. |
| `Body::soil_edge` | `soil cad edge key in body`; soils the edge, its live coedges, their live loops and those live faces. |
| `Body::validate` | `the cad flaws of body`; lump, shell, face, loop and edge checks preserve source order and exact messages. |
| `Body::worst_vertex_gap` | `the cad worst vertex gap of body`; f64 distances, maximum starts at positive zero, NaNs do not replace the maximum. |
| `Body::euler_characteristic` | `the cad euler characteristic of body`; signed 64-bit V − E + F. |
| Ten `Flaw` variants, equality | `cad topology flaw`, named kind constants 0–9, active-message/key equality. |

Import `lib/cadkernel/brep_debug.dl` for owned diagnostic strings of complete
nodes, bodies, provenance, flaws and active geometry payloads. It recursively
prints every source field, including nested NURBS controls, knots and weights,
and uses 17 significant digits for binary64 values. Keys retain exact
`#slotvgeneration` spelling. Other diagnostic labels use native field names;
Rust's derived Debug typography is not a serialization contract. Reference
counters, inactive tagged payloads and storage pointers are not geometry fields.

The validator intentionally preserves several source decisions:

- A lump's shell list is checked for live keys, but shell-to-lump ownership and
  body roots are not checked.
- Sphere and torus faces may have no loops. A NURBS face may have no loops only
  when both periodicity flags are true.
- An empty loop is singular on a sphere, on a cone with `abs(tan(angle)) > 1e-15`,
  or on a torus with `abs(minor) > EPSILON` and `abs(major) <= abs(minor)`.
- One coedge is a closed loop only when its edge has identical endpoint keys.
  Degenerate loops skip later ownership/join checks. Each other loop reports at
  most its first join gap.
- Oppositely directed uses owned by the same loop form greedy seam pairs in
  list order. Only unpaired uses contribute to non-manifold and same-sided tests.
- A partner key need not be live. Coedge endpoint keys need not name live
  vertices. Geometric endpoint gaps are measured separately from validation.
- Soiling does not propagate into shell, lump or body provenance.

## Reproduce verification

```powershell
python -B tests/cadkernel/brep_topology/verify.py --source <pinned-checkout> --rustc <rustc.exe> --dependencies <compatible-cargo-debug-deps>
```

The dependency directory must contain compatible `spade` and `rustc_hash` rlibs
and their transitive dependencies. It can be produced by building the pinned
crate with `cargo build --manifest-path <checkout>/Cargo.toml --features brep`
using the same Rust toolchain and a separate target directory. The current
default is `build/cadkernel-upstream-tests/debug/deps`; specify another path if
more than one matching rlib exists. The runner compiles the complete original
`src/lib.rs`, not replacement Rust implementations or shortened modules. It
verifies the pinned revision and rejects tracked source changes.

`--rust-only` runs the source tests and reference graphs and records
`native_verified: false`. It cannot establish native success.

Files:

- `provenance.dl` translates every assertion of the four original tests and
  adds synthesized-soil and u32 maximum checks.
- `ownership.dl` tests node equality, independent adjacency clone, actual NURBS
  pcurve/curve/surface/planar payload ownership and retained removal results.
- `main.dl` adds empty-body behavior, geometric gap measurement, clone mutation,
  vertex removal and stale-key rejection. All nine nominal key types are checked for clone, slot width, exact debug spelling and ordered u32 hash input.
- `reference.rs` and `probe.dl` build matching graphs through public APIs and
  apply 60 structural scenarios, with additional extreme numeric cases.
- `verify.py` compares every source trace against O0 and O2, and then compares
  the O0/O2 numeric values exactly. Structural values, order, keys, flags and
  messages must match exactly. Binary64 values use 17-digit output and relative
  tolerance `3e-13` only for nonzero finite Rust/native differences; zeros,
  signed zero, infinities and NaN classification are checked separately.

Every subprocess uses the shared guarded runner, preserving exit statuses and
suppressing unattended Windows crash dialogs. The runner does not rebuild the
compiler, edit expected results, or register shared required tests.

Verified native results: four unchanged source tests, 210 graph cases and
19,372 trace lines agree at O0/O2, covering all ten flaw variants and twelve
exact messages. Optimization levels agree exactly. Compiler SHA-256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Report: `build/cadkernel-brep-topology-checks/run-jkiiu9qe/results.json`.
On Windows, the reference used MSVC rustc 1.91.1 and matching `spade` 2.15.1
and `rustc-hash` 2.1.3 libraries. The numeric parser accepts C NaN payload text
such as `-nan(ind)` without weakening finite comparisons or signed-zero checks.

## Compiler regression fixture

`member_type_repro.dl` is import-free. A class member uses `type of` a function
which reads a generated property. Adding a function with that class as a typed
parameter triggers provisional constraint inference. Two unrelated classes
with the same field name incorrectly become ambiguous.

`patternResolution.cpp::createClassPropertyPatternDefinition` sets the generated
receiver's `resolvedTypeConstraint` directly and leaves `typeConstraintName`
empty. Provisional signature inference now preserves that receiver domain and
the selected-constraint bookkeeping. Disjoint unresolved overloads can be
excluded by known domains; still-matching incomplete candidates remain deferred.
See `docs/cadkernel-type-query-effects.md` for the provisional-domain,
unevaluated type-query and borrowed-binding regression coverage.

The repro only infers the sample's type; it never dereferences its null pointer
at runtime. Removing the typed `consume` declaration is the passing control.
