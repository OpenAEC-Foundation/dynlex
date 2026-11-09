# Declarative sections

Sections can produce data instead of executing their indented contents. Their grammar,
body constraints, representation, and behavior are defined in DynLex libraries. The
compiler has no built-in window, table, style, or state-machine syntax.

UI descriptions and their native/browser renderer live in the separate `dynlex-ui`
repository. The compiler provides the general section and callback mechanisms.
See `dynlex-ui/docs/sections.md` for the window grammar and examples.

## Define a section

`lib/sections.dl` supplies ordinary patterns for `many`, `the body`, and static references.
A minimal declaration is:

```text
import lib/sections.dl

section numbers {name}:
    declares: name
    body:
        items: many integer
    value:
        the body's items

numbers example numbers:
    1
    0
    4

print line the item at 2 in example numbers
```

The final line prints `4`. The section value is an array. There is no runtime builder
loop or sequence of assignments to array slots.

| Metadata | Meaning |
| --- | --- |
| `declares: name` | Binds a header capture as the resulting data's name. The value block receives that name as a string. Omit this for unnamed elements. |
| `body:` | Names fields and supplies an ordinary type or constraint expression for each field. |
| `value:` | Uses normal DynLex expressions to define the section's result. |
| `result:` | Optional result constraint. Also makes the type available before the initializer, for forward and cyclic references. |
| `receiver:` | Selects a deferred executable body and evaluates its input type using the section header bindings. |
| `returns:` | Optionally constrains the deferred callback result; otherwise its result is inferred. |

The metadata spellings are configured in `lib/config.dl`. A section has one structural
`body:` or one executable `receiver:`. Its `value:` determines what the section means;
it may return a descriptor, an array, a record, a function pointer, or `nothing` when
immediate execution is intended.

Body fields use existing type constraints. `many integer` is the ordinary array
constraint, with zero or more integer items. A scalar field requires one item. A fixed
array extent requires that many items. The compiler infers each child with normal
pattern matching and type inference, then routes its result to the accepting field.
When both a scalar and a sequence accept an unlabeled value directly, it goes into the
sequence. Named fields resolve overlapping domains explicitly:

```text
class:
    patterns:
        configuration
    members:
        title as a C string
        values

section settings {name}:
    declares: name
    body:
        title: a C string
        values: many integer
    value:
        @intrinsic("construct", configuration, the body's title, the body's values)

settings example:
    title: "Configured"
    values:
        10
        20
```

Field labels are visible in their owning section's body. Multiple section definitions
can reuse labels such as `children` or `title`. Omitted sequences are empty typed arrays;
missing scalars, duplicate scalars, ambiguous destinations, and incompatible element
representations are errors. User-defined implicit conversions are available for body
items: the UI library uses one to interpret a bare string as a table cell.

## Deferred bodies and events

The UI library defines its callbacks using ordinary section metadata:

```text
this window means: @intrinsic("receiver")

section code:
    receiver: a pointer to UI instance
    returns: nothing
    result: UI reference
    value:
        UI node 10 with "" colored "" sized 0 by 0 bordered 0 containing no UI children acting @intrinsic("cast", @intrinsic("deferred body"), a pointer)

section when the {C string:label} button is clicked:
    receiver: a pointer to UI instance
    returns: nothing
    value:
        bind the button labeled label in this window to @intrinsic("deferred body")
```

`@intrinsic("deferred body")` yields a typed callable value; `@intrinsic("receiver")`
refers to that callable's receiver parameter. The callback has one receiver argument,
returns `nothing`, and uses the native or WebAssembly callable ABI. Callback state must be
reached through the explicit receiver; there is no implicit capture of surrounding
function locals.

The `code:` callback initializes each UI instance. When it runs, each event section's
value registers its inner callback for that instance. The event body runs later, when
the corresponding button is clicked. The outer callback's local-variable checks stop
at nested definitions: the inner receiver is a different parameter.

## Static graphs beyond UI

`lib/state_machine.dl` defines `state machine`, `state`, and `on` sections with the same
mechanism. `examples/sections/publication.dl` declares forward and cyclic transitions,
then runs independent machine instances. To try it:

```sh
./build/dynlex examples/sections/publication.dl -o /tmp/publication
/tmp/publication
```

`a static reference to value` emits an immutable LLVM global initialized from the
aggregate value. Arrays and records are composed directly as aggregate values; a fully
static description becomes constant records, child arrays, strings, and callable
addresses. Forward-reference type annotations allow cycles without runtime backpatching.
Static references require constant initializers and diagnose runtime-dependent values.
Instance creation and callbacks are separate executable code.

## Verification

`./scripts/test.sh` checks section parsing, body constraints, static aggregates,
forward references and deferred callbacks. `./scripts/test.sh --integration` checks
library behavior. Native and browser UI tests run in the `dynlex-ui` repository.

## Synchronous request events

`lib/events.dl` defines request and response events entirely through ordinary sections
and typed callbacks. A channel has one responder. Registering again replaces it;
requesting an unbound channel reports an explicit error. A request runs immediately
and returns the responder's result:

```text
import lib/events.dl

set squares to a synchronous event receiving integer returning integer
when squares is requested:
    return this request * this request

print line (request 9 from squares) # 81
```

The request can be a data record containing explicit context, including pointers.
There are no implicit captures of outer locals. Responses can be scalar values,
records, managed strings, or `nothing`. Independent instances can carry different
callback signatures. The section definition is just:

```text
section when {synchronous event:channel} is requested:
    receiver: the request type of (channel's responder)
    returns: the response type of (channel's responder)
    value:
        set channel's responder to @intrinsic("deferred body")
```

`lib/callbacks.dl` provides `a callback receiving integer returning integer` as a
concrete type. `@intrinsic("invoke", action, argument)` checks the full signature
and derives the result type. Named function references carry all parameter types,
including functions with multiple parameters. `@intrinsic("function type", result,
first parameter, second parameter)` builds their types. Signature queries use only
the callback's type and never execute it. Native and WebAssembly `call pointer` remain available
for foreign pointers; if passed a typed callback it checks the declared signature.
