This document explains how the compiler is supposed to behave.

All code should be as DRY, agnostic, user-friendly, and performant as possible. Do more with less code.

When we encounter an error in the users code, we add an error diagnostic and return `false` if this error could cause dependent errors. When a child function returns `false`, return `false` as well. This will make the compiler exit cleanly with a single diagnostic. We do not continue scanning for other diagnostics, because one error will cause lots of other errors most of the time and make it unclear for users what they need to focus on to fix.

When we encounter an error in the compilers code, the compiler should SCREAM. crash with crashCompilerBug.

Later stages of the compiler are very dependent on earlier stages. Every line of code has to be carefully thought out.

# Import Stage

The compiler combines all files into one large file.

# Parse Stage

Sections are analyzed. We do basic parsing **WITHOUT hardcoding**.

- Which line opens a new section?
- What patterns does each section have?
- We parse inline sections and multiline statements (f.e. statements with multi line arrays) too, here.

Integer literals are parsed exactly rather than through floating-point storage.
Magnitudes through `2^63` are retained so unary negation can form the signed
64-bit minimum; larger magnitudes are rejected here. The boundary magnitude is
rejected after inference unless ordinary intrinsic evaluation records that
unary negation consumed it without another value operation using it. Those
effects are committed with the inferred expression, so rejected overload and
operand-grouping trials cannot affect the selected expression. Unreachable
bodies remain semantically uninferred and therefore do not create boundary-use
effects. Explicit floating-point literals remain 64-bit floating-point values.

# Pattern Matching Stage

Patterns are matched. Here we identify:

- What is a variable
- What is an argument

All pattern definitions are stored in a pattern tree, a trie structure containing pattern elements.

We match with multiple iterations. This makes sure that patterns earlier in the file can call functions later in the file.

We discover what is a variable based on these principles:

- A single-word function pattern is never a variable. Therefore, all functions with single-word patterns are parsed in the first round.
- Words authored as `[word]` or `{literal:word}` are always literal and are never eligible for implicit parameter promotion.
- A single word as an argument to an intrinsic is always a variable, unless it references a single-word function. Since functions are parsed before references to them, we are guaranteed that single-word functions exist from the start.
- Alphanumeric strings in argument positions of pattern calls are variables.
- `{name}` explicitly introduces or references a variable. The name can contain multiple words separated by spaces.
- `{type:name}` does the same and constrains the variable to values accepted by the type expression. This syntax works both in pattern definitions and in executable code.
- After a multi-word name has been introduced explicitly, later unbraced occurrences of that full name are matched as one variable in the same lexical scope and its children.

A square-bracket group without a top-level `|` is an explicit literal and is flattened into its ordinary literal elements.
`{literal:text}` produces the same representation. Square-bracket groups with alternatives remain choice elements.

Capture names are type-agnostic unless a type expression appears before the colon. There is no lexical word-capture type; finite concepts such as shader stages are represented by literal patterns, and text values use an ordinary string type constraint.

We use this logic to determine what is a variable and what is not, all the way from the simplest intrinsics to the most complex functions.

The consequence: an unused argument is not an argument.

Pattern matching is type-agnostic. This is because we cannot easily match based on types if we do not even know whether a variable exists yet. Also, variables come from the callee to the caller (the function signature defines what is a variable), while types come from the caller to the callee (the arguments define the type).

The consequence: we cannot know what order nested expressions should have.

Example:

```text
print value as a line
```

We do not know that `print value` returns `nothing` and cannot be used as an argument for `as a line`.

Since we are fully agnostic, we will make all left expressions subexpressions:

```text
(print value) as a line
((value + value) + value) + value
```

Pattern elements are not splittable, except for `Other` tokens in pattern references.
for example, 5*-3 should result in -15. therefore, we match for '*-' first. that doesn't work, so we match for '*' after and submatch, using the '-' in the submatch.

Intrinsic arguments are **ALWAYS** stored as `[name, arg1, arg2]`, etc. So the left operand of `+` in the `add` intrinsic is `[1]` and the right operand is `[2]`.

We sort all expression arguments by their source position, since they did not get added in order. After this, **NO** sorting is done.

# Type Resolution Stage

Before type resolution starts, the compiler initializes the selected target's LLVM data layout. This does not generate or
reorder code. It supplies the target ABI facts needed by compile-time operations such as `size of`; later code generation uses
the same module and layout.

We loop over the code like it would get executed.

Before inferring executable code, we infer every pattern argument type constraint with the same expression inference engine.
Pattern matching has already recorded all candidates, but it has not selected an overload. Signature inference resolves
constraint dependencies first, selects overloads from inferred argument types, and requires every constraint expression to
produce a pure compile-time type or constraint value. A constraint is deferred while one of its candidate signatures is unresolved. If a full
pass makes no progress, the remaining signature dependency is cyclic and compilation fails.

Exact expression types and parameter requirements use separate models. `DataType` describes one exact expression type.
`TypeConstraint` describes the structural domain accepted by a pattern parameter and may additionally require a compile-time-known
value. The `constraint` meta-type carries `TypeConstraint` values just as the `type` meta-type carries type-reference values.
`@intrinsic("fix", value)` converts a type or constraint value into the same structural constraint with its compile-time-known
requirement enabled. Type and constraint shaping uses the same surface patterns; shaping a constraint preserves the constraint
category. A compile-time-known requirement is not an overload axis, so definitions which differ only by `fix` are duplicates.
Type values retain both meanings when used in a signature: their constraint view controls overload matching, while their exact type
view declares a concrete runtime representation when one is required for a callable ABI. For example, `integer` accepts every integer
width as a constraint but denotes the default integer width as a standalone type value. Code generation consumes that recorded exact
view; it does not reinterpret the constraint.

An integer literal uses a 32-bit integer type when its exact value fits and a
64-bit integer type otherwise. Its compile-time value remains an exact signed
integer through inference and pure evaluation; it is never routed through a
floating-point representation.

After all constraints are concrete, we validate overlapping overload domains. Only then can normal call inference select
overloads. Declaration order never selects an overload.

this implies:
 - functions and classes are instantiated on USAGE. for functions when they're called, for classes with the construct intrinsic. so if a class or function is never used, NO instantiation is created.

The compiler NEVER expands flexes to look for something like an intrinsic. instead, the compiler walks the code normally and once intrinsics are encountered, it does something with them.

Control-flow classification is an outcome of that normal inference walk. A control-flow intrinsic emits a typed section outcome, and a flex call may forward the outcome produced by its inferred replacement. A section flex forwards the outcome of its final top-level replacement statement after the preceding statements have executed. A function flex forwards a control-flow outcome only when its replacement is a single expression; control flow in a multi-line function flex remains internal to that function flex. When replacement inference transfers caller-body control, the outcome is recorded on the exact replacement expression that performed the transfer and enclosing flex calls forward it normally. A direct outcome already recorded on an active flex call takes precedence over the replacement expression's forwarded outcome. Transfer identity is the complete active flex-invocation path plus the transfer expression, with reusable clones normalized to their template nodes. This lets retries of one logical invocation share the inferred caller body while distinct direct, helper, and nested-helper call sites produce the required duplicate-transfer diagnostic. The section walker consumes outcomes in execution order: infer an `if` header, infer its reachable body, infer the next alternative header, then infer that reachable body. Before merging the fallthrough state of a conditional, the following section header may be trial-inferred through the ordinary inference transaction to determine whether its outcome continues the same chain. This uses the produced outcome only; it never inspects pattern text or searches an expansion for a particular intrinsic. Flex sections cannot be overloaded, so selecting an overload cannot create a control-flow classification dependency.

The section walker records finalized fallthrough metadata on every top-level expression it reaches. An expression without that metadata is outside the inferred execution paths and remains intentionally uninferred. Code generation consumes this metadata before inspecting an expression, so it emits only paths whose overloads, types, and control flow were finalized by inference.

A section flex replacement is executed line by line in source order during inference, compile-time execution, and code generation. `execute body` transfers execution to the caller body at that exact point; if neither an outcome nor an executed `execute body` consumes it, the caller body executes after the replacement. Variables introduced by the active replacement scopes are bound structurally while the caller body runs, then those bindings end when control returns to the replacement. These rules use the already-selected flex definition, inferred replacement body, and section ownership stacks. They do not rediscover behavior from pattern text.

We track each variable that could possibly be a constant. A variable reference can be constant. Constant means compile-time-known here. It does not guarantee that the value does not change later. Execution-state maps retain an explicit unknown entry after a write or control-flow merge invalidates a previously known value, so an older expression value cannot reappear as the current variable value.

Writing a concrete value through storage whose nested class subtype is still unspecified refines that storage type. Function instantiations record refinements to parameter storage and apply them to the caller, so a generic container can bind its element subtype on its first write without losing it at the function boundary. This only completes unspecified structure; it never changes an already concrete member type.

We can reorder expressions based on types if this is the first valid instantiation, but we cannot change what is a variable and what is not.

**ALL** types of each previous line have to be deduced right away, except in recursive function code. we loop over the function code again if not all types were resolved. if no progress is being made, we emit diagnostics.

Each loop pass walks its body once in execution order. Loop inference joins the entry state with the state produced by one body pass, then repeats the ordinary header and body walk until constants and the subject reach a fixed point. A statically true first header uses the first iteration's result as the fixed-point entry because that iteration is guaranteed; an unknown first header also retains the zero-iteration entry path. Assignments remain precise within each pass, and only values which actually differ across reachable iteration counts become unknown. A body proven unreachable by the header is not inferred and does not contribute variable, subject, or return-type state.

We infer top-down, left to right. So we infer the top-level expression. Before inferring it, we infer the arguments. If those arguments are expressions with arguments as well, no problem, since we infer recursively.

The `store` intrinsic does not break this, since `store` should always be used left of where the value is used.

Example:

```dynlex
set count to 1 and increment count
```

We infer:

```dynlex
{nothing:first} and {nothing:second}
```

We infer the arguments first, so we infer `first` and `second` after. `count` has a type when we get to `second`.

- We infer `count`. If `count` is compile-time-known, we set its value to that value. If it is unset, we keep it unset but do not emit an error yet. The intrinsic checking will do that.
- We infer `1`. We set the compile-time value to the value of the literal.
- Now we infer the `set variable to value` flex.
- We infer the `store` intrinsic. We resolve `variable` and `value`.
- We set the value of `variable` to `1`.
- We set the result of this `store` intrinsic to `nothing`.
- We infer `increment`.
- We infer the second `count`, which reads `1` from the variable.
- We resolve `value`.
- Etc.

So we build up compile-time values hierarchically: from the bottom of the tree to the top (a natural result of top-down inference while inferring the arguments first).

When we encounter a value that cannot be known at compile time, values that build on it also cannot be known at compile time. For those, we track only types, not values.

When processing a function call, we infer that function right away so we can know return types. We do the same with flexes. When a (flex) function fails on typing, we just reorder the expression that is calling it, since we are still inferring that one.

after we have successfully inferred a function, if it is a pure function and all arguments are compile time known. we execute the function in compile time and retrieve the result from it. evaluating a pure function shouldn't modify anything, only give a compile time value as result.

`(print value) as a line` is incorrect, since `nothing` as an argument is not allowed unless explicitly specified in the pattern, and `print value` returns `nothing`.

We know this because we instantiate `print value` and walk over the code just like we do with the code in the main section. We store the return type so we do not have to instantiate functions with the same (possibly incorrect) combinations again and again. We assume functions always return the same type for the same argument types and constants.

All instantiations of a function have the same operand reordering for each code line, but can use different overloads. The first valid instantiation determines reordering.

We reuse the same strategy (code) for flex functions where possible, keeping it DRY.

## Operand Reordering

We iterate over all possible operand orders until we find a valid one.

All instantiation types are known when instantiating a function and do not change. We use this fact for reordering.

The only exception is the `store` intrinsic: it takes a value whose first argument's type should be determined by its second argument's type. Therefore, a `store` intrinsic wrapper has to be a flex function, so it is executed in the outer context.

We prefer sub-first, aka `left = subexpression`, just like pattern matching.

A function's return value may never be unused. This prevents wrong groupings. Public value-producing patterns therefore use noun, adjective, or predicate phrases whose values are consumed by their surrounding sentence. Imperative action patterns return `nothing`. Only replacement-level compiler or library code may intentionally drop a raw intrinsic result with the `discard` intrinsic.

Enclosed expressions like `1 + 4` in `the minimum of 1 + 4 and 5 + 6` are inferred first. When an ordering in the parent expression fails, different orderings in enclosed expressions are tried.

So:

```text
(x + y squared) * z + 2
```

Initial sub-first order:

```text
(((x + y) squared) * z) + 2
```

Inner increment:

```text
((x + (y squared)) * z) + 2
```

All inner orderings have been tried. All inner orderings reset. Outer increment:

```text
((x + y) squared) * (z + 2)
```

Inner increment:

```text
(x + (y squared)) * (z + 2)
```

With multiple separate subexpressions, iterate over `1` first, reset `1` and step `2`, iterate `1` again, step `2` again, and when `2` is finished reset both and do the same with the outer expression.

So, outer/inner-right/inner-left:

```text
000
001
010
011
100
101
etc.
```

For every increment, we need to reset and revalidate the whole expression tree. This is because some intrinsics have side effects.

For example, the `store` intrinsic has a side effect:

```text
set destination to source and print destination
```

The second `destination` type is not known at first and should be set by the `load` intrinsic.

Therefore, we sadly cannot just use a recursive "increment until next valid option."

Therefore, we have to separate increments, validation, and clearing.

- An increment changes a grouping somewhere (or potentially multiple groupings, as long as groupings have finished iterating).
- Validation infers the whole tree recursively.
- Clearing clears all types from the tree recursively.

Grouping candidates are produced by a pull enumerator. It suspends after one candidate, fully unwinds the native generation stack,
and lets validation run before it resumes. It never stores all possible candidates. The suspended state contains only the current
choice indices and one compact expression-pointer layout for the active candidate, because validation may temporarily apply another
layout to the same expression nodes.

To detect ambiguity, we have to keep incrementing until we find another fully passing tree or finish. When encountering the first valid state, we save this state by saving the expression pointers.

A successful candidate keeps its complete inference transaction alive while the pull enumerator checks whether another candidate exists. If the enumerator finishes, that final successful transaction is promoted directly, including all nested subgrouping transactions; the one-candidate case follows the same path. If another candidate exists, the retained transaction is rolled back before that candidate is inferred. A later successful candidate with the same local ordering replaces the retained transaction, so the final accepted subgroupings are promoted together.

We do not clone the expression tree for reordering; we reorder it. Even when storing the correct state and continuing to search for the next valid state so we can give ambiguity warnings, we store our choices instead of cloning the expression tree.

reusable expression trees are cloned only to own per-usage inference state: once for each function instantiation and flex expansion. the original tree is unmodified.

We do not use pointers to expression-pointer locations (`expression**`), since those can be dangling pointers.

**ALL** functions can be seen as one of these 4 categories of operators.

The **ONLY** thing we check for this is:

- Does the operator start with an argument?
- Does it end with an argument?

All other arguments are **IRRELEVANT**. We can never start or end with 2 arguments, since `$$` would just merge into one name when the user supplies two concatenated names `arg1` and `arg2` as `arg1arg2`. A space (` `) is a **SEPARATOR**, a pattern element of type `other`.

Prefix operator:

```text
the inverse of $
set $ to $
vector of $ $ #
```

Postfix operator:

```text
$ doubled
$%
```

Infix operator:

```text
$ + $
```

Other operator:

```text
true
a $ bit integer
```

Some orderings are ambiguous.

For example:

```text
the maximum of 5 and 3 + 4
```

Did the user mean `(the maximum of 5 and 3) + 4` or `the maximum of 5 and (3 + 4)`? Both parse correctly. Because of the left-expression-equals-subexpression nature, the compiler will choose the first one. This behavior might cause glitches. But even humans will not be able to tell without context. We would like the user to specify which option to choose.

# Code Generation Stage

We already know which patterns call which instantiations, the type of every variable, etc. But now, we branch off into compilation target: browser, machine code, SPIR-V, etc.

External call signatures come entirely from their inferred DynLex operands; code generation does not identify or special-case
library function names. `@intrinsic("call", library, function, return type, arguments...)` emits a fixed signature.
`@intrinsic("variadic call", library, function, return type, fixed argument count, arguments...)` emits the first
`fixed argument count` argument types as the fixed prefix and marks the remaining parameters variadic. Only the variadic suffix
receives C's default argument promotions: booleans and integers narrower than 32 bits become 32-bit integers, 32-bit floats become
64-bit floats, and pointers remain pointers. Platform-sized C types are expressed by standard-library type patterns built from
compile-time build information.

SPIR-V vertex-to-fragment interpolants are identified by arbitrary non-empty semantic names. Each independently compiled shader
stage encodes those names into valid interface identifiers and assigns locations in lexical semantic-name order, so matching
vertex outputs and fragment inputs receive identical locations regardless of source order. The standard shader library exposes
the four floating-point components while the intrinsic layer owns the stage restrictions and interface storage classes.

# LSP Interaction

When hovering over an expression, the already-evaluated compile-time value and type are shown.
