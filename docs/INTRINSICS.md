# Intrinsics Reference

Intrinsics are the primitive operations that bridge 3BX patterns to LLVM. They are called using the `@intrinsic("name", args...)` syntax.

## Arithmetic

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `add` | `left`, `right` | number | Addition: `left + right` |
| `sub` | `left`, `right` | number | Subtraction: `left - right` |
| `mul` | `left`, `right` | number | Multiplication: `left * right` |
| `div` | `left`, `right` | number | Integer division: `left / right` |
| `sqrt` | `value` | number | Square root |

### Example
```
expression left + right:
    get:
        return @intrinsic("add", left, right)
```

## Comparison

All comparison intrinsics return `1` (true) or `0` (false).

| Intrinsic | Arguments | Description |
|-----------|-----------|-------------|
| `cmp_eq` | `a`, `b` | Equal: `a == b` |
| `cmp_neq` | `a`, `b` | Not equal: `a != b` |
| `cmp_lt` | `a`, `b` | Less than: `a < b` |
| `cmp_gt` | `a`, `b` | Greater than: `a > b` |
| `cmp_lte` | `a`, `b` | Less than or equal: `a <= b` |
| `cmp_gte` | `a`, `b` | Greater than or equal: `a >= b` |

### Example
```
expression val1 < val2:
    get:
        return @intrinsic("cmp_lt", val1, val2)
```

## Variables

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `store` | `var`, `value` | void | Store `value` in variable `var`. Creates the variable if it doesn't exist. |
| `load` | `var` | value | Load the value from variable `var`. |
| `return` | `value` | value | Return a value from the current pattern. |

### Example
```
effect set var to val:
    execute:
        @intrinsic("store", var, val)
```

## Output

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `print` | `value` | void | Print `value` to stdout with a newline. Handles integers, floats, and strings. |

### Example
```
effect print msg:
    execute:
        @intrinsic("print", msg)
```

## Control Flow

### Conditional Execution

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `if` | `condition`, `section` | void | Execute `section` only if `condition` is true (non-zero). |
| `loop_while` | `condition`, `section` | void | Repeatedly execute `section` while `condition` is true. The condition is re-evaluated before each iteration. |
| `execute` | `section` | void | Execute a section immediately. |
| `exit` | `section` | void | Skip remaining code in `section` and continue after it. |

### Example
```
section if condition:
    execute:
        @intrinsic("if", condition, the caller's child section)

section loop while {expression:condition}:
    execute:
        @intrinsic("loop_while", condition, the caller's child section)
```

### If/Else Chains

The chain intrinsics manage if/else-if/else chains at compile time. They track which branch has executed to prevent multiple branches from running.

| Intrinsic | Arguments | Description |
|-----------|-----------|-------------|
| `chain_start` | `name`, `section` | Start a new chain with the given name. Sets the active chain on the parent section. |
| `chain_continue` | `name`, `section` | Continue an existing chain. Inserts `chain_exit` calls at the end of preceding sections. |
| `chain_exit` | `section` | Exit the chain. Scans following sections for `chain_continue` calls and skips them. |

### How Chains Work

1. `chain_start("if", section)` marks the parent section with `activeChain = "if"`
2. When a branch executes, it calls `chain_exit` which marks the chain as complete
3. `chain_continue("if", section)` checks if the chain is still active before executing

### Example
```
section if condition:
    execute:
        start chain "if"
        execute if condition:
            execute the caller's child section
            exit chain

section else if condition:
    execute:
        continue chain "if"
        execute if condition:
            execute the caller's child section
            exit chain

section else:
    execute:
        continue chain "if"
        execute the caller's child section
        exit chain
```

## Frames and Sections

Frames represent call stack entries. Sections represent indented code blocks.

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `frame` | `depth` | frame ref | Get a frame reference. `0` = current frame, `1` = caller, etc. |
| `section` | `frame`, `offset` | section ref | Get a section relative to a frame. `-1` = child section, `0` = current section, `1` = parent section. |

### Frame Depth

When you define a pattern, its `execute:` or `get:` block runs in a new frame:
- `0 frames back` = the pattern's own frame
- `1 frame back` = the frame that called this pattern (the caller)
- `2 frames back` = the caller's caller

### Section Offset

Sections are navigated relative to a frame's call site:
- `-1 sections up` = the child section (indented block after the colon)
- `0 sections up` = the section containing the call
- `1 sections up` = the parent of that section

### Example
```
expression the caller:
    get:
        return @intrinsic("frame", 2)  # +1 because this pattern creates a frame

expression the caller's child section:
    get:
        return @intrinsic("section", the caller, -1)
```

## Lazy Evaluation

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `evaluate` | `expr` | value | Evaluate a lazy expression captured with `{expression:name}` syntax. |

Lazy expressions are captured without evaluation and re-evaluated each time `evaluate` is called, in the caller's scope.

### Example
```
expression evaluate {expression:expr}:
    get:
        return @intrinsic("evaluate", expr)

# Used for ternary: the branches are lazy so only one is evaluated
expression {expression:truevalue} if condition else {expression:falsevalue}:
    get:
        if condition:
            return evaluate truevalue
        else:
            return evaluate falsevalue
```

## Lists

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `length` | `list` | integer | Get the number of items in a list. |
| `item_at` | `list`, `index` | value | Get the item at the given index (0-based). |

### Example
```
expression amount of items in list:
    get:
        return @intrinsic("length", list)

expression item at index i of list:
    get:
        return @intrinsic("item_at", list, i)
```

## Classes

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `create_instance` | `className` | object | Create a new instance of a class. |
| `has_member` | `obj`, `memberName` | boolean | Check if an object has a member with the given name. |
| `member_access` | `obj`, `member` | value | Get the value of a member. |
| `member_set` | `obj`, `member`, `value` | void | Set the value of a member. |
| `members` | `obj` | list | Get a list of all member names. |

### Example
```
expression new className:
    get:
        return @intrinsic("create_instance", className)

expression {word:member} of obj:
    get:
        return @intrinsic("member_access", obj, member)
```

## Async

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `run_in_background` | `section` | void | Execute a section asynchronously in a background thread. |

### Example
```
section run in background:
    execute:
        @intrinsic("run_in_background", the caller's child section)
```

## Compile-Time

| Intrinsic | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `assert_compile_time` | `condition` | void | Assert that a condition is true at compile time. Emits an error if false. |

### Example
```
effect verify that {expression:cond}:
    when parsed:
        @intrinsic("assert_compile_time", cond)
```

## Not Yet Implemented

The following intrinsics are planned but not yet working:

| Intrinsic | Description |
|-----------|-------------|
| `call` | FFI: Call external C library functions |
| `and`, `or`, `not` | Logical operators |
| `concat` | String concatenation |
| `to_string` | Convert number to string |

Type-related intrinsics (`primitive_type`, `type_check`, `cast`, etc.) are also not implemented - types are currently deduced by the compiler.
