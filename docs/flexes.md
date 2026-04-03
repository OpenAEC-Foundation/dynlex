# Flex System Design

## Overview

Flexes are patterns that get inlined at the call site instead of being compiled to separate functions. This provides zero overhead - the code is directly substituted.

## Syntax

```
# Flex function - inlined, uses "replacement:" instead of "execute:"
flex function return value:
    replacement:
        @intrinsic("ret", value)

# Flex function - inlined, uses "replacement:" instead of "get:"
flex function left + right:
    replacement:
        @intrinsic("add", left, right)

# Regular function - compiled to function, uses "execute:"
function print msg:
    execute:
        @intrinsic("print", msg)

# Regular function - compiled to function, uses "get:", can recurse
function factorial of n:
    get:
        if n <= 1:
            return 1
        return n * factorial of (n - 1)
```

## Key Differences

| Type | Keyword | Body Section | Compiled To | Can Recurse |
|------|---------|--------------|-------------|-------------|
| `flex function` | `flex` | `replacement:` | Inline code | No |
| `flex function` | `flex` | `replacement:` | Inline code | No |
| `function` | - | `execute:` | Function | Yes |
| `function` | - | `get:` | Function | Yes |
| `section` | - | `execute:` | Function | Yes |
| `flex section` | `flex` | `replacement:` | Inline code | No |

## Implementation Status

### Completed

1. **Section hierarchy refactored:**
   - `DefinitionSection` - new base class for function/function/section definitions
   - `EffectSection` - inherits from DefinitionSection
   - `FunctionSection` - inherits from DefinitionSection
   - `SectionSection` - new, inherits from DefinitionSection

2. **`isFlex` flag added to Section** (`section.h`)

3. **Keyword parsing updated** (`section.cpp`):
   - Iterates through keywords like `flex`, `function`, `function`, `section`
   - Sets `isFlex = true` when `flex` keyword is found
   - Creates appropriate section type

4. **Body section handling** (`definitionSection.cpp`):
   - Base class handles `replacement:` for flexes
   - Derived classes handle their specific keywords (`execute:`, `get:`)
   - Falls back to base class which gives error if nothing matches

5. **Codegen skips flexes** (`codegen.cpp`):
   - `generateFunctionCode` inlines flex bodies instead of calling functions

### TODO

1. **Codegen: Inline flex bodies**
   - In `generateFunctionCode` for `PatternCall`, check if `matchedSection->isFlex`
   - If flex: inline the replacement body instead of calling a function
   - Need to bind pattern variables to their argument values

2. **Update test file** to use flex syntax:
   ```
   flex function return value:
       replacement:
           @intrinsic("ret", value)

   flex function left + right:
       replacement:
           @intrinsic("add", left, right)
   ```

## Files Changed

- `src/compiler/section/section.h` - added `isFlex` field
- `src/compiler/section/section.cpp` - keyword parsing for `flex`
- `src/compiler/section/definitionSection.h` - new file
- `src/compiler/section/definitionSection.cpp` - new file
- `src/compiler/section/sectionSection.h` - new file
- `src/compiler/section/sectionSection.cpp` - new file
- `src/compiler/section/effectSection.h` - now inherits from DefinitionSection
- `src/compiler/section/effectSection.cpp` - simplified, delegates to base
- `src/compiler/section/functionSection.h` - now inherits from DefinitionSection
- `src/compiler/section/functionSection.cpp` - simplified, delegates to base
- `src/compiler/codegen/codegen.cpp` - skips flexes in function generation

## Design Decisions

1. **Flexes are compile-time only** - no runtime overhead, pure code substitution
2. **`replacement:` keyword** - distinguishes flex body from function body
3. **Shared base class** - DRY approach, DefinitionSection handles common logic
4. **Check derived first, then base** - derived classes try their keywords first, fall back to base for `replacement:` or error
