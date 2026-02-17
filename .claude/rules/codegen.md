---
paths:
  - "src/compiler/codegen/**"
---

# Codegen Details

## Monomorphization / Instantiation
- `Instantiation` struct: `returnType` + `llvmFunction`, stored per Section in `std::map<std::vector<Type>, Instantiation>`
- `currentInstantiation` pointer on ParseContext: set during non-macro function body inference
- `generateSpecializedFunction` stores `inst.llvmFunction` before generating body (enables recursion)
- Function signature: `void generateSpecializedFunction(...)` taking `Instantiation&` to store early

## Scoped Macro Bindings
- `macroBindingStack` (`std::stack`) on ParseContext: pushed on macro entry, popped on exit
- `resolveMacroBinding`: single lookup, no chaining through bindings
- `MacroScopeGuard` RAII: temporarily pops to caller scope when generating resolved argument expressions
- `getVariablePointer`: recursively resolves through multiple macro scopes by popping/restoring the stack

## Intrinsics Reference
- **Memory**: `@intrinsic("store", var, val)` / `@intrinsic("store at", ptr, index, val)` / `@intrinsic("load at", ptr, index)` / `@intrinsic("address of", var)` / `@intrinsic("dereference", ptr)`
- **Arithmetic**: `@intrinsic("add", a, b)`, `"subtract"`, `"multiply"`, `"divide"`, `"modulo"` / `@intrinsic("negate", v)`
- **Comparison**: `@intrinsic("less than", a, b)`, `"greater than"`, `"equal"`, `"not equal"`, `"less than or equal"`, `"greater than or equal"`
- **Logical**: `@intrinsic("and", a, b)` / `@intrinsic("or", a, b)` / `@intrinsic("not", v)`
- **Math**: `@intrinsic("sin", v)`, `"cos"`, `"sqrt"`, `"abs"`, `"floor"`, `"ceil"`, `"round"`, `"exp"`, `"log"` (unary), `"pow"`, `"min"`, `"max"`, `"atan2"` (binary)
- **Control flow**: `@intrinsic("if", cond)` / `@intrinsic("else if", cond)` / `@intrinsic("else")` / `@intrinsic("loop while", cond)` / `@intrinsic("switch", value)` / `@intrinsic("case", value)`
- **Functions**: `@intrinsic("call", "library", "function", "return_type", args...)` / `@intrinsic("return"[, value])`
- **Types**: `@intrinsic("cast", value, type_string[, bit_size])` / `@intrinsic("construct", type, fields...)` / `@intrinsic("property", instance, fieldname)`
- **Shader I/O**: `@intrinsic("shader input", name)` / `@intrinsic("shader output", r, g, b, a)` / `@intrinsic("shader uniform", name)` / `@intrinsic("extract element", vec, index)`

## Math Function Codegen
- Most math intrinsics map to LLVM intrinsics (e.g. `llvm::Intrinsic::sin`), which lower to libm calls
- `atan2` has no LLVM intrinsic — directly calls libm `atan2`
- Arguments are auto-converted to float (f64) if not already float
- `-lm` is automatically added to `requiredLibraries` when any math intrinsic is used
- Registry-based: `isMathFunction()` checks `SameAsArgs` kind, excluding arithmetic and negate

## SPIR-V Shader Compilation
- `--emit-spirv` + `--shader-stage=vertex|fragment` flags
- LLVM 20 SPIR-V backend (`spirv-unknown-vulkan1.3`) only supports compute entry points
- `patchShaderBinary` post-processes: removes Linkage capability, adds OpEntryPoint, adds decorations, fixes storage classes/pointer types, strips I/O initializers
- Shader I/O globals created at module setup when `emitSPIRV` is set
- Fragment: `gl_FragCoord` (input, BuiltIn FragCoord), `gl_FragColor` (output, Location 0)
- Vertex: `in_Position` (input, Location 0), `gl_Position` (output, BuiltIn Position)

## Sized Type System
- `int byteSize` on Type: Integer 1/2/4/8, Float 4/8, others 0
- `toLLVM()`: i8/i16/i32/i64, f32/f64. Default: Numeric → i32, float literals → f64
- `ensureType()`: SExt/Trunc (int↔int), FPExt/FPTrunc (float↔float), SIToFP/FPToSI (cross-type), PtrToInt/IntToPtr (pointer↔int)
- Pointer: `pointerDepth > 0` → opaque `ptr`

## Bugs Fixed (codegen-specific)
- **Macro binding variable capture**: `return value + 1000` caused infinite recursion. Fix: temporarily erase binding while generating resolved expression.
- **Macro bindings leaking into functions**: `generateSpecializedFunction` now saves/clears/restores `macroExpressionBindings`.
- **Nested macro store**: `add value to target` → `set var to val` needed multi-level resolution. Fix: `getVariablePointer` recursively pops macro scopes.
- **Stale macro body types**: Shared expression nodes retained types from prior callers. Fix: `resetSectionTypes()` before each `inferMacroBody` (macro sections only).
- **Spurious instantiations**: Top-level inference created instantiations with undeduced args. Fix: skip non-macro function body inference when any arg is undeduced.
- **Cast macro resolution**: Cast intrinsic's type string and bits arguments may be macro-bound variable references. Both type inference (`compiler.cpp`) and codegen (`codegen.cpp` — `getEffectiveType` and `generateIntrinsicCode`) must resolve through macro bindings before checking `literalValue`.
- **SPIR-V UBO uniforms**: `glUniform1f` doesn't work with SPIR-V shaders — requires UBOs. The SPIR-V patcher (`spirv.cpp`) wraps scalar float uniforms in OpTypeStruct with Block decoration, OpAccessChain access, and Binding/DescriptorSet decorations. Host-side uses `glGenBuffers`/`glBindBufferBase`/`glBufferSubData` (patterns in `lib/graphics.dl`).
