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
- **TransformedPattern keyframe shift bug**: `replaceLocal()` computed `shift = (endPos - startPos) + replacement.length()` — should be `-` not `+`. Over-shifted keyframes by `2 * replacement.length()` per replacement, causing variable token positions to drift when strings/numbers preceded them.
- **Submatch patternPos not propagated**: When a submatch (sub-expression like `i - 1`) completed and the parent resumed, `patternPos` was not updated from the submatch. Variables discovered after a submatch had positions calculated as if the submatch text wasn't consumed. Fix: `stepUp.patternPos = patternPos` in the stepUp lambda.

## Pattern Specificity Rematching (implemented)
- When a more-specific definition is added to the tree (literal where existing def has argument slot),
  references matched to the less-specific definition are invalidated and re-matched
- **Key functions:**
  - `findLessSpecificDefinitions`: walks definition path through tree, tracking argument-alternative paths.
    Only reports definitions that complete a full match through the argument path (avoids false positives).
  - `walkElementsForOverlap`: helper that advances both the literal path and argument-alternative nodes
  - `classifyDiscoveredVariables`: lightweight VL→Variable classification without creating VariableReferences
  - `trackMatchDefinitions`: recursively tracks definitions from sub-matches (not just top-level)
  - `incrementVariableLikeCounts`: inverse of decrement, used when un-resolving references
- **Resolution phases:**
  1. Iterative loop: resolve definitions (add to tree, detect overlaps), invalidate stale matches, resolve body refs (deferred VarRef creation)
  2. Create VariableReferences from all stable matches
  3. Resolve global references
  4. Expand expressions, resolve variable references
- **Invalidation flow:** revert ALL Variable→VariableLike, re-derive from valid matches + singleVarRefs, check which defs need re-resolution, rebuild trees
- **Key invariant:** `definitionToReferences` must track sub-match definitions too (not just top-level), otherwise sub-expression rematching won't trigger

## Bugs Fixed (specificity & codegen)
- **Word element missing in `walkElementsForOverlap`**: When the new definition had a `{word:name}` element, it fell into the `else` (literal) branch. This looked for `literalChildren[param_name]` (wrong) and treated `wordChild` as less specific (wrong — same specificity). Fix: added explicit `Word` case that follows `wordChild` on `current` and `argumentChild` on `argAlternatives`.
- **Macro binding variable capture in codegen**: `return value + 1000` inside a non-macro function crashed with infinite recursion. The `return value:` macro bound "value" → expression(`value + 1000`). The Variable("value") inside the argument re-resolved through the same binding → infinite loop. Fix: in the Variable codegen, when resolving through macroExpressionBindings, temporarily erase the binding while generating the resolved expression. This ensures macro arguments evaluate in the caller's context.
- **Macro bindings leaking into non-macro functions**: `generateSpecializedFunction` didn't save/clear `macroExpressionBindings`. Call-site macro bindings (e.g., from `set var to val:`) leaked into the function body, causing parameter name collisions. Fix: save/clear/restore `macroExpressionBindings` in `generateSpecializedFunction`.
- **Numeric literals in definitions can't be matched**: Definitions like `expression compute 3 + 5 quickly:` have "3"/"5" as literal text (Other), but references replace numbers with `\a` (argument markers). Variable elements in references skip literal matching in `step()`, so such definitions can never be matched. This is a known design limitation, not a bug — definitions should use words, not numbers, for fixed text.

## Variable Scoping & Global Variables (implemented)

### Function-boundary scoping
- Variables are local to their function scope by default
- Phase 4 (variable resolution) stops grouping at **function boundaries** (non-macro Expression/Effect sections)
- This prevents same-named variables in different functions from being merged into one (e.g., `len` in main vs `len` in `the length of str`)
- Variables inside nested blocks (loops, if-statements) still access their parent function's variables

### Global variables
- Declared via `globals:` section in Expression/Effect definitions
- Syntax: inline `globals: var1, var2, var3` or block form (one per line)
- Stored as `globalVariables` on the Expression/Effect Section
- `declaredGlobalVariables` set on ParseContext (populated during section parsing for fast lookup)
- Functions that declare `globals: var` can read/write the module-level variable
- Functions that don't declare it get their own local variable (shadowing, no error)
- LLVM codegen: `@name = internal global <type> zeroinitializer` (module-level, internal linkage — visible within the executable only)
- Global variables stored via `reinterpret_cast<AllocaInst*>(globalVar)` in `varDef->alloca` so existing codegen paths work unchanged
- Key files: `compiler.cpp` (Phase 4 grouping), `codegen.cpp` (allocateSectionVariables), `definitionSection.cpp` (inline parsing), `globalsSection.cpp`/`h` (block parsing)

### ListingSection base class
- Shared base for `MembersSection` and `GlobalsSection`
- Handles comma-separated and newline-separated lists via `processLine` → `addItem` virtual dispatch
- `parseCommaSeparatedList` utility in `parseUtils.h` used by both ListingSection and inline parsing in ClassSection/DefinitionSection
- Test: `tests/required/10_globals/` — covers shared globals, shadowing, and function-local scoping

## Bugs Fixed (variable scoping)
- **Cross-function variable grouping**: Variables with the same name in unrelated functions (e.g., `len` in main and `len` in `the length of str`) were grouped together in Phase 4 because the parent-chain walk had no function boundary check. The earliest reference was chosen as the definition, placing the alloca in the wrong function. Other functions then tried to use a `%var` that didn't exist in their scope → invalid IR. Fix: stop walking the parent chain at non-macro Expression/Effect sections unless the variable is declared as global.
- **variableDefinitions placed in wrong section**: `definition->range.section()->variableDefinitions[name]` put the definition in whichever section the earliest reference happened to be in, not the highest section. Changed to `highestSection->variableDefinitions[name]` so the alloca is created in the correct scope.

## Debugging Tips
- **Never dump LLVM IR to stdout/stderr in conversation** — it floods context. Use `--emit-llvm` to write to a file, or redirect output to a file and read selectively.

## TODO / Known Issues
- `promote()` doesn't check that operands are numeric before promoting (e.g. `promote(String, Float)` returns Float)
