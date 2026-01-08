# 3BX Language Specification

## Overview

3BX is a language designed to help humans understand code better and agents code better.

Almost all syntax is defined in the language itself. The compiler should focus on context.

3BX is not aimed to 'satisfy the compiler'. Still, the compiler has to figure out every little detail, like variables and the type of each variable.

3BX compiles to machine code using LLVM, optimizing for the highest performance possible.

## Syntax Basics

### Indentation

Indents are done with either tabs or spaces. A source file has either a fixed amount of tabs or spaces per indent.

### Special Characters

Code should use as few special characters as possible (so custom patterns can use them). Reserved characters:

- `:` at the end of a code line indicates a section
- `#` indicates a comment
- `"` starts and ends strings
- `{`, `}` capture syntax inside of pattern definitions {expression:capturedexpression}
- `[`, `]`, `|` optional syntax inside of pattern definitions [option1|option2|]

## Sections

Sections define and use patterns. See [SECTIONS.md](SECTIONS.md) for the complete reference of section types and their required subsections.

### Definition Sections

These add new syntax to the language:

- `expression <pattern>:` can be set, get, added to, etc. (requires `get:` subsection)
- `effect <pattern>:` executes code (requires `execute:` subsection)
- `section <pattern>:` defines a named section (requires `execute:` subsection)
- `class <pattern>:` defines a class type (has `patterns:` and `members:` subsections)

<pattern> is not actual syntax, but represents a pattern like the example pattern below (`print [a|the] message`)

## Patterns

### Pattern Syntax

Patterns use brackets `[]` with `|` to define alternatives.

**Required choice** - `[a|b]` means the user MUST choose one:

```
print [a|the] message:
```

This matches:
- `print a message`
- `print the message`

But NOT: `print message` (must have "a" or "the")

**Optional elements** - Use empty alternative `[|word]`:

```
print [|the] message:
```

This matches:
- `print message`
- `print the message`

### Space Normalization

Spaces in parsed patterns are normalized:
- Double spaces become single spaces
- Leading/trailing spaces are removed

So `" print  the   message "` becomes `"print the message"`.

### Pattern Tree Optimization

The pattern tree should join branches after optional segments to avoid having 200 branches for one pattern with 8 optional segments. When another pattern also uses the same branch, it should split them. Otherwise issues like these occur:

```
give [|some] bread to dave
give some bread to jane  # no optional segment! but since the tree wired
                         # the end of [|some] back to the end of give,
                         # "give bread to jane" will compile incorrectly
```

In that case, split the branches: duplicate `bread to dave` to the `some` branch.

### Returning Values

To return a value in an expression, use either `set result to x` or `return x`.

## Typed Captures

Patterns can use typed captures to control how arguments are matched:

| Syntax | Matching | Use Case |
|--------|----------|----------|
| `$` or plain word | Greedy expression | Most arguments |
| `{expression:name}` | Greedy, lazy evaluation | Control flow (while, if) |
| `{word:name}` | Non-greedy, single identifier | Member access, reflection |

### Expression Captures

`{expression:name}` captures an expression without evaluating it. The expression is re-evaluated each time it's used, in the caller's scope:

```
section while {expression:condition}:
    execute:
        @intrinsic("loop_while", condition, the caller's child section)
```

### Word Captures

`{word:name}` captures a single identifier as a string (non-greedy):

```
expression {word:member} of obj:
    get:
        @intrinsic("member_access", obj, member)
```

This matches `x of player` where `member` = "x" (as a string, not a variable reference).

## Intrinsics

Intrinsics are primitive operations that bridge patterns to LLVM. They use the `@intrinsic("name", args...)` syntax.

See [INTRINSICS.md](INTRINSICS.md) for the complete reference.

## Testing

To see how well the compiler is doing, use `run_tests.sh`. For now it isn't bad if the tests fail, but implement the missing features one by one.

DO NOT modify any tests in the `tests/required` folder. You may modify all files in the `tests/agents` folder to test out things.
