# Compiler Steps

This document describes how the 3BX compiler transforms source code into executable machine code.

## Example Input

The following example is used throughout this document:

```
import lib/prelude

effect print msg:
    execute:
        @intrinsic("print", msg)

expression left + right:
    get:
        return @intrinsic("add", left, right)

print 5 + 3
```

---

## Step 1: Import Resolution

Read all files and merge them using imports. Replace import lines with the imported code.

### Input
```
import lib/prelude

effect print msg:
    execute:
        @intrinsic("print", msg)

print 5 + 3
```

### Output
```
# Contents of lib/prelude.3bx inserted here:
effect return val:
    execute:
        @intrinsic("return", val)

effect set var to val:
    execute:
        @intrinsic("store", var, val)

# ... rest of prelude ...

# Original file continues:
effect print msg:
    execute:
        @intrinsic("print", msg)

print 5 + 3
```

---

## Step 2: Section Analysis

Analyze sections by going over each line and finding indent level. Each section has code lines and each code line can have one child section (when it contains a colon).

The code line becomes a "pattern". When a code line has a colon at the end and the line starts with `section`, `effect`, or `expression`, the rest of the line is a pattern definition. Otherwise, the code line is a pattern reference.

### Input
```
effect print msg:
    execute:
        @intrinsic("print", msg)

expression left + right:
    get:
        return @intrinsic("add", left, right)

print 5 + 3
```

### Output (Section Tree)
```
Section (root):
  CodeLine: "effect print msg:" [pattern definition]
    Section:
      CodeLine: "execute:" [section definition]
        Section:
          CodeLine: "@intrinsic("print", msg)" [pattern reference]

  CodeLine: "expression left + right:" [pattern definition]
    Section:
      CodeLine: "get:" [section definition]
        Section:
          CodeLine: "return @intrinsic("add", left, right)" [pattern reference]

  CodeLine: "print 5 + 3" [pattern reference]
```

---

## Step 3: Pattern Resolution

Resolve patterns and variables iteratively using a pattern tree. Each section and line has a boolean indicating if their pattern is resolved.

> **See [PATTERN_MATCHING.md](PATTERN_MATCHING.md)** for detailed documentation on the pattern tree structure, expression substitution, and alternative handling.

### Important: No LLVM Interaction

Pattern resolution operates **entirely on strings and text**. There is no interaction with LLVM during this phase. The output of pattern resolution is:
- Resolved patterns with their string-based arguments
- Pattern matches that map code lines to their matched patterns
- String values for all captured variables

This separation is intentional:
- Pattern resolution is a **syntactic** operation (matching text against patterns)
- Code generation is a **semantic** operation (producing executable code)
- Keeping them separate allows for alternative backends and easier testing

### Algorithm

```
# Phase 1: Resolve single-word pattern definitions
for each unresolved code line:
    if codeline.type is not a pattern reference:
        if codeline.text is one word:
            codeline.resolved = true
            codeline.childSection.resolved = true

# Phase 2: Match pattern references to definitions
for each unresolved code line:
    if codeline.type is a pattern reference:
        if a resolved pattern matches codeline with single words substitution:
            codeline.resolved = true
            add arguments of the call to codeline.section.resolvedVariables

# Phase 3: Resolve sections when all lines are resolved
for each unresolved section:
    for each code line in section:
        if !codeline.resolved:
            next section
    # all lines are resolved!
    section.resolved = true
    substitute all known variables in the pattern definition
go back to phase 2 if there are still unresolved sections
```

### Input
```
effect print msg:
    execute:
        @intrinsic("print", msg)

expression left + right:
    get:
        return @intrinsic("add", left, right)

print 5 + 3
```

### Output (Resolved Patterns)
```
Pattern Definitions:
  - effect "print $msg"
      variables: [msg]
      body: @intrinsic("print", msg)

  - expression "$left + $right"
      variables: [left, right]
      body: return @intrinsic("add", left, right)

Pattern References:
  - "print 5 + 3"
      matches: effect "print $msg"
      arguments: {msg: expression "5 + 3"}

  - "5 + 3"
      matches: expression "$left + $right"
      arguments: {left: 5, right: 3}
```

---

## Step 4: Type Inference

Infer types for all variables based on intrinsic usage and literal values.

### Rules

1. **Literals**: Numbers are `i64`, strings are `i8*` (pointer to char)
2. **Intrinsic return types**:
   - `add`, `sub`, `mul`, `div`: same type as operands
   - `cmp_*`: `i1` (boolean)
   - `print`: `void`
   - `store`: `void`
   - `load`: type of the variable
3. **Variables**: Inferred from first assignment or usage

---

## Step 5: Code Generation (LLVM IR)

Generate LLVM Intermediate Representation from resolved and typed patterns.

### 5.1: Context Resolution (Frames and Sections)

During code generation, the compiler resolves relative references like `@intrinsic("frame", index)` and `@intrinsic("section", frame, index)`.

- **Frame Context:** The code generator maintains the call stack. It resolves `frame back` by looking up the frame at the specified depth.
- **Section Context:** Sections are resolved relative to a frame. `frame's child section` (or `-1 sections up`) refers to the first indented code block following the code line that created the frame.
- **Section Variables:** Variables can be set on sections (e.g., `@intrinsic("store_section", section, name, value)`). These variables are scoped to the section's lifecycle.

### 5.2: LLVM IR Generation

### Input
```
effect "print $msg":
    body: @intrinsic("print", msg)

expression "$left + $right":
    body: return @intrinsic("add", left, right)

Call: print 5 + 3
```

### Output (LLVM IR)
```llvm
; External declaration for print
declare i32 @printf(i8*, ...)

@.str = private constant [4 x i8] c"%d\0A\00"

; Generated from: expression "$left + $right"
define i64 @expr_add(i64 %left, i64 %right) {
entry:
    %result = add i64 %left, %right
    ret i64 %result
}

; Generated from: effect "print $msg"
define void @effect_print(i64 %msg) {
entry:
    call i32 (i8*, ...) @printf(i8* getelementptr ([4 x i8], [4 x i8]* @.str, i32 0, i32 0), i64 %msg)
    ret void
}

; Main function (top-level code)
define i32 @main() {
entry:
    ; print 5 + 3
    %0 = call i64 @expr_add(i64 5, i64 3)  ; evaluates to 8
    call void @effect_print(i64 %0)
    ret i32 0
}
```

---

## Step 6: Optimization and Output

Apply LLVM optimization passes and generate final output.

### Optimization Passes

1. **Inlining**: Small functions are inlined at call sites
2. **Constant folding**: `5 + 3` becomes `8` at compile time
3. **Dead code elimination**: Unused code is removed
4. **Register allocation**: Variables are assigned to CPU registers

### Input (LLVM IR)
```llvm
define i32 @main() {
entry:
    %0 = call i64 @expr_add(i64 5, i64 3)
    call void @effect_print(i64 %0)
    ret i32 0
}
```

### Output (Optimized LLVM IR)
```llvm
define i32 @main() {
entry:
    ; Constant folded: 5 + 3 = 8
    ; Inlined: effect_print body
    call i32 (i8*, ...) @printf(i8* getelementptr ([4 x i8], [4 x i8]* @.str, i32 0, i32 0), i64 8)
    ret i32 0
}
```

### Output (x86-64 Assembly)
```asm
main:
    push    rbp
    mov     rbp, rsp
    lea     rdi, [rip + .str]
    mov     esi, 8
    xor     eax, eax
    call    printf
    xor     eax, eax
    pop     rbp
    ret

.str:
    .asciz  "%d\n"
```

### Final Output

The compiler produces one of:
- **Executable**: Native binary for the target platform
- **Object file**: `.o` file for linking
- **LLVM IR**: `.ll` file for inspection or further processing

---

## Summary

| Step | Input | Output |
|------|-------|--------|
| 1. Import Resolution | Source files with imports | Single merged source |
| 2. Section Analysis | Merged source | Section tree with code lines |
| 3. Pattern Resolution | Section tree | Resolved patterns with variables |
| 4. Type Inference | Resolved patterns | Typed patterns and expressions |
| 5. Code Generation | Typed patterns | LLVM IR |
| 6. Optimization | LLVM IR | Optimized native code |

---

## Pattern Tree Design

> **Full documentation: [PATTERN_MATCHING.md](PATTERN_MATCHING.md)**

Pattern matching uses a trie-like tree structure with:
- **Merged literals**: Consecutive literal tokens merged into single strings for O(1) lookup
- **Expression slots**: `$` nodes that recursively match sub-expressions
- **Branch-and-merge**: Alternatives `[a|b]` branch within a pattern and merge back, avoiding exponential expansion
- **Lazy captures**: `{word}` captures unevaluated expressions for deferred evaluation
