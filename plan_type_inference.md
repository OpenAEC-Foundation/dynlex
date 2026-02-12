# Type Inference for DynLex

## Design Decisions

- **Default type is f64** — all numeric literals without explicit cast are `double`
- **Fixed types at definition** — a variable's type is locked at first assignment, never widens
- **Use-site conversions** — when passing int to a float parameter (or vice versa), conversion happens at the expression boundary
- **Monomorphization** — patterns are implicitly generic (like C++ templates). Each unique type combination at a call site generates a separate LLVM function specialization
- **No sized integers by default** — only f64 and i64 (via cast). Sized types (i8/i16/i32) available through `cast` intrinsic
- **Narrowing is an error** — float → int requires explicit cast. Widening (int → float) is implicit
- **Future optimization** — a later pass can detect "integer-like" floats (assigned integer literals, only add/sub/mul/compare, never fractional) and silently emit i64. LLVM won't do this for us

## Type Representation

Create `src/compiler/type.h`:

```cpp
struct Type {
    enum class Kind { Undeduced, Void, Bool, Integer, Float, String };
    Kind kind = Kind::Undeduced;

    bool isNumeric() const { return kind == Kind::Integer || kind == Kind::Float; }
    bool isDeduced() const { return kind != Kind::Undeduced; }

    static Type promote(Type a, Type b);      // int + float → float
    llvm::Type *toLLVM(llvm::LLVMContext &ctx) const;
};
```

## Where Types Live

- **`Expression`** — `Type type` field (result type of evaluating this expression)
- **`Variable`** — `Type type` field (locked at first assignment)
- **`Section`** (pattern defs) — `Type returnType` field (void for effects, inferred for expressions)

## New Compiler Pass: `inferTypes()`

```
compile() = import → analyzeSections → resolvePatterns → inferTypes → generateCode
```

### Algorithm (bottom-up, iterative fixed-point)

1. **Literals** — all numeric literals → Float (f64). String literals → String
2. **Cast intrinsic** — `@intrinsic("cast", "i64", value)` → Integer, `@intrinsic("cast", "i8", value)` → Integer (i8), etc.
3. **Arithmetic intrinsics** — `add/subtract/multiply/divide/modulo`: promote(left, right). Default: Float
4. **Comparison intrinsics** — `less than/greater than/equal/not equal`: Bool (i1, zext as needed)
5. **Store intrinsic** — propagates RHS type to variable. Variable type locked at first store
6. **Return intrinsic** — propagates argument type to enclosing pattern's return type
7. **Pattern calls** — monomorphize: each (pattern, type-tuple) → separate specialization
8. **Variables** — type = type of first store's RHS

Iterate until no types change. Error if `Undeduced` remains.

## Intrinsics

| Intrinsic | Arguments | Result Type |
|-----------|-----------|-------------|
| `cast` | target_type_string, value | specified type |
| `store` | var, value | Void (propagates value type to var) |
| `return` | value | Void (propagates to pattern return type) |
| `add` | left, right | promote(left, right) |
| `subtract` | left, right | promote(left, right) |
| `multiply` | left, right | promote(left, right) |
| `divide` | left, right | promote(left, right) |
| `modulo` | left, right | promote(left, right) |
| `less than` | left, right | Bool |
| `greater than` | left, right | Bool |
| `equal` | left, right | Bool |
| `not equal` | left, right | Bool |
| `call` | library, func, format, ...args | Integer (C return) |
| `loop while` | condition | Void (control flow) |
| `if` | condition | Void (control flow) |

## Codegen Changes (codegen.cpp)

- `getValueType()` → dispatch on Expression/Type: Float→`double`, Integer→`i64`, Bool→`i1`, String→`ptr`
- `generatePatternFunction()` → typed parameters and return type per specialization
- Literal: Float emits `ConstantFP::get`, String emits global constant + ptr
- Arithmetic: Float → `CreateFAdd/FSub/FMul/FDiv`, Integer → `CreateAdd/Sub/Mul/SDiv`
- Mixed operands: insert `sitofp` (int→float) at use site
- Comparisons: Float → `CreateFCmpOLT` etc., Integer → `CreateICmpSLT` etc.
- Allocas: typed per variable
- Pattern calls: look up correct specialization by argument types

## Monomorphization

Each pattern definition can produce multiple LLVM functions. Track with a map:

```cpp
// Key: (Section*, vector<Type>) → llvm::Function*
std::map<std::pair<Section*, std::vector<Type>>, llvm::Function*> specializations;
```

When generating a pattern call:
1. Determine argument types
2. Look up or generate the specialization for those types
3. Call the specialization

## Files to Create/Modify

| File | Action |
|------|--------|
| `src/compiler/type.h` | **Create** — Type struct |
| `src/compiler/expression.h` | Add `Type type` field |
| `src/compiler/section/variable.h` | Add `Type type` field |
| `src/compiler/section/section.h` | Add `Type returnType` field |
| `src/compiler/compiler.h` | Declare `inferTypes()` |
| `src/compiler/compiler.cpp` | Add `inferTypes()` pass, call from `compile()` |
| `src/compiler/codegen/codegen.cpp` | Type-aware IR generation + monomorphization |

## Incremental Steps

1. Create `Type` struct
2. Add type fields to Expression, Variable, Section
3. Implement `inferTypes()` — literal typing + intrinsic rules + variable propagation
4. Update codegen for typed arithmetic (int vs float paths)
5. Add monomorphization (specialization map, deferred function generation)
6. Update codegen for float literals, typed allocas, cast intrinsic
7. Verify: test 0_simple → `52`, add float test

## Verification

```bash
./scripts/build.sh
./build/dynlex tests/required/0_simple/main.dl && ./tests/required/0_simple/main  # expect: 52
./build/dynlex --emit-llvm tests/required/0_simple/main.dl  # inspect .ll for correct types
```
