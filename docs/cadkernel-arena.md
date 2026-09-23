# Arena: source contract, API and ownership

The source is `src/brep/arena.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The implementation is
[`lib/cadkernel/arena.dl`](../lib/cadkernel/arena.dl), licensed MPL-2.0.
This module uses the native lifecycle policy in
[`cadkernel-dynlex-design.md`](cadkernel-dynlex-design.md) and
[`stages.md`](stages.md). No compiler recognition of arena, clone or key vocabulary
is involved.

## Representation and lifetime

An arena owns its slot list, vacant-index list, and each occupied slot's separately
allocated, typed payload. A slot stores a generation and a managed payload owner.
The owner contains `storage: pointer to T` and a reference-count allocation.
A free slot contains null pointers and **no live T**. Constructing an arena,
refusing a key, removing a node, cloning a hole, and freeing an empty arena never
construct a default/zero T or invoke T's lifecycle on an absent value.

Paired retain/release hooks manage payload owners. The final release invokes
`destroy at` on the live T, then frees its storage and reference counter. DynLex
therefore invokes T's own hooks or its recursively managed fields. This supports
null-unsafe managed payloads and records with nested strings, not only scalar or
unmanaged records. A payload with raw **owned** children must wrap those children
in a correct paired lifecycle implementation. Raw pointer fields without such an
owner remain borrowed according to the language contract; the arena cannot infer
ownership from a field name.

Fresh payload and arena-header storage uses the existing checked allocation
pattern. Its initial null byte-pointer argument is the allocator ABI input; its
result is `pointer to T`. No live payload is converted to an opaque pointer or
selected by a runtime type tag. The common list-header allocator still has the
separately reported allocation-failure gap; this module does not repair stdlib.

The arena itself is returned as a raw pointer. One caller owns it and must call
`free cad arena nodes` exactly once. Ordinary copies of that pointer are aliases.
The public snapshot lists similarly require `free snapshot` exactly once.

Insertion initializes a live T using DynLex's normal value-copy/lifecycle rules.
It does not implement a Rust move or invalidate the caller's variable. Ordinary
managed copies may share their backing resource; only the explicit clone protocol
determines payload cloning. On removal, ownership of the existing payload box is
retained by the result and removed from the slot; no payload clone occurs.

`cad removal result` is a managed value. Copying or returning it retains the
payload owner, and scope cleanup releases it. Its payload survives destruction of
the originating arena. Discarding the result releases its ownership automatically.
Invalid removal results have an empty owner and need no explicit free.

`cad lookup result` and entry/value snapshots are borrowed. Their pointers do not
extend a node's lifetime, and a copied `valid` flag does not revalidate a key.
Reacquire through the key after mutation. Node addresses survive slot-list growth
in this representation, because payload allocations do not move. Removal and
arena destruction end the borrow; reuse never authorizes following an old borrow.
Replacing a live T preserves its node address but invalidates borrows into replaced
owned children. Do not retain a borrowed pointer to a temporary result value.

## Public patterns

`nodes` below is a pointer to a cad arena specialized on T. A key's `witness` is a
null `pointer to T` used only for static type matching. It never owns or accesses T.

| Pattern | Result or effect |
| --- | --- |
| `a cad arena of T` | Empty arena; also represents source `Default`. |
| `the cad count of nodes` | Number of occupied slots. |
| `nodes is cad empty` | Whether the count is zero. |
| `the cad key after inserting value into nodes` | Inserts exactly T and returns its typed key. |
| `nodes contains cad key key` | Checks range, generation and live payload. |
| `the cad lookup of key in nodes` | Borrowed result: `valid: boolean`, `value: pointer to T`; null when absent. |
| `the cad node pointer for key in nodes` | Borrowed pointer to T, or null; supplies source `get_mut` behavior. |
| `the cad value of result` | Reads T from a valid lookup/removal result using ordinary value lifecycle; aborts if invalid. |
| `the cad removal of key from nodes` | Managed result: `valid: boolean`, `payload: cad arena payload<T>`. |
| `replace cad node key in nodes with value` | Replaces a live T through lifecycle-aware storage; aborts for an invalid key. |
| `the cad keys of nodes` | Caller-owned list of keys in slot order, skipping holes. |
| `the cad entries of nodes` | Caller-owned list of `cad arena entry {key, value: pointer to T}`, skipping holes. |
| `the cad mutable values of nodes` | Caller-owned list of borrowed `pointer to T`, skipping holes. |
| `the cad clone of nodes` | New arena with independent slot/payload allocations; calls T's explicit clone protocol once per live slot. |
| `free cad arena nodes` | Releases live payload owners, both lists and arena header. |
| `the cad slot of key` | Source `Key::slot`, an unsigned 32-bit index. |
| `the cad clone of key` | Copies a key without requiring any trait of T. |
| `left equals cad key right`, `left = right`, `left != right` | Same-T key equality/inequality by index and generation. |
| `hash cad key key into state` | Sends index then generation as two u32 values to the typed hasher protocol. |
| `the cad debug text of key` | Compact source spelling `#<index>v<generation>`. |
| `the cad debug text of nodes` | Compact debug-map text, e.g. `{#0v1: 15}`; delegates payload formatting. |

This is an intentional API change from the initial arena port: lookup `.value`
is now a borrowed pointer, not a copied/default T. Use `the cad value of result`
for a checked value read. Removal uses a distinct managed result type instead of
embedding a fabricated empty T. Callers must not modify the internal slot/owner
records or their reference counts.

Removal increments generation with u32 wrapping. Vacancies are reused last-in,
first-out; cloning preserves generations, holes, vacancy order and count.
As in the literal source, equal-type keys contain **no arena identity**: a key
from another arena can resolve when its index and generation happen to match.
Different payload types are rejected at compilation. Logical list capacity uses
the existing DynLex collection limits.

## Typed payload and hasher protocols

Arena clone requires an overload with the exact input and output type T:

```dynlex
function the cad payload clone of {a domain node:original}:
    execute:
        return the independently cloned domain node of original
```

This is the translation of the source's `T: Clone` and `value.clone()`. It is not
a universal deep-copy operation: a shared-owner payload may intentionally share
its resource if that is its source Clone contract. A Vec-like owned payload must
duplicate its buffer. No catch-all record/pointer clone masks a missing adapter.
Missing adapters and adapters returning another type are compile errors, even for
an empty arena, matching the source's static bound.

The module supplies clone adapters for signed/unsigned integers, floating-point
numbers, booleans, strings and typed keys. String clone copies the bytes through
the standard substring operation. Key cloning never requires T to be cloneable.
Composite payloads supply their own adapter; their normal retain/release policy
must also be correct independently of clone.

Arena debug formatting requires `the cad payload debug text of value`, returning
a managed string with the desired source payload representation. Integer, unsigned
integer, boolean and key implementations are supplied. Other payloads provide
their own formatter; no memory-layout dump substitutes for it.

A hasher supplies this ordinary typed action:

```dynlex
function hash cad unsigned integer {a 32 bit unsigned integer:number} into {a domain hasher:state}:
    execute:
        incorporate number into state
```

The module also accepts a pointer to a u32 list as a word sink. This records the
source's ordered Hash input; it does not choose a hash algorithm, seed or numeric
hash result. Both key equality and hash ignore the type witness at runtime while
retaining static key-type checking for equality.

## Verification

The ten source tests are individually translated in
[`cadkernel_arena_upstream`](../tests/required/cadkernel_arena_upstream/main.dl),
including the source test whose foreign-key name differs from its actual
same-index/same-generation behavior. Existing arena fixtures remain, with result
reads migrated to the new accessor.

Thirteen required arena fixtures cover the ten source tests, exact key formatting
and u32 hash order, key traits without payload traits, null-unsafe resource hooks,
independent nested-list clone/mutation/release, default nested managed fields,
removal-result copies and returns, borrowed pointer intervals, invalid result
access/replacement, wrong key types, missing clone and wrong clone return type.

```powershell
python -B tests/cadkernel/verify.py --filter 'cadkernel_arena*'
python -B tests/cadkernel/arena/verify.py --source <pinned-cadkernel-checkout>
```

Both runners launch subprocesses through `unattended_child_processes`, suppressing
Windows fault dialogs without suppressing exit statuses. No compiler rebuild is
performed. The second runner compiles and runs the **unmodified** upstream module's
ten tests and compares 400 operation steps, including cloning an arena of keys,
against Rust at O0 and O2. All 1,888 output lines must match exactly.

Observed validation on 2026-09-12: all 26 required fixture/mode combinations passed
(13 fixtures at O0 and O2), with no compiler warnings. The upstream ten tests
passed; O0 and O2 each matched all 1,888 trace lines. Compiler binary SHA256:
`bb30f60001c990fb08dc3d68b2a3b8cc75a5a95cdfde4eb12c26c26ac1928857`.
The required-fixture run separately checks exact expected output, diagnostics and
deliberate failure statuses. These are arena results, not certification of the
remaining main-crate port or its validation application.
