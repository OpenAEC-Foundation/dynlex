# Declarative sections

Sections can produce data instead of executing their indented contents. Their grammar,
body constraints, representation, and behavior are defined in DynLex libraries. The
compiler has no built-in window, table, style, or state-machine syntax.

## Try the window

Run these commands from the repository root:

```sh
./scripts/build.sh --lint=false
./build/dynlex examples/sections/project.dl -o /tmp/project-window
/tmp/project-window
```

The native example uses the existing Vulkan, GLFW, and FreeType libraries and the
repository's Liberation Mono font. It shows rounded buttons, hover and press feedback,
a table, and a status line. “New Project” increments the instance's project count;
“Draw” changes its status. Escape closes the window.

Edit `examples/sections/project_description.dl` to change the description:

```text
window project window:
    label "Your projects, in one place."
    toolbar:
        button "New Project"
        button "Draw"
    2 x 4 table:
        blue row:
            "name"
            col "amount"
    style:
        tables are light blue
        table cells are 220 x 48 pixels
        buttons are green and have a 2 pixels border
    code:
        when the "New Project" button is clicked:
            increment this project's state's count
            set this window's status to "Projects created: " + (the string form of this project's state's count)
        when the "Draw" button is clicked:
            set this window's status to "Draw selected"
```

The color words above are file-local aliases for strings. Quoted colors work too:
`"blue" row:`. Named colors include blue, light blue, green, white, black, orange,
purple, and gold.
Empty rows and cells come from the table dimensions.

`project window` names the descriptor data. Defining it opens no window and registers
no handlers. `examples/sections/project.dl` creates application state, creates a UI
instance, and opens the native view. `tests/required/ui_sections/main.dl` uses the same
description without a renderer and verifies independent instances and the blue row's
width, which is 440 pixels with this style.

## Define a section

`examples/ribbon/interface.dl` is a larger example: nine tabs of automatically
arranged ribbon groups, commands, rows and columns. Tab selection belongs to the
UI library and needs no application callbacks. See `examples/ribbon/README.md`
for the authoring patterns and launch command.

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

print the item at 2 in example numbers as a line
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

## Extend the UI grammar

The window declaration in `lib/ui.dl` is an ordinary data section:

```text
section window {name}:
    declares: name
    result: UI reference
    body:
        children: many UI reference
    value:
        UI node 0 with name colored "" sized 0 by 0 bordered 0 containing the body's children acting no UI action
```

UI references point to a common descriptor representation. Buttons, rows, tables,
styles, and user-defined containers all produce this representation, so `children`
accepts new section kinds without changes to the compiler. For example:

```text
section panel:
    result: UI reference
    body:
        children: many UI reference
    value:
        a UI container of kind 12 containing (the body's children)
```

`panel:` can now contain buttons, labels, nested panels, or any other UI reference.
The native renderer treats additional container kinds as vertical groups. New control
behavior or drawing belongs in the DynLex UI library and renderer.

`lib/ui_components.dl` also supplies positioned panels and groups, pages, text roles,
segmented choices, switches, statistics, badges, and paintable canvases. The native
renderer supports prepared font atlases, batched text with fractional advances, supersampled fonts, antialiased
rounded shapes and strokes, gradients, soft shadows, reflections, hover/press feedback,
Tab/Enter focus, and a logical viewport that scales with the window. Pages and control
values belong to each UI instance; the descriptor remains immutable.

`tests/games/snake/interface.dl` declares a complete menu, settings, playing, paused and
finished interface. Its separate `code:` section binds events, while `painting.dl`
implements the canvas callback. See `tests/games/snake/README.md` for running it and
adjusting the sections. Native text editing, scrolling and automatic layout reflow
remain outside the current renderer's controls.

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
returns `nothing`, and uses the existing native callable ABI. Callback state must be
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

`./scripts/test.sh --lint=false` runs the compiler suite and the native window check.
On Linux, the window check uses a private Xvfb display, renders the real application,
sends mouse events, verifies the table color and callback results, and closes it.
It does not use the desktop display. To run it alone and save a screenshot:

```sh
python3 -B scripts/test_ui_sections.py . ./build/dynlex --snapshot /tmp/project-window.png
```

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

print (request 9 from squares) as a line # 81
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
the callback's type and never execute it. Native `call pointer` remains available
for foreign pointers; if passed a typed callback it checks the declared signature.
