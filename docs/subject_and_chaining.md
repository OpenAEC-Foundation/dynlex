# Subjects and action chaining

DynLex actions return `void`. An action can still publish an outcome for the next action by setting the subject. Boolean state is stored under a noun property and exposed through a grammatical predicate:

```dynlex
class:
    patterns:
        [an|] example action
    members:
        success as a boolean

function {example action:action} succeeded:
    replacement:
        action's success

local function a successful example action:
    replacement:
        @intrinsic("construct", an example action, true)

flex function perform an example action:
    replacement:
        set the subject to a successful example action
```

`it` reads the most recently assigned subject value in the current execution flow. The value retains its inferred type, so ordinary typed patterns and generated class properties apply:

```dynlex
perform an example action
print whether it succeeded as a line
```

Subject assignment is explicit. Referencing `it` before `set the subject` is an error. If different runtime branches would assign different subjects, reading `it` after the branch is also an error because its source is ambiguous. A subject assigned inside a loop is available afterward only when the first iteration is statically guaranteed and every path that can leave the loop has the same subject. When zero iterations remain possible, the pre-loop subject path is retained and a different loop assignment makes the result ambiguous.

The standard library has separate `and` overloads for booleans and actions. The action overload expands both void expressions in order, so the right action can use the subject assigned by the left action:

```dynlex
perform an example action and print whether it succeeded as a line
```

`print` does not add a newline. Use `as a line` when a line ending is needed.

`local` definitions are visible only within their source file. This lets a library expose a small action surface while keeping its implementation patterns private.
