# Flex Inference Options

## Current Choice

Bring type inference for flexes back.

### `+`

- Preserves caller-scope regrouping for expressions like `discard 1 as a 64 bit integer`.
- Avoids freezing the wrong grouping behind an opaque expanded flex root.
- Works with the existing flex model where arguments remain syntax-level expressions instead of forced runtime values.
- Does not require a full parameter-mode redesign before fixing the current compiler bugs.

### `-`

- Reintroduces flex-specific inference complexity.
- Keeps the compiler split between flex and non-flex inference behavior.
- Leaves more room for binding-scope and shadowing bugs than a cleaner function-like model.
- Makes it harder to simplify the pipeline around one runtime call model.

## Alternative 1

Treat flexes as function-like boundaries with explicit parameter modes.

Needed parameter kinds:

- value
- lvalue
- type
- word/name
- compile-time constant

### `+`

- Cleaner semantic model than raw substitution.
- Caller arguments can regroup and infer before crossing the boundary.
- Removes much of the current flex-binding weirdness.
- Better long-term architecture if the language formalizes parameter modes.

### `-`

- Large redesign.
- Requires new overload-resolution rules per parameter mode.
- Requires codegen and inference agreement on which parameters are runtime vs compile-time only.
- Cannot be done cleanly by only adding lvalue parameters; type and word parameters are also required.

## Alternative 2

Keep expanded flexes opaque, but allow regrouping inside selected flex arguments before opacity takes effect.

Example intent:

- regroup inside `val` in `discard val`
- regroup inside `val`, not `var`, in `set var to val`

### `+`

- Smaller change than a full flex/function unification.
- Preserves current flex syntax and most current behavior.
- Targets the concrete failure mode where wrapper flexes seal arguments too early.

### `-`

- Adds special-case boundary logic to flex expansion/regrouping.
- Needs argument-role classification anyway.
- Still keeps flex expansion semantics partially separate from normal function semantics.
- Easier to get subtly wrong than either full flex inference or full parameter-mode unification.
