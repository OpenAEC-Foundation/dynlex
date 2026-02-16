# DynLex Compiler — Core Architecture

## Design Principles
- **No short-term solutions** — code must be clean and correct
- **No fallbacks** — fail hard (assert) on invalid states, don't silently default
- **Macros = code replacement** — no special "return type", just substitution
- **Only patterns that store to arguments should be macros** — other patterns use monomorphization
- **Point out inconsistencies** instead of guessing

## Build & Test
- `./scripts/build.sh` to build
- `./scripts/run_tests.sh` to run all tests
- `--emit-llvm` to inspect generated IR, `--emit-spirv` for SPIR-V shaders

## Key Invariants
- Macro body expression nodes are **shared mutable state** — reset types before each `inferMacroBody` call (macro sections only)
- Type inference: fixed-point iteration (64 max). Types refine downward only: `Undeduced → Numeric → Integer/Float`, never back up
- Instantiation argTypes vector must be built in `nodesPassed` order (both inference and codegen)
- `macroBindingStack` (`std::stack`): macros only see their own bindings. `MacroScopeGuard` pops to caller scope for argument evaluation
- `getVariablePointer` recursively resolves through multiple macro binding scopes (for nested macros like `add value to target` → `set var to val`)
- Operator precedence uses wave-based BFS level assignment — same-wave operators get same precedence, enforcing left-to-right associativity

## Compilation Phases
1. **Import** — read source files
2. **Section Analysis** — parse indentation, identify sections
3. **Pattern Resolution** — match patterns, resolve variables, assign precedence (topological sort)
4. **Variable Resolution** — group variables by scope (function boundaries), handle globals
5. **Type Inference** — fixed-point iteration over all code lines
6. **Codegen** — LLVM IR generation → native executable, .ll, or .spv

## Intrinsic Registry & Type Inference
- `intrinsicInfo.h`: central registry mapping intrinsic names → `{argCount, IntrinsicReturnKind}`
- `IntrinsicReturnKind` enum: `SameAsArgs`, `Bool`, `Void`, `Float`, `Custom`
- Type inference (`compiler.cpp`, `IntrinsicCall` case) uses registry lookup + switch on return kind:
  - **SameAsArgs**: unary (1 arg) or binary (2 args) with pointer arithmetic special case for add/subtract
  - **Bool**: direct `Bool` assignment
  - **Void**: direct `Void` assignment + special `store` side effects (type propagation to variables)
  - **Float**: direct `Float` assignment (e.g. shader inputs)
  - **Custom**: individual handlers for `address of`, `dereference`, `load at`, `return`, `call`, `cast`, `construct`, `property`
- Adding a new intrinsic: add to registry in `intrinsicInfo.h` → type inference and codegen helpers (`isMathFunction`, `isComparisonOperator`, etc.) automatically pick it up

## TODO / Known Issues
- `promote()` doesn't check that operands are numeric before promoting
- **Argument greediness**: `factorial of n - 1` parses as `(factorial of n) - 1`. Pattern arguments greedily consume tokens. Operator precedence (wave-based) fixes associativity but not argument boundaries for non-operator patterns.
