# Recursive inference and instantiated local storage

## Recursive operands

An ordinary function wrapping an unresolved recursive call was rejected before
the recursive body's later base return could establish its result type.
`resolvePatternCall` now observes recursive dependencies separately for each
argument. If all undeduced arguments arise from those dependencies, an
unsuccessful overload lookup remains deferred for the normal body reinference
pass. Implicit conversions are considered after the types become available.

Flex overloads that intentionally accept unresolved arguments still participate
in ordinary selection. Return wrappers must reach their Return intrinsic;
deferring the wrapper would incorrectly classify its caller as returning
nothing. Invalid argument inference and actual overload ambiguity still fail.

The required `recursive_operand_helper`, `recursive_operand_typed_overload`
and `recursive_operand_mutual` fixtures assert runtime results. Focused checks
also cover missing base cases, a concrete incompatible base type, unresolved
nonrecursive siblings, recursive actions, conversion ambiguity and four reduced
arc-length reproductions. All 38 O0/O2 fixture/probe combinations passed.

## Generated property constraints

Generated property patterns have a resolved receiver constraint without an
authored constraint phrase. Provisional inference previously interpreted the
absence of text as an unrestricted parameter, making unrelated classes with
the same property ambiguous. Initial and provisional resolution now retain the
resolved constraint, and signature initialization records that the parameter
is constrained. No property names are special-cased.

`provisional_generated_accessors` uses a property-reading function as a member's
type witness. It then constructs both receiver classes and checks their
distinct values through ordinary reads, writes and typed calls.

## Local storage identity

Two nested expansions can share one source declaration while owning different
live local storage. Each instantiated expression now retains its owning body,
and each body records its parent instance. Flex roots attach to their call-site
body; ordinary function roots are independent. `variableStorageKey` combines
the declaration with the instance that declares it. Globals have no local body.

Existing binding resolution returns the actual caller expression, preserving
its storage identity when passed into another expansion. Writes through that
reference update the original variable. Pure evaluation, native storage lookup
and finalized local types use the same key. Allocations no longer live on the
reusable `VariableReference`; the former allocation save/restore is removed.
Globals use an LLVM value directly rather than a stack-allocation cast.
Pure evaluation resets and restores only the current expansion's local keys,
including during early returns.

`pure_flex_captured_locals` asserts `10 10 12 12`: compile-time and runtime
results for reading and updating an outer local. The combined storage/type/
Polyline2 run passed all 70 O0/O2 checks, including nested and reentrant locals,
repeated bodies, zero iterations, early returns, dependent constraints, class
member refinements and generated accessors.

These focused results use compiler SHA-256
`5b81b92da3da254c9deb12e062272b2aa2c8eddf233cf59f9b2350b543d60817`.
They do not by themselves establish complete main-crate coverage.

## Integration regressions

The subsequent complete required run recorded 566 passes and 34 failures.
Six cases timed out; the remaining failures exposed premature recursive
grouping selection, detached operand nodes, and ambiguous component cloning.
The focused counts above are historical evidence, not a full-suite result.

Grouping selection now distinguishes a deferred recursive candidate from a
resolved candidate. A resolved alternative can establish the owning function's
return type; deferred alternatives are revisited by ordinary whole-body
reinference before their ambiguity can be decided. The existing recursive
ambiguity diagnostic fixture continues to enforce the exact warnings.

Nested grouping transactions and reuse of a cached grouping preserve the
caller's fixed roots. Type requests honor those roots, including through flex
bindings. A borrowed argument therefore cannot be regrouped behind its owner's
reference. Argument-root updates also precede failure returns so a failed
trial can roll back the complete expression graph.

Fixed roots are borrowed through parent-linked scopes. A child records only
its own resolved roots rather than copying all ancestor sets into each trial
and propagating them back on promotion. Each scope lives for the enclosing
inference call or transaction; restoration precedes its destruction.

Compiler `9ff4beef41a1cb2595e50bcf1ca8afed2cbd3238bd7512de99c7bfd13c8e63eb`
passed 36 focused grouping checks and the 38 recursive inference checks at
O0/O2. On the same Windows host, the JSON-RPC strict transport fixture compiled
in 8.25/8.58 seconds after this change, versus 44.95/45.44 seconds with copied
ancestor sets. The notification-limit fixture took 7.36/9.34 seconds, versus
27.17/27.94 seconds. These are local observations, not timing assertions;
the complete required suite must also pass before declaring integration done.

`recursive_property_operand` checks both a value-producing overload on a
member and an observable action overload. `recursive_property_predicate`
warms up a shared return pattern before evaluating nested property predicates;
the warmup reproduces the cached-grouping crash without library imports.

Component clone calls explicitly parenthesize the component being copied.
Cloning a complete tagged curve and then reading its component is also a
type-correct grouping, but is not the component-copy operation required here.

## Reusing committed line groupings

The next full required run recorded 598 passes and eight failures. The failures
included unsigned casts in loop headers and reused conditional lines, plus
legitimate spline-expression grouping warnings. This result belongs to compiler
`9ff4beef41a1cb2595e50bcf1ca8afed2cbd3238bd7512de99c7bfd13c8e63eb`.

An already-ordered expression previously inherited the caller's fixed-root
filter without registering its own child nodes. Reinferring a nested condition
could then regroup `value >= limit as word` as `(value >= limit) as word`, which
is individually type-correct but invalid as an `if` condition. The debugger
confirmed a valid selected caller grouping followed by this failure on replay.

Committed lines and cloned headers now add their full snapshot to a local fixed
scope while retaining the caller's borrowed roots. A selected trial exports only
roots belonging to the expression snapshot it can restore; unrelated callee
body nodes remain governed by their own grouping journals. An empty selection
does not introduce an empty filter that changes the meaning of ordered inference.

`loop_header_cast_grouping` and `cached_callee_grouping_replay` are import-free
runtime regressions. The latter fails on compiler
`20909aca908ce51b0020c43533bc840336433e0f9c4fadd71ca470d2051c60f0`
and produces `77` on the repaired compiler at O0/O2. Compiler
`ec4f37ae0da9455353f8825f05f69caed3b3567e529d484e6037993f8c0e85bc`
passed all 60 focused grouping, recursive-property, flex, spline and random
generator checks, plus both optimization modes of the new callee regression.
These focused results do not replace the full required run.

The subsequent full run of `scripts/test.sh` passed all 610 checks with zero
failures or skips in 799.578 seconds on compiler `ec4f37ae...8c0e85bc`.
Local evidence: `build/required-committed-grouping.log`. The callee replay and
two clip required fixtures were added after that run started and passed
separately at O0/O2 (six additional checks); they are not included in its 610.
The same compiler passed all 38 recursion checks, Dynedra Lab model/state/
hidden-window graphics checks plus both interactive builds, and 804 spline
differential cases / 56,876 Rust comparisons after the expression parentheses
were made explicit. That integration snapshot preceded the provisional-domain,
unevaluated-type and borrowed-expression fixes documented in
`cadkernel-type-query-effects.md`. On compiler
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`, the later
full required run passed 633 checks with zero failures or skips; the earlier
provisional-signature blocker is resolved. Evidence:
`build/required-area2-budget.log` and the focused results in that document.
