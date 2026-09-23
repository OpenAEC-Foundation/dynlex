# DynLex design basis for the cadkernel port

Status: researched implementation guidance, 2026-09-12. This document records the language contract used by the port and the remaining alignment work; it does not certify a complete port.

## Evidence and scope

The compiler and language sources reviewed are pinned to `332dac1385edcbe6458386a5119b4cc62d010581`. GitHub's repository API reported that same commit for `master` on 2026-09-12. Local remote-tracking refs and cached web pages can be older; they do not override this pinned source. Recheck the design basis when updating the compiler.

The primary sources are [AGENTS.md](../AGENTS.md), [compiler stages](stages.md), [core rules](../web/wiki/core-rules.html), [function declarations](../web/wiki/sections/function.html), [named compile-time values](named_compile_time_values.md), [subjects and chaining](subject_and_chaining.md), and the implementation and required tests linked below. These are published project rules and executable evidence, not a claim that the designer has reviewed this port.

The [official site](https://dynlex.com/) describes project vocabulary as programmable phrases. This supports a domain vocabulary for geometry and building operations. The detailed API choices below are this port's application of that principle. [Future plans](future-plans.md) and [alternative inference designs](flex_inference_options.md) describe proposals; they are not evidence that a feature already exists.

The delivery remains one PR for the 83-file main crate and its 672 upstream tests, including optional modules, plus the validation application. See the [specification](superpowers/specs/2026-09-12-cadkernel-dynlex.md), [coverage](cadkernel-coverage.md), and [BIM extension proposal](cadkernel-bim-extension.md).

## How the language works

The pipeline in [`compile`](../src/cpp/compiler/compiler.cpp) imports sources, expands declaration shorthands, analyzes sections, resolves patterns, validates them, establishes target layout and infers types. Code generation then consumes the resolved/inferred program for the selected target. A later stage must not reconstruct meaning from CAD-specific words or repair an invalid earlier state.

Patterns define syntax, including control-flow phrases supplied by libraries. Matching first identifies literals and arguments without knowing runtime types. Typed constraints subsequently select valid overloads. A plain header word used in its body can become a parameter; `{type:name}` supplies an explicit constraint, while `[word]` or `{literal:word}` fixes a literal. An unused plain word is not automatically a parameter. Imports therefore affect the vocabulary available to a call.

The type system is static with inference, not dynamically typed because the surface resembles English. Functions and classes are instantiated on use. A type constraint describes an accepted domain; a concrete type supplies a runtime representation. `fixed` requires a compile-time-known value and is not a separate overload axis. Dependent constraints must resolve to pure compile-time type/constraint values. Declaration order cannot choose between overlapping overloads.

`to` is an action declaration whose inferred result must be `nothing`. `to get` declares a value function. `means:` declares a value-producing replacement. The full `function` form remains valid, including for metadata. These forms normalize into the ordinary function/replacement machinery. A replacement can act in caller scope, which is needed for assignment and section behavior; it is not interchangeable with an ordinary runtime function merely because their text looks alike.

`it` is an explicitly assigned, statically typed subject. It is not an implicit global result or a universal error slot. Incompatible branch subjects and a possibly unset subject are rejected. File-local definitions are hidden from importing callers. Ordinary internal calls and external callable wrappers have different ABI concerns.

## Rules and their application to the kernel

| Documented rule | Application to this port | Verification |
| --- | --- | --- |
| Language behavior comes from patterns and minimal intrinsics; standard libraries are written in DynLex. | Implement geometry in `.dl`; no CAD keyword recognition or geometry-specific compiler intrinsic. Reuse existing patterns when their semantics match. | Review dependency and intrinsic use; compile the library without the test application's definitions. |
| Code should read as clear, unambiguous sentences. | Choose geometric noun phrases and predicates for results; imperative phrases for actions. Use explicit typed captures and distinct argument/literal names in public patterns. | Compile representative nested expressions and combined imports; test invalid types and visibility. |
| Actions produce `nothing`; returned values must be consumed. | Prefer `the ... of ...` for a calculated value. A mutating action may publish a typed result through the documented subject mechanism where appropriate. Do not discard a public result to make an imperative-looking value function compile. | Positive and negative action/value tests; exercise the intended application call style. |
| Classes are data shapes, with operations supplied by patterns. | Use typed records for vectors, topology, result/status and evaluator state; ordinary overloaded patterns provide behavior. Translate the source's behavioral contract rather than reproducing its object/trait surface. | Check each translated source operation and refusal case, not just matching class names. |
| Constants can be zero-argument replacement functions. | Give domain statuses, sampling defaults and binding constants descriptive names; preserve the exact source values where required. Do not introduce a compiler enum facility for the port. | Compile-time-value tests and numerical boundary fixtures. |
| Generic instantiation follows usage and concrete types. | Use ordinary typed pattern protocols for curve evaluators and generic containers. State the required patterns, payload type and ownership. Avoid function-pointer erasure unless a real external callback boundary needs it. | Instantiate multiple payload/evaluator types; compile rejected combinations. |
| Raw pointers are non-owning; managed values have compiler-supported lifetime operations. | Assign an explicit owner to each raw allocation and an explicit borrowing interval to each returned pointer. Define copy, clone, removal and release behavior for nested payloads. | Allocation/destruction counters, aliasing, mutation-after-clone and pointer-invalidity tests. |
| Correctness precedes optimization; failures must be explicit; solutions should be general and deterministic. | Preserve source evaluation order, f64 arithmetic and refusal classifications. Do not hide a compiler failure by changing algorithms or accepting different expected output. Avoid duplicate algorithms added merely to bypass a failing path. | Source differential plus independent geometric oracles, O0/O2 parity, meaningful runtime inputs and measured compilation. |
| Libraries expose a small surface using `local` implementation definitions. | Keep helper solvers, allocation primitives and intermediate representations local unless another module genuinely needs them. Keep test/probe vocabulary out of production files. | Combined-import and visibility tests; inspect editor completions where the API changes. |
| The callable ABI is explicitly declared. | Isolate any host binding in `exposed function` wrappers and use `--emit-object --no-main` for a definition-only library. Normal kernel code uses ordinary DynLex patterns. | The existing object-output and callable tests are the starting point; actual kernel bindings need their own ABI tests. |

These applications are port design decisions derived from the documented rules. They are not new language requirements.

## Numerical representation and standard-library reuse

[`lib/vector.dl`](../lib/vector.dl) is not simply a float32-only library: its CPU vector members are inferred, and its constructor accepts numbers. Its selected vector type changes for the GPU target. Therefore, a separate CAD representation cannot be justified by saying that all standard vectors lose binary64 precision.

The actual CAD contract requires explicit binary64 storage, two- and three-dimensional types, specific normalization failure thresholds, non-finite classifications and source evaluation order. The current dedicated records in [`core.dl`](../lib/cadkernel/core.dl) make that contract explicit. Reuse a standard operation only after checking these semantics; record a precise reason for retaining a domain-specific operation. Keep conversion to rendering representations at the application boundary. Do not silently change modelling precision based on build target.

The standard 3D arithmetic, dot and cross implementations are concrete reuse candidates; a nominal CAD record alone does not justify maintaining duplicate formulas. Evaluate a thin domain adapter or a shared implementation with differential tests and a measured compilation cost. Retain the CAD normalization result and `1e-300` threshold: the standard normalization returns a vector and treats zero differently. There is no existing standard 2D vector in this module. Do not force two-dimensional behavior into an unrelated three-dimensional contract.

The standard library's mutating `clamp value between low and high` uses ordered comparisons, matching the port's NaN-preserving clamps. Its value-producing `value clamped between low and high` uses intrinsic min/max instead; the two are not interchangeable for all non-finite inputs. Reuse the matching pattern and consolidate repeated scalar-finiteness checks with one tested definition. Keeping short pure functions as `execute` is valid; they can still be evaluated at compile time. A blanket conversion to replacements is not a design requirement.

Ordinary arithmetic, square root, trigonometry, allocation and collection operations should use the existing library vocabulary where equivalent. A low-level construction or external-call intrinsic may be appropriate inside a library implementation; every occurrence is not automatically a defect. A missing general mathematical operation belongs behind a reusable, naturally named wrapper with a documented signature and target support. A native C math call passing a test does not establish WebAssembly or GPU availability.

Preserving upstream behavior can differ from a later robustness improvement. A passed differential may preserve an upstream degeneracy or overflow limitation. Record that separately from the post-port exact-predicate/tolerance work rather than changing source semantics without an explicit contract.

## Ownership boundaries

The decisive distinction is representation, not the apparent complexity of the pointee. [`docs/stages.md`](stages.md) and the pointer-lifecycle changes in commits [`707157e`](https://github.com/OpenAEC-Foundation/dynlex/commit/707157e) and [`3636f2d`](https://github.com/OpenAEC-Foundation/dynlex/commit/3636f2d) explicitly keep raw pointers non-owning even when they point to managed arrays. Owning arrays retain elements in forward order and release in reverse order.

[`lib/list.dl`](../lib/list.dl) allocates a list and backing storage explicitly. Its append/replace/remove/clear operations invoke element lifecycle operations, and `free` releases the backing storage and list allocation. Copying a record that contains a raw list pointer does not recursively duplicate or free that list. Managed fields and raw owned children therefore need separate treatment.

For each public kernel type, document: who allocates it, who releases it, which fields are borrowed or owned, whether ordinary copies share children, whether `clone` is deep, and which mutation invalidates returned pointers. The arena's typed generational key is a handle checked by library code, not a language-level borrow checker. Reallocation can invalidate a node pointer while the corresponding logical key remains valid.

An invalid result containing allocated lists still needs an explicit release contract until the implementation guarantees otherwise. Prefer a predictable result lifecycle; callers must not infer ownership from the `valid` flag.

Paired class `retain`/`release` sections are supported: [`lib/string.dl`](../lib/string.dl) uses them for shared storage. [`type.cpp`](../src/cpp/compiler/type.cpp) requires a complete pair, and [`managedLifecycle.cpp`](../src/cpp/compiler/codegen/managedLifecycle.cpp) invokes custom hooks instead of the default traversal of embedded managed fields. An owning wrapper must account for every owned child and ordinary copy; adding only a freeing hook is insufficient. Reference-counted lifetime is not a general deep clone. Internal calls can pass addressable arguments by reference, so callers must not assume that a function call makes an independent copy.

At raw-storage boundaries, `initialize at` initializes fresh storage; `store at` replaces an existing value with lifecycle handling; `destroy at` releases a stored managed value without freeing the allocation or clearing the stored bytes. Do not manually destroy a managed local that will still receive automatic scope cleanup. See [`codegenIntrinsics.cpp`](../src/cpp/compiler/codegen/codegenIntrinsics.cpp).

## Application and BIM vocabulary

The validation application must call the same kernel patterns as the tests. UI callbacks and draw code should describe actions; geometry and quantities should be consumed as typed values. Memory layout, ABI arguments and raw intrinsics belong behind library interfaces, not in the application controls.

The BIM proposal follows the same boundary: element, placement, material and transaction data are records; operations are typed patterns. Regeneration and validation are ordinary library algorithms. Nothing in the researched language contract calls for new compiler semantics for walls, openings, IFC or model transactions. Keep exact numerical data and stable identity independent of render state.

## Language-contract verification

On 2026-09-12, the already-built compiler at the pinned revision passed all 24 fixture/mode combinations below, at both `-O0` and `-O2`. Existing `main.dl`, `expected.txt` and expected diagnostics were unchanged. The existing `verify_fixture` function in [`tests/cadkernel/verify.py`](../tests/cadkernel/verify.py) was invoked directly for this explicit selection; no new expectations or runner semantics were introduced.

| Existing required fixture | Contract exercised |
| --- | --- |
| `declaration_shorthands` | Actions, value declarations and replacements normalize correctly. |
| `named_compile_time_values` | Named replacement values satisfy compile-time constraints. |
| `subject_chaining`, `subject_unset` | Explicit typed subject propagation and rejection before assignment. |
| `local_functions`, `local_function_hidden` | File-local helper resolution and import visibility. |
| `dependent_multi_word_parameter_constraint` | A typed signature depends on a fixed parameter. |
| `custom_patterns` | Vocabulary and primitive wrappers can be supplied without std imports. |
| `managed_array_pointer_lifecycle`, `managed_array_pointer_minimal` | Raw pointers to managed aggregates remain non-owning. |
| `action_shorthand_return_value` | An action declaration cannot return a value. |
| `fixed_constraint_requires_compile_time` | Runtime values cannot satisfy a fixed-value requirement. |

All eight successful-program fixtures matched their expected runtime output; the four rejection fixtures matched expected diagnostics without a binary. Individual compilation times were 0.031–0.500 seconds on this host. These small fixtures are language evidence, not a whole-kernel compilation benchmark, a full compiler-suite run or proof of full-port correctness.

The [recursive-action correction](../tests/cadkernel/compiler-regressions/README.md) establishes the implicit result through ordinary inference state and return intrinsics, including rollback. The production tessellation algorithm now passes O0/O2. Independent review passed. Arena integration and the Windows auxiliary test harness have since passed their checks; the complete compiler suite is being rerun with all compiler corrections. A further [pure-evaluation correction](cadkernel-pure-evaluation.md) bounds recursive execution and prevents incomplete calculations from becoming constants.

The sampling contract now uses fixed binary64 arrays with matching dimensions.
Its differential runner passes 372 cases and 827,664 Rust comparisons; eight
compile-rejection checks cover precision, dimension and 3D callback types.
The angle value pattern is now `the cad normalized angle of ...`.

The arena representation now stores live payloads in separately allocated managed
boxes. Empty slots contain no live payload, explicit clone invokes the typed
payload protocol, and node addresses survive slot-list growth. Removal and
arena destruction still end a borrow. The corrected implementation passes all
ten source tests, managed payload/lifetime fixtures and 1,888 Rust trace lines
at each optimization level; see [the arena contract](cadkernel-arena.md).

Helix construction now returns the shared managed NURBS3 result. Ordinary
copies retain its storage, explicit clone remains deep, and rejected results
need no manual release. The duplicate helix-specific NURBS representation and
freeing patterns have been removed. The two source tests, typed consumers,
copy/list/reset lifetime checks, six refusal cases and 341 differential cases
(162,520 Rust comparisons) pass at O0/O2; see
[the helix checks](../tests/cadkernel/helix/README.md).

## Remaining alignment work

The following findings come from source review, with the precision finding additionally reproduced as described below. They are open implementation/verification obligations.

| Finding | Evidence and required closure |
| --- | --- |
| Reusable arithmetic and utility behavior is duplicated. | Apply the standard-vector, ordered-clamp and scalar-finiteness review above. Keep justified numerical differences explicit. Missing `atan2`, `acos`, `hypot` and `asinh` wrappers are not evidence that external calls are prohibited; consolidate their bindings where useful and retain their numerical contract. |
| General allocation failure is not fully covered. | [`lib/array.dl`](../lib/array.dl) allocates a list-header slot with `calloc` and initializes it without checking null. A later kernel buffer check does not cover this earlier failure. Handle any required repair in the common allocation contract with controlled failure tests, not a CAD-specific compiler path. |

Before the sampling-contract correction, the following diagnostic program reproduced accepted wrong-precision input at both O0 and O2. It printed `false` then `true`: the same direction gave a nonzero angle with f32 storage and zero with f64 storage. The corrected contract rejects both arbitrary lists below; supported coordinate containers are matching fixed binary64 arrays or the typed CAD vectors.

```dynlex
import lib/cadkernel/tessellation.dl
import lib/cadkernel/helix.dl

set narrow to an empty list of a 32 bit floating-point number
append (1e20 as a 32 bit floating-point number) to narrow
set wide to an empty list of a 64 bit floating-point number
append (1e20 as a 64 bit floating-point number) to wide
print whether ((the cad direction angle between narrow and narrow) = 0.0) as a line
print whether ((the cad direction angle between wide and wide) = 0.0) as a line
free narrow
free wide
```

The Helix import supplies the actual evaluator patterns referenced by the sampling module. Without an implementation of that documented protocol, its function bodies cannot resolve those pattern references even if adaptive sampling is not invoked. Separate pattern availability from on-use type instantiation when designing import boundaries; do not add dummy implementations to satisfy the matcher. The historical diagnostic invokes the angle calculation only; adaptive sampling is covered separately by its full differential runner.

The numerical coverage manifest and language/API review have different meanings: a mathematically verified module can still need API or ownership corrections. All material findings must be closed before the complete-port PR is presented as ready. Record the final corrected behavior and validation in the PR, with any required departure from standard-library behavior justified against the numerical or ownership contract.
