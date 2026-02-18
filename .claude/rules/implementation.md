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
- **Precedence constraints (maxPrecedence, minRightPrecedence) only apply to infix operators** — patterns that consumed a left argument via `argumentChild` (detected at runtime via `match.nodesPassed[0] == rootNode->argumentChild`). Prefix patterns (like `the sine of value`) produce atomic values; their precedence should not constrain which operators can take them as operands. Without this, `the sine of a + the sine of a` fails because `+` is rejected by the sine's low precedence level.

## Compilation Phases
1. **Import** — read source files (`compiler.cpp`: `importSourceFile`)
2. **Section Analysis** — parse indentation, identify sections (`compiler.cpp`: `analyzeSections`)
3. **Pattern Resolution** — match patterns, resolve variables, assign precedence (`patternResolution.cpp`: `resolvePatterns`)
4. **Variable Resolution** — group variables by scope, handle globals (part of `resolvePatterns`)
5. **Type Inference** — fixed-point iteration over all code lines (`typeInference.cpp`: `inferTypes`)
6. **Codegen** — LLVM IR generation → native executable, .ll, or .spv (`codegen/`)

## Source File Organization

### Compiler core (`src/compiler/`)
- `compiler.cpp` — import phase, section analysis, intrinsic classifier helpers (`isArithmeticOperator`, etc.)
- `patternResolution.cpp` — pattern matching, resolution loop, precedence assignment, variable scoping
- `typeInference.cpp` — fixed-point type inference, macro body inference, type defaulting/validation

### Code generation (`src/compiler/codegen/`)
- `codegen.cpp` — expression codegen (`generateExpressionCode`), monomorphized function generation (`generateSpecializedFunction`), section codegen, main driver (`generateCode`)
- `codegenTypes.cpp` — type utilities (`getLLVMType`, `ensureType`), macro binding infrastructure (`resolveMacroBinding`, `MacroScopeGuard`, `getVariablePointer`), effective type resolution (`getEffectiveType`), variable allocation
- `codegenIntrinsics.cpp` — all intrinsic code generation (`generateIntrinsicCode`): arithmetic, comparison, logical, math, pointer, control flow, return, call, cast, construct, property, shader I/O
- `codegenInternal.h` — shared declarations across codegen files (functions in codegenTypes.cpp and codegenIntrinsics.cpp that are called from codegen.cpp, and vice versa)
- `spirv.cpp` — SPIR-V shader binary emission and patching
- `native.cpp` — native executable emission via LLVM target machine

## Intrinsic Registry & Type Inference
- `intrinsicInfo.h`: central registry mapping intrinsic names → `{argCount, IntrinsicReturnKind}`. `argCount` includes the name argument (e.g. `@intrinsic("add", a, b)` → 3). Arg counts are validated at parse time in `section.cpp`; codegen and type inference do not re-check.
- `IntrinsicReturnKind` enum: `SameAsArgs`, `Bool`, `Void`, `Float`, `Custom`
- Type inference (`typeInference.cpp`, `IntrinsicCall` case) uses registry lookup + switch on return kind:
  - **SameAsArgs**: unary (1 arg) or binary (2 args) with pointer arithmetic special case for add/subtract
  - **Bool**: direct `Bool` assignment
  - **Void**: direct `Void` assignment + special `store` side effects (type propagation to variables)
  - **Float**: direct `Float` assignment (e.g. shader inputs)
  - **Custom**: individual handlers for `address of`, `dereference`, `load at`, `return`, `call`, `cast`, `construct`, `property`
- Adding a new intrinsic: add to registry in `intrinsicInfo.h` → type inference and codegen helpers (`isMathFunction`, `isComparisonOperator`, etc.) automatically pick it up

## Bugs Fixed
- **Cast macro resolution**: `value as a 32 bit float` (macro `@intrinsic("cast", value, "float", bits)`) — the `bits` parameter is a macro-bound variable reference, not a literal. Type inference and codegen must resolve cast's type string and bits arguments through macro bindings (`resolveVarThroughMacro` in compiler.cpp, `resolveMacroBinding` in codegen.cpp). Without this, sized casts default to 8 bytes.
- **Argument position ordering**: `section.cpp` processes parenthesized expressions before number literals, so `expr->arguments` had parens first then numbers regardless of text position. Pattern matcher's `sourceArgumentIndex` walks `\a` placeholders left-to-right, mapping to wrong arguments. Fix: sort `expr->arguments` by source position after collection. Note: `expandMatch` also produces non-positional order (direct args, submatches, variables, words), so codegen/inference `sortArgumentsByPosition` calls are also needed — both sorts serve different purposes.

## LSP Architecture (`src/lsp/`)

### Multi-file diagnostic tracking
- The LSP tracks an **import graph** (`importedBy`: imported URI → set of main URIs) and **cached diagnostics per main document** (`diagnosticsPerMain`: main URI → file URI → diagnostics).
- `onDidOpen`: if the file is already an import of an open main document, skip compilation (diagnostics already published). Otherwise compile as a new main document.
- `onDidChange`: recompile the file if it's a main document. Also recompile all main documents that import it (via `importedBy`).
- `onDidClose`: clean up state, re-publish merged diagnostics for affected files.
- Diagnostics are **grouped by source file URI** and published separately to each file. When multiple main documents produce diagnostics for the same file, they're merged with deduplication (same range + message = same diagnostic).
- `generateSemanticTokens` only suppresses tokens for errors **in the requested file**, not errors in imported files.

### Diagnostic related information
- Compiler `Diagnostic` has a `relatedInfo` vector (`RelatedInfo{message, range}`) for linking to related source locations.
- The LSP converts these to `DiagnosticRelatedInformation` with proper absolute URIs, rendered as clickable links in VS Code.
- Example: "Duplicate pattern definition" links to the conflicting definition.

## DAP Architecture (`src/dap/`)

The DAP (Debug Adapter Protocol) server enables VS Code debugging of DynLex programs. It embeds in the `dynlex` binary (`--dap`), compiles the `.dl` file with `-g -O0`, launches GDB as a subprocess, and translates between DAP and GDB MI protocol.

### Components

- **`gdbmi.h/cpp`** — GDB subprocess manager. Launches GDB via `fork`/`exec` with two pipes (stdin/stdout). Includes a recursive-descent MI value parser for `"string"`, `{tuple}`, `[list]` syntax. Key methods: `send()` (async), `sendAndWait()` (sync with async callback), `readRecord()`.
- **`dapProtocol.h`** — Header-only DAP type definitions with `nlohmann::json` serialization: `Source`, `Breakpoint`, `StackFrame`, `Scope`, `Variable`, `Thread`, `Capabilities`.
- **`dapServer.h/cpp`** — Main DAP server. Reuses `lsp::Transport`/`StdioTransport` for Content-Length framing (same protocol as LSP, different message format).

### Threading model
Main thread handles DAP messages from the client. A reader thread reads GDB MI output and translates async records (`*stopped`, `@"text"`, etc.) into DAP events. Mutex on `sendJson()` prevents interleaved writes.

### Request handlers
`initialize`, `launch`, `setBreakpoints`, `configurationDone`, `threads`, `stackTrace`, `scopes`, `variables`, `continue`, `next`, `stepIn`, `stepOut`, `pause`, `disconnect`.

### Launch workflow
1. Find self via `/proc/self/exe`
2. Run `<self> <file.dl> -g -O0 -o <output>` via `fork`/`exec`
3. Launch GDB on the compiled binary with `--interpreter=mi`
4. Start reader thread for async GDB output

### GDB async → DAP events
- `*stopped,reason="breakpoint-hit"` → `stopped` event (reason: "breakpoint")
- `*stopped,reason="end-stepping-range"` → `stopped` event (reason: "step")
- `*stopped,reason="exited-normally"` → `terminated` event
- `@"text"` (target stream) → `output` event (category: "stdout")

### Name demangling
Reverses `getPatternFunctionName()` from `codegenTypes.cpp`: strips type suffixes (`_i32`, `_f64`, etc.), replaces `_` back to spaces. Heuristic — works for most pattern names.

## TODO / Known Issues
- `promote()` doesn't check that operands are numeric before promoting
- **Argument greediness**: `factorial of n - 1` parses as `(factorial of n) - 1`. Pattern arguments greedily consume tokens. Operator precedence (wave-based) fixes associativity but not argument boundaries for non-operator patterns.
