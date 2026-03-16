# Regression Findings (2026-03-13)

## Context
During macro/type-inference and codegen alignment work, regressions appeared in `test.sh` (notably: `array_literal`, `import`, `pattern`, `section`, `specificity`, `syntax_config`).

## Confirmed causes
1. Codegen-side macro overload reselection can pick a different parse shape than inference-selected ordering, causing invalid groupings to survive (example path: vector construction expression regrouped into class `*` float).
2. Macro call parameter binding can lose promoted `VariableLike` parameters unless bindings are reconstructed from definition pattern elements.
3. Macro codegen scope handling diverged from inference when bindings were cleared instead of merged; this can drop caller-visible names in nested macro expansions.
4. Arithmetic inference accepted unresolved promotion results (`promoteArithmetic` returned unresolved) and allowed bad states to pass forward.
5. Property/type recomputation in codegen relied too much on cached node types in macro-shared AST paths; some property reads required dynamic recomputation from owner type/instantiation.

## Guardrails for patching
- Keep inference-selected overload/order authoritative; only reselect when selection is genuinely missing.
- Recompute macro types from current bindings, but do not bypass inference consistency.
- Fail hard on unresolved arithmetic/property states during inference rather than at late codegen.
- Keep fixes minimal and local to binding/selection consistency paths.
