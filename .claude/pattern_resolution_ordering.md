# Pattern Resolution Ordering Problem

## Background

Pattern definitions are added to the pattern tree incrementally. A definition can only be added once all its VariableLike elements are classified as either Variable or text (Other). This classification happens level-by-level: body references resolve, `searchParentPatterns` converts matching VL elements to Variable, and remaining VL elements with no body references using them (variableLikeCount == 0) become text.

## What's Been Fixed

### Global references (fixed)

Global references (top-level code not inside any definition body) are now resolved in a separate phase, after all definitions have been added to the tree. This guarantees they see the complete pattern set and always match the most specific pattern.

**Example:**
```
expression the length of str:
    get:
        ...

expression the {word:propertyname} of ownername:
    get:
        ...

set len to the length of msg
```

Previously, `the length of msg` could match `the {word} of ownername` if that definition was added to the tree first. Now it's resolved after both definitions are in the tree, so the more specific `the length of str` wins via text match priority.

### Faster VL classification (improved)

The variableLikeCount mechanism classifies VL elements as text earlier. For each VL element in a definition, we count how many body references contain that text as a VL element. When the count reaches 0 (all such references resolved without producing a matching variable), the element is classified as text without waiting for `unresolvedCount == 0`. This gets definitions into the tree faster.

## Remaining Problem: Body References Inside Definition Sections

Body references inside definition sections are still resolved during the main iteration loop alongside definition resolution. If a more general definition resolves before a more specific one, body references in other definitions can match the general pattern incorrectly.

### Example

Consider these definitions in std.dl:

```
expression the length of str:
    get:
        @intrinsic("call", "libc", "strlen", "i64", str)

expression the {word:propertyname} of ownername:
    get:
        @intrinsic("property", ownername, propertyname)
```

And a third definition whose body uses `the length of`:

```
effect draw text str at x y:
    replacement:
        set len to the length of str
        ...
```

The body reference `the length of str` inside `draw text` needs to match `the length of str` (the specific pattern), not `the {word} of ownername` (the general pattern).

If `the {word} of ownername` gets added to the tree before `the length of str`, the body reference matches the wrong pattern. The variableLikeCount mechanism makes this less likely (both definitions classify their text elements quickly), but doesn't guarantee ordering between definitions whose VL elements resolve at the same iteration.

### Why It Happens

The iteration loop processes all sections in a single pass per iteration. If two definitions both become resolved on the same iteration, their order in the `unResolvedSections` list determines which gets added to the tree first. Body references processed in the same or subsequent iteration may then match the first-added (more general) pattern before the second (more specific) one is in the tree.

### Possible Solutions

1. **Pattern specificity scoring**: When matching, prefer patterns with more literal text matches over word/argument matches. This is essentially what the tree's priority system does (text > word > argument), but it only works when both patterns are in the tree simultaneously.

2. **Deferred body reference resolution**: Don't resolve body references until all definitions at the same level are in the tree. This requires detecting "levels" of definitions (definitions whose body references don't depend on each other).

3. **Re-matching after tree changes**: After adding new definitions to the tree, re-check already-resolved body references to see if a more specific match is now available. This is expensive but correct.

4. **Conflict detection**: After all definitions are in the tree, verify that every resolved body reference still matches the same pattern. If not, re-resolve it. This is cheaper than option 3 since it only re-checks, not re-resolves everything.
