# Sections and Stack Frames

In 3BX, the execution state is managed through a hierarchy of **Stack Frames** and **Sections**. Understanding how these interact is crucial for implementing control flow (like loops and conditionals) and accessing variables across different scopes.

## Stack Frames

A **Stack Frame** represents a call to an expression, effect, or section definition. It contains its own set of local variables and captures from the pattern that invoked it.

- **Current Frame (`0 frames back`):** The frame of the pattern currently being executed.
- **Calling Frame (`1 frame back` or `the caller`):** The frame that invoked the current pattern.
- **Intrinsics:** Frames are accessed via `@intrinsic("frame", index)`.

When you define an `expression`, `effect`, or `section`, its `get:` or `execute:` block runs in a new frame.

## Sections

A **Section** is a block of code (usually indented) associated with a pattern. Sections can be nested, forming a parent-child relationship.

- **The Caller's Child Section:** When a pattern is followed by a colon (e.g., `if condition:`), the code block following it is passed to the pattern's execution frame as `the caller's child section`. This refers to the first indented code block after the calling frame's execution line.
- **Nesting:** Sections can be navigated relatively:
    - `0 sections up from the caller`: The current section where the pattern was invoked.
    - `1 section up from the caller`: The parent section of where the pattern was invoked.
- **Execution:** Sections are executed using `@intrinsic("execute", section)`.

### Example: Loop Implementation
In `lib/loop.3bx`, a `while` loop uses the `loop_while` intrinsic, passing the condition and the block of code to be repeated:

```3bx
section loop while {condition}:
    execute:
        @intrinsic("loop_while", condition, the caller's child section)
```

Here, `the caller's child section` refers to the code block indented under `loop while ...:`.

## Summary of Relative References

From within a definition's `execute:` or `get:` block:

| Reference | Description |
|-----------|-------------|
| `the current frame` | The frame of the definition itself (`0 frames back`). |
| `the caller` | The frame that used the pattern (`1 frame back`). |
| `the caller's child section` | The first indented code block after the calling frame's execution line. |
| `this section` | Equivalent to `0 sections up from the caller`. |
| `the parent section` | Equivalent to `1 section up from the caller`. |

---

# Language Reference: Section Types

# returns the first indented code block after the frame's execution line
# example:
# if a is less than b:
# 	print "a is less than b"
# frame is at 'if a is less than b:'
# returns the section with 'print "a is less than b"'
expression:
	patterns:
		[the|] [inner|child] section [|of frame]
		frame's child section
	get:
		if frame isn't set:
			set frame to the caller
		return -1 sections up from the caller

possibilities for hardcoded sections:

# Named class with optional patterns/members
class <pattern>:
	patterns:
		<pattern>
		<pattern>
	members: m1, m2

# Anonymous class with pattern aliases (patterns: required)
class:
	patterns:
		<pattern>
		<pattern>
	members: m1, m2

expression <pattern>:
	patterns:
		<pattern>
		<pattern>
	get:
		<any code>
	set to $:
		<any code>

effect <pattern>:
	execute:
		<any code>
	patterns:
		<pattern>
		<pattern>
section <pattern>:
	execute:
		<any code>
	patterns:
		<pattern>
		<pattern>
<section pattern reference>:
	<any code>