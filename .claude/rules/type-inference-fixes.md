# Type Inference & String Class Fixes

## Bug 1: Numeric→Integer constraint mismatch (selectOverload)

**Root cause:** `selectOverload` in `compiler.cpp` checks if argument types match type constraints. Integer literals start as `Numeric` kind (not `Integer`), but `{integer:value}` constraints have `Integer` kind. `Numeric != Integer` fails the match, so constrained overloads like `{integer:value} as a string` are skipped. The fallback picks an unconstrained overload instead.

After type inference, `defaultNumericTypes` converts `Numeric→Integer(4)`. Codegen's overload selection now matches `{integer:value}` (since the arg is `Integer`), but finds no pre-existing instantiation — the inference ran a different function. This causes the "Return type must be deduced before codegen" assertion.

**Fix:** In `selectOverload`, treat `Numeric` as matching `Integer` constraints:
```cpp
DataType::Kind effectiveKind = argType.kind;
if (effectiveKind == DataType::Kind::Numeric)
    effectiveKind = DataType::Kind::Integer;
matches = effectiveKind == constraint.kind && argType.pointerDepth == constraint.pointerDepth;
```

**File:** `src/compiler/compiler.cpp`, `selectOverload` function

## Bug 2: Non-flex functions returning class types (codegen)

**Root cause:** The `construct` intrinsic returns an alloca (pointer to stack struct), but non-flex functions declare their return type as the struct itself (by value). Two issues:
1. The `return` handler does `CreateRet(alloca)` — returns a pointer instead of the struct value
2. The caller receives a struct by value but tries to load from it as if it were a pointer

**Fix:**
1. In the `return` handler (`codegenIntrinsics.cpp`): if the return value is an alloca of a struct type, load the struct before returning it
2. In the PatternCall caller (`codegen.cpp`): if the call returns a struct type, store it into a temp alloca so the rest of codegen can treat it as a pointer (matching the internal class-values-as-allocas convention)

**Files:** `src/compiler/codegen/codegenIntrinsics.cpp` (return handler), `src/compiler/codegen/codegen.cpp` (call site)

## Bug 3: Class member "name: type" parsing

**Root cause:** `parseFieldDeclaration` in `membersSection.cpp` only handles `name as type` syntax. The string class uses `name: type` syntax (e.g., `data: pointer to byte`). Without `: ` handling, the entire line becomes the field name (e.g., `"data: pointer to byte"` instead of `"data"`). Property access `the data of msg` looks for field name `"data"` and fails.

**Fix:** Add `: ` as an alternative separator in `parseFieldDeclaration`, tried after ` as `.

**File:** `src/compiler/section/membersSection.cpp`, `parseFieldDeclaration` function

## Other changes in this worktree

- **lib/string.dl**: Added `ptr as a string` conversion (unconstrained, loops to find null terminator). Changed `value as a line` body to `return value as a string + "\n" as a string`.
- **patternResolution.cpp**: Added compound type constraint resolution (e.g., `{pointer to byte:ptr}`) using recursive tree walk + flex class body evaluation.
- **typeInference.cpp**: Refactored `inferExpressionType` to use `bool& changed` + error return. Improved error messages. Made promote failure non-fatal. Added unconstrained-definition fallback in overload selection.
