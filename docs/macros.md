# Macro System Design

## Overview

Macros are patterns that get inlined at the call site instead of being compiled to separate functions. This provides zero overhead - the code is directly substituted.

## Syntax

```
# Macro effect - inlined, uses "replacement:" instead of "execute:"
macro effect return value:
    replacement:
        @intrinsic("ret", value)

# Macro expression - inlined, uses "replacement:" instead of "get:"
macro expression left + right:
    replacement:
        @intrinsic("add", left, right)

# Regular effect - compiled to function, uses "execute:"
effect print msg:
    execute:
        @intrinsic("print", msg)

# Regular expression - compiled to function, uses "get:", can recurse
expression factorial of n:
    get:
        if n <= 1:
            return 1
        return n * factorial of (n - 1)
```

## Key Differences

| Type | Keyword | Body Section | Compiled To | Can Recurse |
|------|---------|--------------|-------------|-------------|
| `macro effect` | `macro` | `replacement:` | Inline code | No |
| `macro expression` | `macro` | `replacement:` | Inline code | No |
| `effect` | - | `execute:` | Function | Yes |
| `expression` | - | `get:` | Function | Yes |
| `section` | - | `execute:` | Function | Yes |
| `macro section` | `macro` | `replacement:` | Inline code | No |

## Implementation Status

### Completed

1. **Section hierarchy refactored:**
   - `DefinitionSection` - new base class for effect/expression/section definitions
   - `EffectSection` - inherits from DefinitionSection
   - `ExpressionSection` - inherits from DefinitionSection
   - `SectionSection` - new, inherits from DefinitionSection

2. **`isMacro` flag added to Section** (`section.h`)

3. **Keyword parsing updated** (`section.cpp`):
   - Iterates through keywords like `macro`, `effect`, `expression`, `section`
   - Sets `isMacro = true` when `macro` keyword is found
   - Creates appropriate section type

4. **Body section handling** (`definitionSection.cpp`):
   - Base class handles `replacement:` for macros
   - Derived classes handle their specific keywords (`execute:`, `get:`)
   - Falls back to base class which gives error if nothing matches

5. **Codegen skips macros** (`codegen.cpp`):
   - `generatePatternFunctions` skips sections with `isMacro = true`

### TODO

1. **Codegen: Inline macro bodies**
   - In `generateExpressionCode` for `PatternCall`, check if `matchedSection->isMacro`
   - If macro: inline the replacement body instead of calling a function
   - Need to bind pattern variables to their argument values

2. **Update test file** to use macro syntax:
   ```
   macro effect return value:
       replacement:
           @intrinsic("ret", value)

   macro expression left + right:
       replacement:
           @intrinsic("add", left, right)
   ```

## Files Changed

- `src/compiler/section/section.h` - added `isMacro` field
- `src/compiler/section/section.cpp` - keyword parsing for `macro`
- `src/compiler/section/definitionSection.h` - new file
- `src/compiler/section/definitionSection.cpp` - new file
- `src/compiler/section/sectionSection.h` - new file
- `src/compiler/section/sectionSection.cpp` - new file
- `src/compiler/section/effectSection.h` - now inherits from DefinitionSection
- `src/compiler/section/effectSection.cpp` - simplified, delegates to base
- `src/compiler/section/expressionSection.h` - now inherits from DefinitionSection
- `src/compiler/section/expressionSection.cpp` - simplified, delegates to base
- `src/compiler/codegen/codegen.cpp` - skips macros in function generation

## Design Decisions

1. **Macros are compile-time only** - no runtime overhead, pure code substitution
2. **`replacement:` keyword** - distinguishes macro body from function body
3. **Shared base class** - DRY approach, DefinitionSection handles common logic
4. **Check derived first, then base** - derived classes try their keywords first, fall back to base for `replacement:` or error
