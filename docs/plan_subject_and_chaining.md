# Implementation Plan: Subject Tracking (`it`) and Statement Chaining (`and`)

## Overview

Two new language features that work together:
- **`it`** — a function that resolves to the current subject variable
- **`and`** — chains multiple statements on a single line

Both are defined as patterns in std.dl, not hardcoded in the compiler.

## Prerequisites: Unify Effects and Functions

Effects and functions are nearly identical — effects are just functions that return void. Unifying them enables expression submatches, which `and` requires.

### Changes
- Merge the Effect and Function pattern trees into one
- Remove `SectionType::Effect` (or make it an alias)
- Allow sub-matches to match expression-like patterns (patterns that return void)
- Update `MatchProgress::step()` — sub-matches search the unified tree
- Update `resolveReferences`, `analyzeSections`, and codegen to work with one tree

### Result
Patterns like `left and right` can now take two statement-level arguments.

## Feature 1: Statement Chaining (`and`)

### std.dl definition
```
macro function left and right:
    replacement:
        left
        right
```

This executes `left`, then `right`. Since effects/functions are unified, both arguments can be any statement.

### Example
```
set x to 5 and increment x
```
Parses as: `and(set x to 5, increment x)`

## Feature 2: Subject Tracking (`it`)

### Intrinsics
- **`@intrinsic("new subject", var)`** — sets `var` as the active subject
- **`@intrinsic("subject")`** — returns the current subject variable; errors if no subject is set or if the subject is ambiguous

### How the subject is determined

After each statement:
1. **Explicit:** if the statement's macro body contains `@intrinsic("new subject", var)`, that variable becomes the subject
2. **Implicit:** if exactly one variable is referenced in the statement, it becomes the subject automatically
3. **Ambiguous:** if multiple variables are referenced and no explicit `@intrinsic("new subject")` was used, the subject becomes ambiguous. Using `it` after this produces the error: *"what subject are you referring to?"*

The subject persists across lines within a scope.

### std.dl definitions
```
function it:
    replacement:
        @intrinsic("subject")
```

```
macro set var to val:
    replacement:
        @intrinsic("store", var, val)
        @intrinsic("new subject", var)
```

Other macros that clearly operate on one variable can also set the subject explicitly. Patterns with a single variable argument get implicit subject tracking for free.

### Examples
```
set x to 5
increment it          # it = x (explicit subject from set macro)
print it              # it = x (implicit, only one variable in 'increment x')

set y to it + 1       # it = x, then subject becomes y (explicit from set)
print it              # it = y
```

```
set x to 5 and increment it    # 'and' chains: set x, then increment it (= x)
```

### Error cases
```
add x to y
print it              # error: "what subject are you referring to?"
                      # (two variables, no explicit @intrinsic("new subject"))
```

```
x + 1                 # error: "unused expression result"
                      # function returns a value but is used at line-level
                      # (the result is discarded — likely a mistake)
```

Line-level statements that return a non-void value are errors. The user probably forgot `set ... to` or `print`. This is checked after type inference — if a top-level line expression has a non-void return type, report *"unused expression result"*.

## Implementation Steps

### Step 1: Unify effects and functions
- Merge pattern trees
- Update sub-match logic to allow matching against the unified tree
- Update all references to `SectionType::Effect` / `SectionType::Function`
- Verify existing tests still pass

### Step 2: Implement `and` pattern
- Add `macro function left and right:` to std.dl
- Should work with no compiler changes beyond unification
- Test: `set x to 5 and print x`

### Step 3: Implement subject tracking
- Add `currentSubject` (variable pointer + ambiguous flag) to `ParseContext`
- Implement `@intrinsic("new subject", var)` — sets `currentSubject` during resolution
- Implement implicit subject tracking — after each statement, count variable references; if exactly one, set as subject
- Implement `@intrinsic("subject")` — returns `currentSubject` or errors

### Step 4: Implement `it` pattern
- Add `function it:` to std.dl with `@intrinsic("subject")` replacement
- Test: `set x to 5`, `increment it`, `print it`

### Step 5: Combined tests
- `set x to 5 and increment it`
- Subject across lines
- Ambiguous subject error
- Subject in nested functions

## Design Notes

- No hardcoded syntax — `and`, `it`, and subject tracking are all pattern-defined
- Intrinsic names are human readable, no underscores
- Subject is a compile-time concept resolved during pattern resolution
- The implicit single-variable rule keeps most patterns ergonomic without requiring explicit `@intrinsic("new subject")` everywhere
