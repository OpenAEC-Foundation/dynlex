# Expression Compile-Time Value Plan

## Goal

Make type inference infer each expression exactly once per active configuration and store both:

- the expression type
- the expression's compile-time value when it is known

Later consumers must read that stored state instead of re-evaluating or re-inferring the same subtree.

## Why

The current compiler still recomputes compile-time values from expression trees after inference. That causes three problems:

- it lets later code observe a subtree before the active grouping/inference state has fully settled
- it encourages probe-style helper paths that re-enter inference from the side
- it duplicates logic between type inference, compile-time type resolution, constant evaluation, and codegen

The intended model is simpler:

1. Infer the active tree top-down, left-to-right.
2. While inferring a node, request its child expressions in that same active tree.
3. When a node's operands are known enough, compute and store:
   - `expr->type`
   - the node's compile-time value if it is compile-time-known
4. Later phases and parent nodes just read the stored result.

## Core Rules

- No probing for normal child inference.
- No re-inferring the same expression in the same configuration.
- No recomputing compile-time values for already inferred expressions.
- Compile-time consumers such as `select`, `construct`, `size of`, `if`, and codegen must read stored expression values.
- If a value is not stored as compile-time-known, the expression is not compile-time-known in that configuration.

## Configuration Boundary

Stored expression results are only valid for one active configuration:

- one operand grouping
- one instantiation key
- one known-constant state
- one trial / real pass

When any of those change, expression types and stored compile-time values must be cleared together.

## Required Data Model Change

`Expression` should store a compile-time value alongside `type`.

That stored value must:

- represent "unknown" cleanly
- support the existing `CompileTimeValue` kinds
- be reset whenever expression types are reset

## Intended Inference Flow

For one fixed configuration:

1. Request the root expression.
2. If it is already inferred, return its stored type/value.
3. Otherwise infer it.
4. During inference, request its children in the node's real inference order.
5. Compute the node type from child results.
6. If the node is compile-time-known, compute and store its compile-time value immediately.

Examples:

- `+`: request left, request right, compute result type, and if both operands are compile-time-known, store the summed value on the `+` expression
- `store`: request the value expression, use that type/value to refine the destination state, then store `Void` on the `store` expression
- `select`: request condition, true branch, false branch like any other operator; if the condition value is compile-time-known, store the selected branch value, otherwise leave the `select` value unknown

## Consequences

- `resolveCompileTimeTypeReference(...)` should read stored compile-time values of type expressions instead of trying to reconstruct them from partially inferred trees.
- Compile-time readers should consume already inferred expression state in normal compiler flow, not rebuild values from syntax trees a second time.
- Codegen should read stored compile-time values for folding decisions instead of re-evaluating expression trees.
- Trial inference should still own rollback of variable/instantiation state, but expression-local type/value computation should remain single-pass inside that trial.

## Migration Plan

1. Add stored compile-time value state to `Expression`.
2. Reset it everywhere types are reset.
3. Update intrinsic and pattern-call inference to compute/store values during real inference.
4. Replace later compile-time consumers with reads of stored expression values.
5. Remove redundant re-evaluation paths once all active users have moved over.

## Non-Goals

- No workaround paths for specific intrinsics.
- No reparsing expression text.
- No hidden fallback to speculative child re-inference.
- No separate "maybe infer" helper layer for ordinary child access.

## Invariant To Preserve

An expression that has been inferred in the current configuration is the single source of truth for both its type and its compile-time value.
