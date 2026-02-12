# DynLex Compiler Memory

## Type Inference System (implemented)
- Static type inference, no annotations
- Numeric literals (`int64_t`) → `Numeric` type (not Integer, not Float)
- Float literals (`double`, e.g. `0.5`) → `Float`
- String literals → `String`
- `Numeric` adapts to context: `Numeric + Float → Float`, `Numeric + Integer → Integer`, `Numeric + Numeric → Numeric`
- After fixed-point iteration, remaining `Numeric` defaults to `Integer(4)` (i32); literals > INT32_MAX → `Integer(8)`
- Macro expressions: type = replacement body expression type (macros are code replacement, no "return type" concept)
- Macro effects (like `set var to val`): trace `@intrinsic("store")` through bindings to propagate types to variables
- Variable types refine downward: `Undeduced → Numeric → Integer/Float`, never back up
- Non-macro functions: monomorphized per argument type combination
- Key files: `type.h`, `type.cpp`, `compiler.cpp` (inferTypes), `codegen.cpp` (getEffectiveType, generateSpecializedFunction)

## Important Design Principles (from user)
- **No short-term solutions** — code must be clean and correct
- **No fallbacks** — fail hard (assert) on invalid states, don't silently default
- **Macros = code replacement** — no special "return type" for macros, just substitution
- **Only patterns that store to arguments should be macros** — other patterns use monomorphization
- **Point out inconsistencies** instead of guessing

## Build & Test
- `./scripts/build.sh` to build
- `./build/dynlex tests/required/0_simple/main.dl -o tests/required/0_simple/main && ./tests/required/0_simple/main` → expect `52`
- `--emit-llvm` flag to inspect generated IR

## Type Error Diagnostics (implemented)
- `validateExpressionTypes()` runs after inference, checks arithmetic/comparison/negate on non-numeric types
- Reports errors with source ranges pointing to the offending operand

## Monomorphization / Instantiation System
- `Instantiation` struct: `returnType` + `llvmFunction`, stored per Section in `std::map<std::vector<Type>, Instantiation>`
- `currentInstantiation` pointer on ParseContext: set during non-macro function body inference, `@intrinsic("return")` writes return type directly
- **Key invariant**: argTypes vector must be built in `nodesPassed` order (both inference and codegen)
- **Key invariant**: after Numeric→Integer defaulting, instantiation map keys must also be re-keyed

## Bugs Fixed
- **matchProgress.cpp sourceArgumentIndex bug**: `sourceArgumentIndex++` was incrementing `this` instead of `substituteStep`, causing all argument slots in submatches to read index 0. Fixed to `substituteStep.sourceArgumentIndex++`.
- **Instantiation key mismatch**: argTypes built from unordered_map iteration (non-deterministic) + Numeric types in keys not defaulted to Integer after inference. Both fixed.

## Bugs Fixed (continued)
- **Macro bodySection duplication bug**: Non-section macros (expression/effect, like `not value:`) incorrectly picked up `expr->range.line->sectionOpening` when on a line that opened a section (e.g., `if not game_over:`). Body was generated twice. Fixed: only set `bodySection` when `matchedSection->type == SectionType::Section`.
- **Missing "string" cast in codegen**: `@intrinsic("cast", "string", value)` wasn't handled. Added snprintf-based integer/float→string conversion.
- **Variable position offset bug**: `addVariableReferencesFromMatch` used `varMatch.lineStartPos` (pattern-relative) directly as absolute line position. For indented lines, this gave variables wrong positions, causing `sortArgumentsByPosition` to swap arguments. Fixed: add `reference->range().start()` offset.

## Sized Type System (implemented)
- `int byteSize` field on Type: Integer 1/2/4/8, Float 4/8, others 0
- `toLLVM()` dispatches on byteSize: i8/i16/i32/i64, f32/f64
- `promote()`: larger byteSize wins; Integer+Float → Float with max(both sizes)
- `ensureType()`: SExt/Trunc for int↔int, FPExt/FPTrunc for float↔float, SIToFP/FPToSI cross-type
- `fromString()`: maps "i8"→Integer(1), "i16"→Integer(2), etc.
- Default: Numeric → i32 (Integer, byteSize=4); float literals → f64

## Typed Call Intrinsic (implemented)
- Format: `@intrinsic("call", "library", "function", "return_type", args...)`
- Return type parsed via `Type::fromString()` from the 4th argument
- External functions declared as varargs with proper return type
- std.dl: printf → "i32", graphics: gl_create_window → "pointer", etc.

## Cast Intrinsic (updated argument order)
- Format: `@intrinsic("cast", value, type_string[, bit_size])`
- Value is argument 1, type string is argument 2, optional bit size is argument 3
- `"integer"`/`"float"` as type string: reads optional bit-size literal (e.g. 64 → byteSize=8)
- Default without bit size: 64-bit (byteSize=8)
- Pattern: `value as a 64 bit integer` → `@intrinsic("cast", value, "integer", 64)`
- Sized casts work because macros are code substitution — literal is visible at compile time

## Pointer Type System (implemented)
- `int pointerDepth` field on Type: 0=value, 1=ptr, 2=ptr-to-ptr, etc.
- `isPointer()`, `pointed()`, `dereferenced()` helpers
- LLVM: any `pointerDepth > 0` → opaque `ptr` via `PointerType::getUnqual(ctx)`
- Intrinsics: `"address of"` (returns alloca ptr), `"dereference"` (load through ptr)
- Cast target `"pointer"` → `{Integer, 8, pointerDepth=1}`
- `ensureType`: handles Pointer↔Integer via PtrToInt/IntToPtr
- `store at`/`load at`: skip IntToPtr when arg is already Pointer type
- std.dl patterns: `address of var`, `value at ptr`, `value as pointer`

## Control Flow (implemented)
- **if/else if/else**: `@intrinsic("if")`, `@intrinsic("else if")`, `@intrinsic("else")`
  - `else`/`else if` redirect unconditional predecessor branches (from if/elif bodies) to a new exit block; conditional false-path branches stay
  - Nesting works naturally — each level manages its own exit block
- **switch/case**: `@intrinsic("switch", value)`, `@intrinsic("case", value)`
  - Uses LLVM's native `switch` instruction (integer constants only)
  - `currentSwitchInst` + `currentSwitchExitBlock` on ParseContext for case intrinsics
  - Default case branches to exit (no match = skip)
  - switch doesn't set bodySection->exitBlock — insert point naturally ends at exit after all cases
- Patterns: `if/else if/else/match based on/switch on/case` all in std.dl

## Bugs Fixed (more)
- **Macro binding self-reference loop**: `resolveVarThroughMacro`/`resolveTypeThroughMacro`/`resolveMacroBinding` infinite recursion when user variable name matches macro parameter name (e.g. `set var to 2` where `set` pattern param is `var`). Fix: check `it->second != expr` (pointer equality detects the cycle).
- **Single-arg intrinsic parsing**: `@intrinsic("name")` with no comma failed to parse intrinsic name. The `"` node was passed to `detectPatternsRecursively` which didn't handle `"` as the root node. Fix: extracted `createStringLiteral()` helper, used in `processIntrinsicArg` for `"` nodes.

## Debugging Tips
- **Never dump LLVM IR to stdout/stderr in conversation** — it floods context. Use `--emit-llvm` to write to a file, or redirect output to a file and read selectively.

## TODO / Known Issues
- `promote()` doesn't check that operands are numeric before promoting (e.g. `promote(String, Float)` returns Float)
