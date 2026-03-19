---
paths:
  - "src/compiler/**"
---

# Compiler Core Architecture

## Design Principles
- **No short-term solutions** — code must be clean and correct
- **No fallbacks** — fail hard (assert) on invalid states, don't silently default
- **Macros = code replacement** — no special "return type", just substitution
- **Only patterns that store to arguments should be macros** — other patterns use monomorphization
- **Point out inconsistencies** instead of guessing

## Build & Test
- `./scripts/build.sh` to build
- `./scripts/test.sh` to run all tests
- `--emit-llvm` to inspect generated IR, `--emit-spirv` for SPIR-V shaders
- Build lint stage (`scripts/build.sh`) must only classify clang-tidy errors from real diagnostic lines (`file:line:column: error:`), not substring matches like `parse_error::...`.

## Key Invariants
- Macro body expression nodes are **shared mutable state** — reset types before each `inferMacroBody` call (macro sections only)
- Type inference: execution-order processing (top to bottom), with per-instantiation recursive re-inference when undeduced recursive dependencies are observed.
- Instantiation argTypes vector must be built in `nodesPassed` order (both inference and codegen)
- Non-macro instantiation signatures are immutable. The map key `argTypes` is canonical, and any duplicated `Instantiation.argumentTypes` copy must remain identical to that key. Callee inference must not rewrite caller argument types.
- `macroBindingStack` (`std::stack`): macros only see their own bindings. `MacroScopeGuard` pops to caller scope for argument evaluation
- `getVariablePointer` recursively resolves through multiple macro binding scopes (for nested macros like `add value to target` → `set var to val`)
- Operator precedence uses wave-based BFS level assignment — same-wave operators get same precedence, enforcing left-to-right associativity
- **Precedence constraints (maxPrecedence, minRightPrecedence) only apply to infix operators** — patterns that consumed a left argument via `argumentChild` (detected at runtime via `match.nodesPassed[0] == rootNode->argumentChild`). Prefix patterns (like `the sine of value`) produce atomic values; their precedence should not constrain which operators can take them as operands. Without this, `the sine of a + the sine of a` fails because `+` is rejected by the sine's low precedence level.
- **Opaque regrouped subexpressions are atoms in the outer search** — once operand reordering infers a nested explicit group or any non-boundary/non-submatch expression on its own, that resulting node may appear in the parent flat-node interval only as an operand. Do not let the parent regrouping pass reopen that node as a root candidate, or explicit groups and already-inferred interior arguments leak back into the surrounding search space.
- **Operand regrouping validates one chosen subtree at a time under the trial journal** — nested opaque subexpressions expose ordered local regroupings, and rejecting one candidate must roll back the entire trial-inference scope before the next local candidate is tried. Do not mutate caller-visible type state outside that journaled scope.
- **Rejected regrouping trials are not real diagnostics** — trial inference may discover invalid local orderings, but those failures must stay in `typesValid` / `typeFailureDetail` only. Never add user-visible diagnostics from a trial candidate, and never mutate the fixed-root set while replaying the finally chosen grouping.

## Compilation Phases
1. **Import** — read source files (`compiler.cpp`: `importSourceFile`)
2. **Section Analysis** — parse indentation, identify sections (`compiler.cpp`: `analyzeSections`)
3. **Pattern Resolution** — match patterns, resolve variables, assign precedence (`patternResolution.cpp`: `resolvePatterns`)
4. **Variable Resolution** — group variables by scope, handle globals (part of `resolvePatterns`)
5. **Type Inference** — execution-order processing with operand reordering, plus per-instantiation recursive re-inference on undeduced recursive dependencies (`typeInference.cpp`: `inferTypes`)
6. **Codegen** — LLVM IR generation → native executable, .ll, or .spv (`codegen/`)

## Source File Organization

### Compiler core (`src/compiler/`)
- `compiler.cpp` — import phase, section analysis, intrinsic classifier helpers (`isArithmeticOperator`, etc.)
- `patternResolution.cpp` — pattern matching, resolution loop, precedence assignment, variable scoping
- `typeInference.cpp` — execution-order type inference with operand reordering and per-instantiation recursive re-inference, `InferenceContext` (trial mode for reordering), macro body inference, type validation

### Code generation (`src/compiler/codegen/`)
- `codegen.cpp` — expression codegen (`generateExpressionCode`), monomorphized function generation (`generateSpecializedFunction`), section codegen, main driver (`generateCode`)
- `codegenTypes.cpp` — type utilities (`getLLVMType`, `ensureType`), macro binding infrastructure (`resolveVariableBinding`, `resolveThroughMacroLayers`, `MacroScopeGuard`, `getVariablePointer`), effective type resolution (`getEffectiveType`), variable allocation
- `codegenIntrinsics.cpp` — all intrinsic code generation (`generateIntrinsicCode`): arithmetic, comparison, logical, math, pointer, control flow, return, call, cast, construct, property, shader I/O
- `codegenInternal.h` — shared declarations across codegen files (functions in codegenTypes.cpp and codegenIntrinsics.cpp that are called from codegen.cpp, and vice versa)
- `spirv.cpp` — SPIR-V shader binary emission and patching
- `native.cpp` — native executable emission via LLVM target machine

## Intrinsic Registry & Type Inference
- `intrinsicInfo.h`: central registry mapping intrinsic names → `{argCount, IntrinsicReturnKind}`. `argCount` includes the name argument (e.g. `@intrinsic("add", a, b)` → 3). Arg counts are validated at parse time in `section.cpp`; codegen and type inference do not re-check.
- `IntrinsicReturnKind` enum: `SameAsArgs`, `Bool`, `Void`, `Float`, `Custom`
- Type inference (`typeInference.cpp`, `IntrinsicCall` case) uses registry lookup + switch on return kind:
  - **SameAsArgs**: unary (1 arg) or binary (2 args) with pointer arithmetic special case for add/subtract
  - **Bool**: validates operands via `promoteArithmetic` (rejects non-numeric like Void), then `Bool` assignment
  - **Void**: direct `Void` assignment + special `store` side effects (type propagation to variables)
  - **Float**: direct `Float` assignment (e.g. shader inputs)
  - **Custom**: individual handlers for `address of`, `dereference`, `load at`, `return`, `call`, `cast`, `type`, `add pointer depth`, `construct`, `property`
- Adding a new intrinsic: add to registry in `intrinsicInfo.h` → type inference and codegen helpers (`isMathFunction`, `isComparisonOperator`, etc.) automatically pick it up

## Import Resolution Order
- Relative to importing file → CWD → `PROJECT_SOURCE_DIR/` → `PROJECT_SOURCE_DIR/lib/` → `/usr/share/dynlex/` → `/usr/share/dynlex/lib/`
- Dev paths come before system paths so debug builds use source tree libraries, not stale installed copies

## Bugs Fixed
- **Cast simplification**: Cast now always takes a TypeReference (`@intrinsic("cast", value, type_ref)`), not a string+bits. Type patterns (`@intrinsic("type", "int", bits)`) and class patterns produce TypeReferences. The old string-based path was removed. New intrinsics: `@intrinsic("type", kind[, bits])` produces a TypeReference; `@intrinsic("add pointer depth", type_ref)` wraps a TypeReference with one more pointer level. String type = `@intrinsic("type", "string")` → `{Integer, 1, ptr=1}` TypeReference.
- **Argument position ordering**: `section.cpp` processes parenthesized expressions before number literals, so `expr->arguments` had parens first then numbers regardless of text position. Pattern matcher's `sourceArgumentIndex` walks `\a` placeholders left-to-right, mapping to wrong arguments. Fix: sort `expr->arguments` by source position after collection. Note: `expandMatch` also produces non-positional order (direct args, submatches, variables, words), so codegen/inference `sortArgumentsByPosition` calls are also needed — both sorts serve different purposes.
- **Property store through macros**: `set the x of target to val` — the store destination resolves to `the x of target` (a PatternCall to the property macro), not directly to `@intrinsic("property", ...)`. Both codegen (`resolveThroughMacroLayers`) and type inference (`resolveThroughBindingsDeep`) must expand through macro PatternCalls to detect property stores and generate GEP+store / propagate field types.
- **Per-variable class instantiations (copy-on-assign)**: `ClassInstantiation::fieldTypes` is shared mutable state via `getOrCreateInstantiation`. When two variables are constructed with the same field types, they share an instantiation. Property stores (e.g., `multiply v1 by 4.5` promoting Numeric→Float) mutated the shared `fieldTypes`, contaminating all variables sharing that instantiation. Fix: on first assignment of a Class type to a variable (Undeduced→Class in the store handler), copy the instantiation so each variable gets its own independent `fieldTypes`. This runs exactly once per variable since `canRefineTo(Class→Class)` is false afterward. The old oscillation workaround in the construct handler (preferring existing compatible instantiations) was removed — with per-variable copies, the construct's instantiation is never mutated, so oscillation can't occur.
- **Class store type mismatch**: Per-variable instantiation copies can diverge from the construct's instantiation after field type promotion. E.g., construct creates `{i32,i32,i32}` (Numeric→Integer) but the variable's copy is promoted to `{f64,f64,f64}` (Float). A whole-struct `load`/`store` would reinterpret bit patterns incorrectly. Fix: store codegen (`codegenIntrinsics.cpp`) detects when source and destination class instantiations have different field type layouts and generates element-wise load+convert+store (`ensureType` per field) instead of a single struct copy.
- **Cross-scope store destination resolution**: `add value to the x of target` chains through 3 macro layers: scalar `add` macro → `set` macro → `@intrinsic("store", var, val)`. The store dest `var` resolves to `target` (in set's scope), but `target` is bound to `the x of target` in the ADD macro's scope (one level up). `resolveThroughMacroLayers` only searched the current scope. Fix: `resolveThroughMacroLayers` now pops to parent scopes when a variable isn't found in the current scope. The store handler generates the value FIRST (in the original scope), then saves/restores the full scope state (`macroExpressionBindings` + `macroBindingStack`) around destination resolution, since `resolveThroughMacroLayers` freely modifies scopes. Changed signature from `int` (scope count) to `void` (caller saves/restores instead).
- **Parameter binding mismatch with Word captures**: Type inference's PatternCall handler used `node->type == Variable` to iterate `nodesPassed`, but `expr->arguments` contains both Variable and Word captures sorted by source position. This skipped Word nodes, desyncing `argIndex`. E.g., `the {word:propertyname} of ownername` bound arguments backwards. Fix: use `node->parameterNames.find(def)` (matching codegen's approach) in all three loops: overload arg types, callBindings, and function argTypes.
- **InferenceContext and trial mode**: Replaced passing raw `ParseContext&` + `bool& validTypes` with `InferenceContext` struct. Trials for operand reordering now create a separate `InferenceContext` with `trial = true`, preventing diagnostics from leaking and ensuring `typesValid` is isolated per trial. Invalid instantiations created during trials are marked `inst.valid = false` and skipped during post-inference validation.
- **Intrinsic operand validation for Bool**: Comparison intrinsics (`less than`, `equal`, etc.) unconditionally set return type to `Bool` without validating operands. Fix: call `promoteArithmetic` on operands before setting Bool — rejects non-numeric types (e.g., Void) and correctly invalidates wrong operand groupings during reordering trials.
- **Cast validation in type inference**: Added `isSupportedCastConversion()` check during type inference. Rejects casts with Void source or unsupported type conversions (e.g., Class→Int) before reaching codegen, preventing `ensureType` assertion failures.
- **Removed `get:`/`set:` section types**: Non-macro functions now use `execute:` consistently. The `SectionType::Get` and `SectionType::Set` enum values and their string mappings in `sectionType.cpp` were removed. All lib files and tests updated from `get:` to `execute:`.
- **`currentInstantiation` moved to InferenceContext**: Removed `currentInstantiation` from `ParseContext` — it's only needed during type inference, not codegen. Now lives in `InferenceContext`.
- **Codegen uses pre-sorted arguments**: `generateExpressionCode` no longer calls `sortArgumentsByPosition` — type inference now sorts arguments in-place, so codegen can use `expr->arguments` directly.
- **Overload selection failure is an error**: When `selectOverload` returns null in both type inference and codegen, it's now treated as an error (with diagnostic) instead of silently falling back to `defs[0]`.
- **Ambiguous single-word function-vs-parameter warnings now suggest concrete pattern alternatives**: During pattern resolution, the warning walks matched function section definitions in source order (starting from the matched definition), expands parsed `Choice` alternatives from `DefinitionPatternElement` trees, and suggests the first valid alternative spelling. Multi-word alternatives get both a warning suggestion and a quick-fix edit; single-word alternatives are only suggested when they don't collide with enclosing parameter names.
- **Recursive return inference is order-robust**: Non-macro instantiation inference now records when an in-progress callee return type is still undeduced, defers only those argument deductions for the current pass, and re-infers the same instantiation in code order until resolved (or emits a non-convergence type error). This keeps hard-failure behavior for non-recursive undeduced arguments.
- **Signed numeric literals**: `section.cpp` numeric extraction now recognizes signed literals (`-1`, `-1.5`) as single number literals via the number-regex pass (`(?<![A-Za-z0-9_])(?:-)?\\d+(?:\\.\\d+)?\\b`). This removes the need for `0 - 1` workarounds in source.
- **Macro type probes must derive generic intrinsic result types without shared-section inference state**: `resolveTypeThroughBindings()` now computes types for generic intrinsics (`SameAsArgs`, `Bool`, `Void`, `Float`) directly from bound argument types. Without this, nested grouped macro expressions like `the minimum of (the maximum of ...) and 0.0` could fail during operand regrouping because recursive macro type probes hit an in-progress macro section and saw an unresolved body expression.

## TODO / Known Issues
- **Argument greediness**: `factorial of n - 1` parses as `(factorial of n) - 1`. Pattern arguments greedily consume tokens. Operator precedence (wave-based) fixes associativity but not argument boundaries for non-operator patterns.
