# Welcome to 3BX

**3BX** is a programming language where syntax is defined in the language itself. Instead of hardcoded keywords, you write patterns that teach the language new commands.

## Philosophy

Traditional compilers bake syntax into the compiler. 3BX bakes syntax into the language:

- The compiler only understands structure (indentation, strings, numbers, words)
- All syntax comes from patterns defined in `.3bx` files
- Users can define new syntax without modifying the compiler
- The language is self-documenting (read `prelude.3bx` to understand syntax)

## What Does 3BX Code Look Like?

```
set x to 5
print x + 3
```

This works because `prelude.3bx` defines patterns like:
```
effect set var to val:
    execute:
        @intrinsic("store", var, val)

effect print msg:
    execute:
        @intrinsic("print", msg)
```

## The Power of Patterns

You can teach 3BX new commands by defining patterns:

```
effect say hello to name:
    execute:
        print "Hello,"
        print name
```

Now `say hello to Alex` is valid syntax.

## Quick Links

| Guide | What You'll Learn |
|-------|-------------------|
| [Getting Started](Getting-Started.md) | Install 3BX and run your first program |
| [Writing Your First Program](Writing-Your-First-Program.md) | Learn the basics step by step |
| [Patterns](Patterns.md) | Create your own custom commands |
| [Intrinsics](INTRINSICS.md) | Primitive operations reference |
| [Examples](Examples.md) | See what you can build |

## Why 3BX?

1. **Extensible** - Define new syntax in the language itself
2. **Readable** - Code looks like natural language
3. **Powerful** - Compiles to native code using LLVM
4. **Simple Compiler** - The compiler is just pattern matching

## Quick Example

```
# Define a custom pattern
effect greet person:
    execute:
        print "Hello,"
        print person

# Use it
greet "World"
```

Ready to start? Head over to [Getting Started](Getting-Started.md)!
