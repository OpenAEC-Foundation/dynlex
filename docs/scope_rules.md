# Scope rules

A scope rule states a property of everything written in one lexical scope. Rules
remove repeated annotations while keeping the source plain English.

```dynlex
in this file, by default:
    numbers are 64 bit floating-point
    functions are local
```

## Blocks

A block opens with a scope phrase and an optional default marker:

| Header | Scope |
|---|---|
| `in this file:` | the source file containing the block |
| `in this class:` | the class section containing the block |
| `in this function:` | the function, conversion or section definition containing the block |

Appending `, by default` makes the block a default block. Otherwise it is a rule
block.

- A **default block** fills in what code leaves unspecified. Explicit code wins silently.
- A **rule block** fills in the same way. Explicit code that contradicts the rule still wins, but produces a warning
  pointing at the rule.

The scope phrase identifies the source file or nearest enclosing class or function,
conversion or section definition. An ordinary `execute` or `replacement` body is
transparent: function rules may be written directly in that body, including with
`to` and `to get` shorthand.

Rules apply to their entire named scope, including code and definitions preceding
the block. They are collected before locality and number constraints are applied;
they are never executed. A rule inside a loop, conditional or other executable
nesting produces a warning and still applies even if that code is never reached.
A file rule inside a function also warns and still applies to the whole source file.
A class or function rule without a corresponding enclosing definition is an error.

Each scope has at most one rule block and one default block. The two blocks of a
scope cannot state the same property, even when declared in different nested bodies.

Rules are lexical. Inner scopes override outer scopes per property: function, then
class, then file. A rule never affects an imported file, and flex replacements keep
the rules of the file that defines them, while arguments keep the rules of the call
site.

All header and locality phrases are configured in `lib/config.dl`; number rules are
ordinary patterns in `lib/std.dl`.

## Locality

| Line | Effect |
|---|---|
| `functions are local` | functions are local unless marked `public` |
| `classes are local` | classes are local unless marked `public` |
| `sections are local` | sections are local unless marked `public` |
| `conversions are local` | conversions are local unless marked `public` |
| `everything is local` | all four of the above |

Locality is decided after section parsing collects all rules, so these lines are
syntax rather than patterns.

- `public` marks a definition visible from other files. It is an error when no rule makes that kind of definition local:
  the modifier would be redundant.
- `local` is an error when a rule already makes that kind of definition local.
- In a rule block, `public` additionally produces a warning.

## Numbers

`numbers are {properties}` is a pattern in `lib/std.dl`. The bare word
`numbers` remains available as a variable name. The properties are number
constraints joined like any type phrase, e.g. `64 bit floating-point`,
`unsigned`, `32 bit`, `integer`. A number has three independent properties:

| Property | Values |
|---|---|
| kind | integer, floating-point |
| sign | signed, unsigned (integers only) |
| width | 8, 16, 32 or 64 bit |

### What a number rule applies to

A number rule fills the unspecified properties of every number that is written in
its scope.

**Numeric literals.**
- A literal without a decimal point leaves kind, sign and width unspecified.
- A literal with a decimal point or exponent specifies the floating-point kind.
- Built-in defaults apply after scope rules: integer literals use signed 32 or 64 bits when possible,
  unsigned 64 bits through its range, and then a power-of-two signed width of at least 128 bits.
  Floating-point numbers are 64 bit, or 32 bit on GPU targets.

**Number type phrases**, such as `a number`, `an integer` or `a floating-point number`, in signatures, casts and
other type positions.
- `{a number:x}` in a file whose numbers are 64 bit floating-point accepts exactly 64 bit floating-point numbers.
- `x as a number` converts to that type.

### Literals that do not fit

When a rule would give an integer literal a type that cannot represent it exactly,
the literal uses the next larger type of the same kind and the compiler warns:

- a 32 bit integer that does not fit becomes 64 bit, with larger integers widening further as needed;
- an integer above `2^24` that would become a 32 bit floating-point number becomes 64 bit.

When no larger type can represent the value exactly, the literal is an error. For example,
an integer with more than 53 significant binary digits cannot become a 64 bit floating-point
number exactly. Integer widths are bounded by LLVM's integer type limit.

### Contradicting a rule

In a rule block (no `, by default`), explicit code whose number properties
contradict the rule wins and produces a warning. Examples are a decimal literal under
`numbers are integer`, or an `{integer:x}` parameter under
`numbers are floating-point`.

### Redundant rules

A number rule that adds no property, such as `numbers are a number`, is a warning.

### Diagnostics

An error that involves a number whose type came from a rule carries a note pointing at that rule.

## Number type phrases

Number types are built from properties joined with `and`:

Width properties constrain numeric kinds. They cannot be combined with a
Boolean or another nonnumeric kind. Pointer shaping preserves the numeric
restriction on the pointee: a pointer to an integer accepts signed and unsigned
integer pointers, and a pointer to `32 bit` accepts 32-bit numeric pointers.

```dynlex
[an|] integer means: @intrinsic("type", "integer")

unsigned means: @intrinsic("type", "uint")

[a|an|] unsigned {properties} means: @intrinsic("and", unsigned, properties)
```

`and` of two type or constraint values accepts exactly the values both accept. The result keeps an exact type view
whenever the properties determine one: a kind, with its default width when no width is given. This is why
`x as a 64 bit floating-point number` and `x as an integer` keep working.

`integer` accepts signed and unsigned integers of every width. As an exact type it is a signed 32 bit integer, because
integers are signed by default. `signed` and `unsigned` restrict the sign.

Plural forms such as `numbers` are aliases written for readers. The compiler
gives them no special meaning.
