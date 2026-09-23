# cadkernel Port Implementation Plan

**Goal:** A verified native DynLex port, an interactive test application and a reviewable pull request.
**Architecture:** Preserve the upstream behavioral and dependency boundaries through idiomatic DynLex patterns, collections and typed records. Keep reference fixtures, numerical checks and visualization separate from the library.
**Tech Stack:** DynLex, pinned Rust reference, Python test orchestration, existing DynLex graphics runtime.
**Spec:** ../specs/2026-09-12-cadkernel-dynlex.md

## Global constraints
- Upstream source: 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
- Base compiler: 332dac1385edcbe6458386a5119b4cc62d010581; compile native test programs with -O2.
- Derived files preserve MPL-2.0 attribution.
- Preserve explicit failure behavior and binary64 modelling arithmetic.
- Report incomplete coverage accurately and preserve unrelated working-tree changes.
- Apply the researched [DynLex design basis](../../cadkernel-dynlex-design.md); distinguish documented language rules from port-specific decisions and future proposals.

## 1. Foundation and language compatibility
Files: lib/cadkernel/core.dl; tests/required/cadkernel_core/main.dl and expected.txt.
- [x] Translate vector/frame mathematical fixtures before implementation.
- [x] Confirm the new fixture fails against the missing library.
- [x] Port Vec2 and Vec3 arithmetic and normalization refusal using typed result records.
- [x] Run the fixture; compare with independent exact values and the Rust tests.
- [x] Research the published language rules, compiler stages and standard-library conventions; verify the selected action/value, generic, visibility and lifecycle language contracts at O0/O2.
- [ ] Close the design audit's public API, standard-library reuse and nested ownership findings before accepting the complete port.

## 2. Geometry and topology modules
Files: lib/cadkernel/geom2d/*.dl, lib/cadkernel/space/*.dl, lib/cadkernel/brep/*.dl, corresponding tests/required/cadkernel_* fixtures.
- [x] Inventory every upstream module and its dependencies in a source coverage manifest.
- [ ] Port in dependency order: numeric helpers, curves, intersections, planar operations, topology arenas, constructors, tessellation, mass properties and solid operations.
- [ ] Exercise each module's normal and refusal cases against translated upstream tests before integrating it.
- [ ] Account explicitly for codec, offset and triangulation dependencies.

## 3. Test application and differential checks
Files: examples/dynedra/*.dl; tests/cadkernel/lab_*; tests/cadkernel/reference/; tests/cadkernel/verify.py.
- [x] Draw the initial native curve/surface examples and expose operation/parameter controls.
- [ ] Extend the application through the remaining planar and B-rep operations.
- [ ] Compare deterministic fixtures with a pinned Rust reference and independently calculated expected volumes and bounds.
- [ ] Check operation failures, topology invariants and large-coordinate behavior.
- [ ] Verify the application starts and renders the geometry it computes.

## 4. Verification and pull request
Files: docs/cadkernel-port.md; lib/cadkernel/README.md and source/license notice.
- [ ] Record commands, versions, test outcomes, timings and remaining gaps.
- [ ] Review changes and run the applicable repository tests.
- [ ] Commit on codex/cadkernel-port, push and create the requested pull request with accurate coverage.

## Current verification state

The repaired compiler passes the complete required suite: 633 passes, zero
failures and zero skips (`build/required-area2-budget.log`). An additional 58
fixture-mode checks pass separately; these are not a second full-suite run.
The compiler has not changed since that completed suite. Regression coverage
includes recursive inference, pure binding/evaluation, lifecycle scope and
ownership, bounded compile-time execution, and Windows test harness handling.

Dynedra Lab passes model/state checks and six drawable hidden-window scenarios
at O0/O2: rational arc, Bezier curve, helix, NURBS surface, planar faces and
variable-width bands. Both interactive builds compile. Pixel inspection is
not included in this smoke test. The complete main kernel, remaining B-rep
application scenarios, final review and pull request are still pending.
