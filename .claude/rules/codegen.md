---
paths:
  - "src/compiler/codegen/**"
---

# Codegen Details

## Monomorphization / Instantiation
- `Instantiation` struct: `returnType` + `llvmFunction` + `valid`, stored per Section in `std::map<std::vector<Type>, Instantiation>`
- `currentInstantiation` pointer on `InferenceContext`: set during non-macro function body inference (not on ParseContext)
- `generateSpecializedFunction` stores `inst.llvmFunction` before generating body (enables recursion)
- Function signature: `void generateSpecializedFunction(...)` taking `Instantiation&` to store early

## Scoped Macro Bindings
- `macroBindingStack` (`std::stack`) on ParseContext: pushed on macro entry, popped on exit
- `resolveVariableBinding` (codegenTypes.cpp): resolves one Variable through the current macro's binding map. Each resolution crosses one scope boundary — caller must pop before evaluating the result
- `resolveThroughMacroLayers` (codegenTypes.cpp): resolves fully through variable bindings AND macro PatternCall expansions, crossing scope boundaries as needed (pops to parent scopes when a variable isn't found in the current scope). Freely modifies scope state — callers must save/restore `macroExpressionBindings` + `macroBindingStack`. Use when inspecting the underlying expression kind (e.g., detecting property intrinsic in store dest)
- `expandMacroPatternCall` (parseContext.h): extracts macro body + parameter bindings from a PatternCall. Shared by codegen and type inference. Does not modify any binding stack
- `resolveThroughBindings` (typeInference.cpp): type inference counterpart of `resolveVariableBinding` — takes an explicit bindings map instead of using the context stack
- `resolveThroughBindingsDeep` (typeInference.cpp): type inference counterpart of `resolveThroughMacroLayers` — resolves through variable bindings AND macro PatternCalls with explicit bindings. Returns the active bindings via output parameter
- `MacroScopeGuard` RAII: temporarily pops to caller scope when generating resolved argument expressions
- `getVariablePointer`: iterates `resolveVariableBinding` + scope pops to resolve through nested macro bindings to find the actual variable's alloca

## Intrinsics Reference
- **Memory**: `@intrinsic("store", var, val)` / `@intrinsic("store at", ptr, index, val)` / `@intrinsic("load at", ptr, index)` / `@intrinsic("address of", var)` / `@intrinsic("dereference", ptr)`
- **Arithmetic**: `@intrinsic("add", a, b)`, `"subtract"`, `"multiply"`, `"divide"`, `"modulo"` / `@intrinsic("negate", v)`
- **Comparison**: `@intrinsic("less than", a, b)`, `"greater than"`, `"equal"`, `"not equal"`, `"less than or equal"`, `"greater than or equal"`
- **Logical**: `@intrinsic("and", a, b)` / `@intrinsic("or", a, b)` / `@intrinsic("not", v)`
- **Math**: `@intrinsic("sin", v)`, `"cos"`, `"sqrt"`, `"abs"`, `"floor"`, `"ceil"`, `"round"`, `"exp"`, `"log"` (unary), `"pow"`, `"min"`, `"max"`, `"atan2"` (binary)
- **Control flow**: `@intrinsic("if", cond)` / `@intrinsic("else if", cond)` / `@intrinsic("else")` / `@intrinsic("loop while", cond)` / `@intrinsic("execute body")` / `@intrinsic("switch", value)` / `@intrinsic("case", value)`
- **Functions**: `@intrinsic("call", "library", "function", "return_type", args...)` / `@intrinsic("return"[, value])`
- **Types**: `@intrinsic("cast", value, type_ref)` / `@intrinsic("type", kind[, bits])` / `@intrinsic("add pointer depth", type_ref)` / `@intrinsic("construct", type, fields...)` / `@intrinsic("property", instance, fieldname)`
- **Shader I/O**: `@intrinsic("shader input", name)` / `@intrinsic("shader output", r, g, b, a)` / `@intrinsic("shader uniform", name)` / `@intrinsic("extract element", vec, index)`

## Math Function Codegen
- Most math intrinsics map to LLVM intrinsics (e.g. `llvm::Intrinsic::sin`), which lower to libm calls
- `atan2` has no LLVM intrinsic — directly calls libm `atan2`
- Math intrinsics compute in float (f64, or f32 for SPIR-V) and must convert the result back to the inferred DynLex result type before returning from codegen
- `-lm` is automatically added to `requiredLibraries` when any math intrinsic is used
- Registry-based: `isMathFunction()` checks `SameAsArgs` kind, excluding arithmetic and negate

## SPIR-V Shader Compilation
- `--emit-spirv` + `--shader-stage=vertex|fragment` flags
- LLVM 20 SPIR-V backend (`spirv-unknown-vulkan1.3`) only supports compute entry points
- `patchShaderBinary` post-processes: removes Linkage capability, adds OpEntryPoint, adds decorations, fixes storage classes/pointer types, strips I/O initializers
- Shader I/O globals created at module setup when `emitSPIRV` is set
- Fragment: `gl_FragCoord` (input, BuiltIn FragCoord), `gl_FragColor` (output, Location 0)
- Vertex: `in_Position` (input, Location 0), `gl_Position` (output, BuiltIn Position)

## WebAssembly Compilation
- `--emit-wasm` emits a WebAssembly binary artifact through LLVM's WebAssembly backend
- The wasm emitter is a separate backend path like SPIR-V, not an extension of native linking
- The first-stage wasm output is emitted directly from LLVM; browser runtime imports and final playground wiring are separate concerns
- The wasm emitter patches the emitted module to export `main` and strips object-only custom sections like `linking` / `reloc.*` from the final runtime artifact
- Web-facing output should funnel through string writes, not per-type print overload explosions. Keep formatting/conversion in DynLex (`as a string`), and keep the wasm host ABI to environment functions like `dynlex_print_string` plus the libc-shaped imports the current stdlib/tests still use (`malloc`, `memcpy`, `snprintf`, `strlen`, `printf`, etc.).

## Sized Type System
- `int byteSize` on Type: Integer 1/2/4/8, Float 4/8, others 0
- `toLLVM()`: i8/i16/i32/i64, f32/f64. Default: Numeric → i32, float literals → f64
- `ensureType()`: SExt/Trunc (int↔int), FPExt/FPTrunc (float↔float), SIToFP/FPToSI (cross-type), PtrToInt/IntToPtr (pointer↔int)
- Pointer: `pointerDepth > 0` → opaque `ptr`

## Class Struct Store
- When storing a class value to a variable (`store` intrinsic, non-property branch), codegen checks if source and destination have the same field type layout
- **Same layout**: direct whole-struct `load`/`store` (fast path)
- **Different layout**: element-wise load from source struct, `ensureType` conversion per field, store to destination struct. This handles per-variable instantiation copies where field types diverge (e.g., construct creates `{i32,i32,i32}` but variable's copy is promoted to `{f64,f64,f64}`)
- The layout comparison checks `srcFields[i] != destFields[i]` for each field; any mismatch triggers element-wise path

## Error Propagation in Codegen
- `generateExpressionCode` returns `bool` (success/failure) with `llvm::Value *&result` output parameter. `false` = error (stop codegen immediately), `true` with `result=nullptr` = void/no-value success (e.g., store, control flow intrinsics)
- `generateIntrinsicCode` uses the same `bool` + `llvm::Value *&result` convention
- `generateSectionCode` calls `generateExpressionCode` per line and returns `false` on the first failure, stopping further IR generation
- `TRY_EXPR(var, expr)` macro in `codegenIntrinsics.cpp`: declares `llvm::Value *var`, calls `generateExpressionCode`, returns `false` from the enclosing function on failure
- **Why**: previously both functions returned `llvm::Value*` where `nullptr` meant both "void" and "error". Void intrinsics (store, if, loop) returning nullptr caused callers to incorrectly treat success as failure, and actual errors (e.g., shader intrinsics without `--emit-spirv`) produced nullptr that was passed to LLVM builder methods, causing segfaults and memory corruption

## Bugs Fixed (codegen-specific)
- **Macro binding variable capture**: `return value + 1000` caused infinite recursion. Fix: temporarily erase binding while generating resolved expression.
- **Macro bindings leaking into functions**: `generateSpecializedFunction` now saves/clears/restores `macroExpressionBindings`.
- **Caller macro bindings must never leak into monomorphized function bodies**: `generateSpecializedFunction` must isolate `macroBindingFrames` to a fresh root frame while emitting the callee body, then restore caller frames after emission. Pushing an empty child frame is insufficient because cross-frame lookup can still capture parent caller bindings by name.
- **Section macro locals must be allocated per invocation and restored after expansion**: section-macro replacement bodies can declare local variables (e.g., `set __i to 0`). Macro expansion must allocate replacement-section locals before emitting replacement lines, and restore the macro definition's `VariableReference::alloca` pointers after the call so nested/re-entrant macro calls do not overwrite each other's active local storage bindings.
- **Section macro body placement is explicit-capable and wrapper-safe**: section macros still auto-emit the opened body after replacement by default, but `@intrinsic("execute body")` can emit that body at a specific point inside replacement code. Ownership resolution is deterministic and layered: source-section ancestry first, then active macro call-site section stack, then active macro-definition stack. This allows helper macro wrappers (for example `execute the body`) and nested loop-pattern macros to resolve the intended caller body without name hacks.
- **Explicit body emission must not finalize loop control flow early**: when `execute body` is emitted explicitly inside a section macro, codegen must continue in the current replacement flow so lines after it (for example `increment index`) remain inside the loop body. Back-edge/exit finalization happens once at section-macro exit (or during fallback auto-emission) to avoid both infinite loops and duplicate body runs.
- **Cross-frame binding resolution must be single-step**: resolving a bound variable name across macro frames should return one substitution step at a time. Multi-step resolution in one pass can self-capture sibling macro parameters with the same names (e.g., `set var to val` with argument `val`) and silently drop stores.
- **Nested macro store**: `add value to target` → `set var to val` needed multi-level resolution. Fix: `getVariablePointer` recursively pops macro scopes. For property stores through nested macros (e.g., `add value to the x of target`), `resolveThroughMacroLayers` now also crosses scope boundaries. The store handler generates the value first, then saves/restores scopes around dest resolution.
- **Stale macro body types**: Shared expression nodes retained types from prior callers. Fix: `resetSectionTypes()` before each `inferMacroBody` (macro sections only).
- **Macro-expanded `construct` nodes cannot rely on `expr->type` during codegen**: `cloneMacroExpansionExpression()` clears cloned expression types, so wrappers like `new string from data ...` reach codegen as fresh `@intrinsic("construct", ...)` nodes with `expr->type == unresolved`. `getEffectiveType()` must reconstruct the `construct` result type from its compile-time type argument instead of assuming inference annotations survived cloning.
- **Spurious instantiations**: Top-level inference created instantiations with undeduced args. Fix: skip non-macro function body inference when any arg is undeduced.
- **Cast simplification**: Cast now always takes a TypeReference (from `@intrinsic("type", ...)` or class patterns), not a string+bits. The old string-based path (`"integer"`, `"float"`, `"string"`, `"pointer"`) was removed. String cast is detected by checking target type `{Integer, 1, ptr=1}` + numeric source → snprintf path.
- **SPIR-V UBO uniforms**: `glUniform1f` doesn't work with SPIR-V shaders — requires UBOs. The SPIR-V patcher (`spirv.cpp`) wraps scalar float uniforms in OpTypeStruct with Block decoration, OpAccessChain access, and Binding/DescriptorSet decorations. Host-side uses `glGenBuffers`/`glBindBufferBase`/`glBufferSubData` (patterns in `lib/graphics.dl`).
- **Shader intrinsics crash without --emit-spirv**: `shader input`, `shader uniform`, `shader output` had `assert()` calls that crashed when shader code was compiled without `--emit-spirv`. Replaced with proper error diagnostics + `return false`.
- **Codegen null propagation crashes**: `generateExpressionCode` returning nullptr on error was passed directly to LLVM builder methods (`CreateExtractElement`, `CreateInsertElement`, etc.), causing segfaults and memory corruption. Fixed by refactoring to bool return + output parameter (see "Error Propagation in Codegen" above).
