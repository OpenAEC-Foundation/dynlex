# Named compile-time values

DynLex does not need a separate enum or constant declaration for named values.
A zero-argument function with a `replacement` body is a named compile-time
value when that replacement is compile-time known:

```dynlex
function the GLFW press status:
    replacement:
        1
```

The name can be used anywhere an ordinary value can be used. The compiler
infers the replacement and propagates its compile-time value, so it can also
be passed to a parameter requiring a fixed value:

```dynlex
function the selected value for {fixed integer:value}:
    execute:
        return value

print the selected value for the GLFW press status as a line
```

This is the same general pattern used by the JSON, LSP, workspace, path, and
Unicode libraries. External-library enum values belong in the library that
owns the binding and should be referenced by their descriptive DynLex names,
not repeated as numeric literals at each intrinsic call site.
