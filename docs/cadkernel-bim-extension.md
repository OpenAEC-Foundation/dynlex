# BIM extension after the complete cadkernel port

**Status: design proposal, 2026-09-12.** This proposes a BIM layer above the port; it accepts no new architectural decision.
The prerequisite remains the complete main-crate port: **83 source files, 39,621 production lines, 672 upstream tests**, optional modules included. The [port specification](superpowers/specs/2026-09-12-cadkernel-dynlex.md) and [coverage manifest](cadkernel-coverage.md) own delivery status; the full native port is currently incomplete.
The separate constraint solver is outside that delivery and cannot be assumed available for regeneration. ADR-0005 and ADR-0006 are accepted; ADR-0004 and the broader kernel design remain proposed. This document neither authorizes compiler fixes nor starts the repository restart.

## Working name

**Dynedra** (pronounced *die-nee-dra*) is the proposed name, with the descriptor **Geometry and BIM kernel in DynLex**.
The coined name combines *Dyn*, referring to DynLex, with *edra*, evoking geometry and polyhedra.
**Dynedra Lab** is the proposed name for the kernel's test application. DynLex remains the language and Open BIM Studio the consuming BIM application.
These are naming proposals only; package, API, file and repository names remain separate decisions.

## What the geometry foundation supplies

The following inventory describes the pinned upstream source, not completed native BIM capabilities. The main-crate source attribution and MPL-2.0 notices remain applicable to its translated files; see the coverage manifest.

| Foundation | Reusable capability after verification | Explicit boundary |
| --- | --- | --- |
| `space/`, `geom2d/` | Binary64 vectors, planes, local frames, curves, NURBS, planar intersections, offsets and triangulation | Frames do not supply units, CRS metadata, element placement hierarchies or BIM semantics. |
| `brep/geometry.rs`, `topology.rs`, `arena.rs` | Analytic/spline geometry; body, lump, shell, face, loop, coedge, edge and vertex topology; typed keys and validation | A valid body is not a building element; keys are not persistent identities. |
| `brep/make.rs`, `sweep*.rs`, `loft*.rs`, `boolean.rs`, `mass.rs` | Construction, selected modelling operations, tessellation and geometric mass properties | Operation names do not imply arbitrary-input robustness or building quantity conventions. |
| Format codec and per-node provenance | Source-record reuse and explicit modification state | Neither IFC exchange nor semantic edit history is supplied. |

New algorithms remain necessary beyond translation: general surface intersections, broader shelling/junction cases, spatial acceleration and feature-specific robustness. [Surface intersection dispatch][surface-source] explicitly returns
`Unknown` for unsupported pairs; [shelling][shell-source] recognizes boxes, circular cylinders and spheres.
Retain refusals as refusals. Do not reinterpret an unknown intersection as disjoint geometry or silently replace a failed solid operation.
The exported triangle mesh does not implement ADR-0005's editable mesh arm; implicit and point-cloud arms and explicit conversions also remain work.
**Verified port does not mean ADR-0006 conformance:** exact adaptive predicates and explicit per-entity tolerance propagation need a separate post-port audit and gate.
No complete BIM model, building analysis, drawing system, universal topology naming or full IFC coverage follows from these geometry modules.

## Proposed ownership and identity contracts

Proposed implementation baseline, to be detailed in the forthcoming [DynLex design proposal](cadkernel-dynlex-design.md): data-only records and ordinary typed patterns; frontend actions return `nothing`; calculations use noun-phrase patterns with typed results.
The library explicitly owns raw allocations and their lifetimes. No BIM-specific compiler intrinsics are proposed.

Keep geometry/numerics in the geometry library, the semantic model and evaluator in an application-domain library, IFC in an adapter, and interaction/rendering in the host. Proposed flow: command → model transaction → dependency evaluation → validated geometry revision → views/quantities/export.
An IFC-native editing graph reduces mapping boundaries but does not guarantee fidelity; a native graph alone risks losing unmapped source information. Recommend a native editing graph with retained IFC source data alongside it, accepting the added mapping and storage cost. This choice remains open.

Give occurrences a persistent `ElementId`, definitions a `TypeId`, owned semantic features a `FeatureId`, and relationships their own identities. Use persistent native entity UUIDs independent of storage slots. Retain `(DocumentId, sourceId)` plus any source GlobalId alongside the native identity; sourceId identifies a record within its document.
Duplicate GlobalIds within or across imports produce collision reports and never silently merge elements; resolving a collision requires an explicit policy.
[IfcRoot][ifc-root] supplies GlobalId to rooted entities, including relationships; resource entities do not all have such an identifier.
Saving, reloading and undo preserve semantic identity. Cloning creates new occurrence/owned-feature IDs and export GlobalIds;
sharing a type or material is explicit. Deleting and recreating an unrelated element never reuses its identity.

Upstream [`Key<T>`][arena-source] contains index, generation and type, **no arena/body identity**; its cross-arena test resolves equal keys to different values.
Cloning carries generations; a newly regenerated body can start them over. Proposed ephemeral references therefore require at least `ElementId + geometryRevision + body/arena namespace + typed key`, checked before every lookup.
Publish a fresh, non-reused geometry revision on regeneration and undo; do not restore the validity of discarded caches.
Namespace allocation, generation rollover, body cloning and reload lifetimes require independent review and executable gates before relying on this wrapper.

Persist attachments as `ElementId + FeatureId/semantic role + expected cardinality`, such as an opening on a wall's named exterior side. Resolution returns resolved, missing or ambiguous; a split may produce a set only if the attachment contract permits it.
Never select an arbitrary surviving face. Unsupported free-form attachments retain an explicit unresolved state. This follows the [semantic feature policy](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/domain/parametric-components.md); it does not solve general topological naming.
[SourceRef][provenance-source] is a u32 format-record index; `Clean/Dirty/Synthesized` records origin/modification state.
Boolean operations synthesize nodes without a complete old-to-new mapping. Add operation/feature/input-revision provenance separately; retain available ancestry with cardinalities and uncertainty, rather than inventing lineage from equal coordinates.

## Model graph, placement and evaluation

Model project/site/building/storey containment, decomposition, occurrence-to-type, material assignment, properties, hosting and joins as typed graph edges.
Keep reverse dependency indexes and validate cardinality, missing targets and acyclic containment/placement. Tiles are coordinate/storage partitions, never storeys. Types hold versioned construction rules and defaults; occurrences hold placements and explicit overrides. Properties retain value type, units, unset/unknown states and source.
Separate material definitions and ordered layers from display styles; quantities are derived records with method, units, inputs and geometry revision.
Changing a property that does not affect geometry must not rebuild bodies; changing a type invalidates only affected occurrences and dependants.

Use f64 authoring coordinates and f64 transforms between explicit element, tile and project frames; optional f32 exists only on the rendering path. This follows [ADR-0006](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/architecture/decisions/0006-f64-local-coordinates-in-tile-frames.md), overriding the older f32 authoring proposal in [spatial-model.md](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/domain/spatial-model.md).
Normalize dimensional parameters to metres/radians internally while retaining source units. IFC defaults apply unless a property/quantity has its own unit. [Project units][ifc-units]
Store CRS identity, map scale, rotation, offsets and vertical datum separately from geometry. [IfcMapConversion][ifc-map] maps engineering coordinates into a projected CRS; it is not a geodetic reprojection engine. Reject unsupported coordinate operations explicitly; unknown georeferencing must not be guessed.
Validate placement cycles and composed transforms; [IfcLocalPlacement][ifc-placement] defines relative or context-based placement and leaves cycle prevention to applications.

Represent construction as a versioned dependency DAG: type/parameters/placement → base wall → hosted cuts → permitted joins → quantities/plan/section/display. Joins need an explicit policy for ownership, priority, layer cleanup and quantity attribution; group mutually dependent walls into one evaluation operation.
Reject unsupported cycles with a path diagnostic. A general sketch/constraint solver is a separate design and licensing decision.
Cache by definition version, parameter/placement/tolerance revisions and dependency revisions; consumers must not combine products from different committed revisions. Each regeneration builds provisional bodies, validates topology and feature resolution, then atomically publishes model edges, geometry and derived results.
Failure, cancellation or resource-limit exhaustion discards provisional state. Undo/redo journals semantic changes and restores stable IDs through a fresh publication. Maintain source provenance and dirty dependency closures through undo; stale geometry may be displayed as stale but cannot be silently exported as current.

For each supported feature, audit every combinatorial decision for exact adaptive predicates, with an independent exact-arithmetic oracle. Metric tolerances stay explicit in metres/radians; propagate non-shrinking entity tolerances with recorded causes and a declared failure budget.
Use ADR defaults of 1e-6 m and 1e-9 rad as the initial policy to test, not as universal proof. Rounded constructions still require validation. Extend degeneracy tests beyond bit-identical inputs. A passing Rust differential may reproduce upstream limitations and cannot close this robustness gate.

## IFC adapter and round-trip contract

Use the official buildingSMART **IFC 4.3.2.0 / IFC4X3_ADD2** release documentation linked below, checked on 2026-09-12. It is a **proposed first export target**, not a settled schema strategy; supported import versions and exchange requirements remain decisions.
Pin the chosen schema artifact/hash and adapter version for tests. Release links use `/IFC/RELEASE/IFC4_3/HTML/`;
rolling development documentation must not silently redefine the contract.

Retain the original file, declared schema, source record graph, typed values, references and native-to-source associations. Track interpreted, retained-but-uninterpreted, edited, unsupported and rejected content independently. Source retention alone does not prove a valid rewritten file.
Rewrite only a verified dependency closure, preserving references to retained records; refuse an edit when unknown shared dependencies cannot be updated safely.
Keep an unchanged-file passthrough distinct from edited semantic round-trip. Do not promise byte identity after rewriting or infer a parametric recipe from an arbitrary imported solid. Every export reports schema, representation policies, regenerated entities, preserved opaque content, unit/CRS changes, losses and validation scope; unexpected loss fails the fixture.
Compare normalized semantic graphs and geometry, not STEP line numbers. Use an independent schema validator/reader as well as self-round-trip tests.

Map the first slice to project/site/building/storey, `IfcWall`/`IfcWallType`, placements, material layers, property sets, quantities and one hosted opening.
[Type relationships][ifc-type] and [spatial containment][ifc-containment] remain distinct; [material association][ifc-material] is separate from shape.
Validate layer direction, offset and total thickness against geometry using [IfcMaterialLayerSetUsage][ifc-layers].
[LoadBearing][ifc-wall] records intended load-carrying status; toggling it is not structural analysis or proof of capacity.
An opening connects through `IfcRelVoidsElement` and is not directly spatially contained. [Opening semantics][ifc-opening]

Propose **gross, uncut wall Body + opening Body** for newly authored walls: the exchange semantics require the cut.
Recognize **net wall Body + opening Reference** on import: the opening is already represented in the host and must not be cut again. [Opening representations][ifc-opening]
Record this policy per host representation; regenerate authored hosts from base parameters, never by subtracting again from the previous net cache.
Retained net-only imports may be re-exported unchanged, but moving/resizing their holes needs an explicit base-reconstruction contract or a refusal.
Test both encodings, repeated import/export, opening edits and dirty-cache invalidation. Malformed/unsupported mixtures produce diagnostics, not guessed extra cuts.

## First vertical experiment

Recommend one straight vertical wall with constant thickness and a rectangular profile, one rectangular through-opening, one storey,
one material-layer type, bearing/nonbearing occurrence states, one plan and section, quantities and IFC round-trip through the same committed revision.
Fix a closed geometric input class: wall spans x=[0,5], y=[0,0.3], z=[0,3] metres; opening spans x=[2,3], z=[0.5,2.5] through the full thickness. The opening rectangle lies strictly inside the wall face, separated from its perimeter by more than the declared tolerance.
Thus Vgross=L×T×H=4.5 m³, Vopening=1×0.3×2=0.6 m³, Vnet=3.9 m³; changing only L to 6 m gives Vnet=4.8 m³.
Elevation areas are 15/13 m² gross/net, then 18/16 m². Three layers of 0.05/0.20/0.05 m give net volumes 0.65/2.60/0.65 m³ initially.
These are independent rectangular-fixture calculations, not normative quantity rules for arbitrary walls.
Map only quantities whose method matches the selected [wall quantity definitions][ifc-quantities]; distinguish side area from surface area including reveals.
Exclude curved/sloping/tapered walls, exterior corners, lateral opening overlaps, perimeter-touching cuts, recesses, multiple openings and automatic joins from this first experiment.
Keep boundary and out-of-class cases as explicit refusal/rollback tests. Acceptance would establish the chain only within this declared class, not general boolean validity or BIM completeness.

## Four proposed post-port milestones

All start after the full-port gate; their criteria supplement rather than replace the 83-file/672-test obligation.
They are proposed gates, not dates or accepted changes to the existing geometry milestones.

| Milestone | In scope and dependencies | Out of scope | Measurable acceptance |
| --- | --- | --- | --- |
| B1 — Domain and robustness contracts | Complete port; identities, namespaces, units/frames, graph, transactions; predicate/tolerance audit for the wall class | Broad authoring, solver, IFC editing | Independent identity/lifetime review closes every listed gate; 10⁶ seeded random/degenerate predicate cases agree with an exact oracle; frame and rollback tests pass. |
| B2 — Regenerating wall | B1; fixed wall/opening class, materials, types, bearing flag, quantities, one plan and section, undo | General joins, free-form constraints, production drawing sheets | All numerical and mutation fixtures below pass at O0/O2; one edit updates exactly its dependency closure; no invalid or mixed-revision publication. |
| B3 — Auditable IFC exchange | B2 plus schema/profile decision, parser/writer and independent validator; retained source graph | Full schema support, arbitrary imported-solid parameter recovery, schema migration | Both opening policies round-trip with zero unexplained graph/geometry differences; unknown-record and shared-resource fixtures retain data or fail explicitly; loss report matches observed changes. |
| B4 — Controlled expansion and scale | B3; one separately specified orthogonal two-wall join class, connected evaluation groups, two-tile interaction, explicit representation dispatch | Arbitrary junctions, full mesh/implicit/cloud implementations, million-element certification | Join/quantity oracles and 100 undo/redo cycles pass; 10,000-instance edit rebuilds only its measured closure; record p50/p95 time and peak memory against budgets fixed before measurement. |

Broader walls/floors/roofs, spaces, doors/windows, analysis and complete drawing coordination need further capability gates.
ADR-0005's remaining representations retain their own algorithms, validity and conversion-loss tests; B4 does not discharge those obligations.
Runtime threading, GPU work and compiler capacity remain separate dependencies with their own evidence and authorization.

## Verification matrix for the experiment

The following are future acceptance tests, not executed results. Numerical fixtures run at both O0 and O2 with fixed inputs and recorded compiler/kernel revisions.
Proposed fixture limits: lengths/placement residuals ≤1e-8 m, areas ≤1e-8 m² and volumes ≤1e-8 m³; predicates, identities and graph relations compare exactly.
These oracle thresholds are separate from modelling tolerances; a failure is investigated rather than widening expectations after observing output.

| Check | Required assertion |
| --- | --- |
| Base/edited wall | Volumes 4.5/3.9 and 5.4/4.8 m³; areas and layer volumes match the formulas above; independent geometry integration agrees. |
| Topology and opening | Closed oriented manifold boundary including inner loops, consistent incidences, no unintended shell/self-intersection; independent volume and section confirm one hole. Any Euler check must account for genus and inner loops. |
| Identity/lifetime gate | Two arenas with equal keys cannot cross-resolve through the wrapper; deletion/reuse, generation rollover, clone, reload and full regeneration reject stale references. Independent review required. |
| Feature reference gate | Preserve semantic roles through length changes; split/delete fixtures produce explicitly permitted sets, missing or ambiguous results; never accidental rebinding. |
| Atomicity and undo | Invalid dimensions, perimeter contact, unsupported intersections and cancellation leave committed state unchanged; 100 edit/undo/redo cycles preserve IDs and reject old geometry references. |
| Cloning and type edits | Clone gets new occurrence/owned-feature IDs; shared type/material references follow policy; type edits invalidate affected instances only. |
| Units and georeferencing | Metre/millimetre encodings normalize equally; property-unit overrides survive; 500 km translation, 90° map rotation and tile reassignment preserve shape/quantity within limits. |
| Import identity collisions | Two documents with the same GlobalId remain distinct by DocumentId/sourceId and native identity; report duplicates within a file too, without silent merge or overwrite. |
| Semantic round-trip | Preserve GlobalIds, type/occurrence overrides, false versus unset, containment, hosting, material order/thickness, units and CRS through import/export/reimport. |
| Opening policy | Gross+Body and net+Reference yield one hole; repeated export and moved-opening regeneration never reuse stale cuts; opening has no direct spatial containment. |
| Retained data and failures | Unknown records/shared resources survive with valid references, or dependent editing fails; schema errors, unsupported representations and every intentional loss appear in the report. |
| Optimisation and robustness | O0/O2 classification/topology parity; near-degenerate non-identical inputs match exact predicates; no tolerance decrease or unreported growth. |
| Incremental consistency | Unrelated elements retain geometry revisions; views, quantities and export agree on the committed model revision; incomplete dependencies prevent publication/export. |

## Decisions still requiring resolution

Approve the native-model/source-retention tradeoff, persistent ID and namespace lifecycle, semantic reference cardinalities and supported imported-edit boundary.
Choose export schema/profile, import versions, independent validation tooling, quantity conventions and source retention/storage limits.
Set tolerance budgets, permitted join policies, tile ownership/rebasing rules, streaming versus extent requirements and measurable interaction/resource limits.
No acceptance claim here resolves these choices or the independent ID/handle lifetime gates.

## Source basis

Port status: the specification and coverage linked above. Upstream file references below are pinned to `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.
Architecture context: [layers](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/architecture/layers.md), [overview](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/architecture/overview.md), [ADR-0004](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/architecture/decisions/0004-build-our-own-kernel-in-dynlex.md), [ADR-0005](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/architecture/decisions/0005-multi-representation-kernel.md), [ADR-0006](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/architecture/decisions/0006-f64-local-coordinates-in-tile-frames.md), [roadmap](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/roadmap.md) and [geometry design](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/superpowers/specs/2026-09-09-geometry-kernel-design.md).
Domain context: [parametric components](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/domain/parametric-components.md), [IFC model alternatives](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/domain/ifc-data-model.md) and [spatial model](https://github.com/OpenAEC-Foundation/open-bim-studio/blob/main/docs/domain/spatial-model.md). Their proposals are not evidence of round-trip fidelity.
IFC factual statements use buildingSMART's official release pages below; architectural recommendations and fixture expectations are this proposal's own.

[arena-source]: https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/brep/arena.rs#L314
[provenance-source]: https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/brep/mod.rs#L100
[surface-source]: https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/brep/intersect.rs#L53
[shell-source]: https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/brep/shell.rs#L62
[ifc-root]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcRoot.htm
[ifc-units]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/concepts/Project_Context/Project_Units/content.html
[ifc-map]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcMapConversion.htm
[ifc-placement]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcLocalPlacement.htm
[ifc-type]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcRelDefinesByType.htm
[ifc-containment]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcRelContainedInSpatialStructure.htm
[ifc-material]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcRelAssociatesMaterial.htm
[ifc-layers]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcMaterialLayerSetUsage.htm
[ifc-wall]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/Pset_WallCommon.htm
[ifc-opening]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/IfcOpeningElement.htm
[ifc-quantities]: https://standards.buildingsmart.org/IFC/RELEASE/IFC4_3/HTML/lexical/Qto_WallBaseQuantities.htm
