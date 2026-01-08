# Patterns - Create Your Own Syntax

Patterns are how 3BX defines syntax. All language constructs - from `set x to 5` to `print "hello"` - are patterns defined in `.3bx` files.

## How Patterns Work

When you write `set x to 5`, the compiler:
1. Tokenizes into: `IDENTIFIER("set") IDENTIFIER("x") IDENTIFIER("to") NUMBER(5)`
2. Matches against registered patterns
3. Finds `effect set var to val:` from prelude.3bx
4. Executes its body with `var=x` and `val=5`

The compiler has no hardcoded keywords. Words like "set" and "to" only have meaning because they appear in pattern definitions.

## Pattern Types

### Effect Patterns (Do Something)

Effects perform actions:

```
effect greet name:
    execute:
        print "Hello,"
        print name
```

Usage: `greet "Alex"`

### Expression Patterns (Calculate Something)

Expressions return values:

```
expression double of x:
    get:
        return x * 2
```

Usage: `set result to double of 5`

## Pattern Syntax with Alternatives

Use brackets `[]` with `|` to define alternatives:

### Required Choice

`[a|b]` means the user MUST choose either `a` or `b`:

```
effect set [the|a] variable to val:
    execute:
        @intrinsic("store", variable, val)
```

This matches:
- `set the variable to 5`
- `set a variable to 5`

But NOT: `set variable to 5` (must have "the" or "a")

### Optional Elements

Use an empty alternative `[|word]` to make something optional:

```
effect print [|the] message:
    execute:
        @intrinsic("print", message)
```

This matches:
- `print message`
- `print the message`

### Multiple Options

```
effect show [the|a|an] item:
    execute:
        @intrinsic("print", item)
```

Matches: `show the item`, `show a item`, `show an item`

### Space Normalization

Spaces in patterns are normalized:
- Double spaces become single spaces
- Leading/trailing spaces are removed

So `print  the   message ` becomes `print the message`.

## Variables in Patterns

Variables are deduced from `@intrinsic` calls:

```
effect set var to val:
    execute:
        @intrinsic("store", var, val)
```

The arguments to `@intrinsic` (`var`, `val`) are identified as variables. Everything else in the syntax (`set`, `to`) becomes a literal that must match exactly.

## Intrinsics

Intrinsics are primitive operations the compiler understands. They use the `@intrinsic("name", args...)` syntax.

See [INTRINSICS.md](INTRINSICS.md) for the complete reference.

## Priority

When patterns could conflict, use `priority:` to specify order:

```
expression left * right:
    priority: before left + right
    get:
        return @intrinsic("mul", left, right)
```

This ensures `*` binds tighter than `+`.

## Complete Example: Custom Math

```
# Natural language addition
expression add a and b:
    get:
        return @intrinsic("add", a, b)

# Natural language subtraction
expression subtract b from a:
    get:
        return @intrinsic("sub", a, b)

# Use them
set result to add 10 and 5
print result    # 15

set diff to subtract 3 from 10
print diff      # 7
```

## How Pattern Matching Works

1. The compiler collects all patterns from imported files
2. For each statement, it tries to match against registered patterns
3. The most specific pattern wins (more literal words = more specific)
4. If two patterns match equally, the compiler reports an error

## Tips

1. **Make patterns readable** - They should sound like natural sentences
2. **Use descriptive variable names** - `person_name` not `n`
3. **Keep patterns focused** - Each pattern should do one thing
4. **Test as you go** - Try your pattern immediately after writing it

## What's Next?

- [Examples](Examples.md) - See patterns in action
- [Writing Your First Program](Writing-Your-First-Program.md) - Back to basics
