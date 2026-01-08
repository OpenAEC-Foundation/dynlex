# Pattern Matching and Expression Substitution

This document describes how 3BX matches patterns and substitutes expressions into calls.

## Overview

3BX uses a **pattern tree** (trie-like structure) to efficiently store and match patterns. Each node in the tree represents a pattern element, with child nodes stored in unordered maps keyed by strings.

## Architectural Principle: No LLVM at Resolution Time

**Pattern resolution is entirely text-based.** There is no interaction with LLVM during pattern matching. This is a fundamental design principle:

| Phase | Input | Output | LLVM? |
|-------|-------|--------|-------|
| Pattern Resolution | Source text | Matched patterns with string arguments | ❌ No |
| Code Generation | Resolved patterns | LLVM IR | ✅ Yes |

### Why This Matters

1. **Clean separation**: Pattern matching is syntactic; code generation is semantic
2. **Arguments are strings**: When `set z to x + y` matches `set $ to $`, the arguments are `["z", "x + y"]` as strings
3. **Evaluation happens later**: The code generator decides how to interpret these strings (variable name vs expression)
4. **Testable**: Pattern resolution can be tested without LLVM dependencies

### Example Flow

```
Source: "set z to x + y"

Pattern Resolution (no LLVM):
├── Match against effect tree
├── Found: "set $ to $"
└── Arguments: {var: "z", val: "x + y"}  ← strings only!

Code Generation (with LLVM):
├── Process matched pattern
├── "z" → treat as variable name for store
├── "x + y" → recursively evaluate as expression
│   ├── Match "$ + $"
│   ├── Generate: load x, load y, add
│   └── Return: LLVM value
└── Generate: store result to z
```

## Parsing Workflow

Parsing happens in phases, with literals and special constructs detected first, then pattern trees used for matching.

### Phase 1: Detect Literals and Hierarchy

Before pattern matching, scan the input for:

1. **Number literals**: `42`, `3.14`, `-7`
2. **String literals**: `"hello"`, `"world"`
3. **Intrinsic calls**: `@intrinsic("name", arg1, arg...)` - parsed specially, args processed recursively
4. **Grouping parentheses**: `(a + b)` - defines evaluation hierarchy

```
Input: print @intrinsic("add", 2 + 3) * 5

Phase 1 output:
├── "print "
├── INTRINSIC(@add)
│   ├── arg[0]: "x"
│   └── arg[1]: "2 + 3"
└── " * 5"
```

### Phase 2: Parse Intrinsic Arguments

For each intrinsic call detected, parse its arguments through the **expression tree**:

```
@intrinsic("add",x, 2 + 3)
     │  └── match against expression tree
     │      └── "$ + $" matches with {first $: 2, second $: 3}
     └── match against expression tree
         └── variable reference "x"
```

### Phase 3: Match Line Against Effect/Section Tree

Determine line type by trailing colon:
- **With `:`** → use effect/section tree
- **Without `:`** → use effect tree (statements)

```
Input: "print @intrinsic("add",x, 2 + 3) * 5"

Effect tree matching:
├── "print " matches literal
└── [$] matches remainder "@intrinsic("add",x, 2 + 3) * 5"
    └── recurse to expression tree (Phase 4)
```

### Phase 4: Expression Submatching

When a `$` slot is encountered, match against the **expression tree**:

```
"@intrinsic("add",x, 2 + 3) * 5"

Expression tree:
└── [$] " * " [$]
    │         └── "5" (literal)
    └── "@intrinsic("add",x, 2 + 3)" (intrinsic, already parsed)
```

### Matching Priority: Finish Pattern First

When traversing pattern trees, **prefer completing the current pattern** before trying submatches:

```
function match(input, node, position):
    # 1. First, try to complete current pattern with literals
    for (literal, child) in node.children:
        if input.startswith(literal, position):
            result = match(input, child, position + literal.length)
            if result:
                return result  # Pattern completed!

    # 2. Only if no literal match, try expression substitution
    if node.expression_child:
        for boundary in find_expression_boundaries(input, position):
            sub_expr = input[position:boundary]
            sub_match = match_expression(sub_expr)  # Recurse into expression tree
            if sub_match:
                result = match(input, node.expression_child, boundary)
                if result:
                    return result

    # 3. Check if pattern ends here
    if position >= input.length and node.patterns_ended_here:
        return best_match(node.patterns_ended_here)

    return null
```

This ensures `print hello` matches `print $` directly, not `$ $` with subexpressions.

### Separate Trees by Pattern Type

Maintain separate pattern trees for different contexts:

| Tree | Contains | Used For |
|------|----------|----------|
| **Effect Tree** | `effect` patterns | Standalone statements |
| **Section Tree** | `section` patterns | Lines ending with `:` |
| **Expression Tree** | `expression` patterns | `$` slot submatching (includes booleans) |

Note: `condition` patterns are treated as `expression` patterns since conditions are just expressions that return boolean values.

### Complete Example

```
Input: "set result to @intrinsic("mul", x + 1, 5)"

Phase 1 - Detect literals:
├── Intrinsic: @intrinsic("mul", ...)
│   ├── arg[0]: "x + 1"
│   └── arg[1]: "5"

Phase 2 - Parse intrinsic args (expression tree):
├── "x + 1" → "$ + $" with {left: x, right: 1}
└── "5" → literal 5

Phase 3 - Match line (effect tree):
└── "set $ to $" matches
    ├── $1 = "result" (identifier)
    └── $2 = @intrinsic("mul", ...) (intrinsic expression)

Result:
SetEffect {
    var: "result",
    value: IntrinsicCall {
        name: "mul",
        args: [
            AddExpr { left: VarRef("x"), right: 1 },
            5
        ]
    }
}
```

## Pattern Elements

Pattern elements are **merged literal sequences** or **variable slots**:

| Input | Elements |
|-------|----------|
| `print msg` | `["print ", $]` |
| `left + right` | `[$, " + ", $]` |
| `set var to val` | `["set ", $, " to ", $]` |

**Tokenization rules:**
- Consecutive literals (alphanumeric, whitespace, operators) are merged into single strings
- Merging stops at variable boundaries (`$` or `{word}`)
- `$` represents a variable slot (matches any expression)
- `{word}` represents a lazy/deferred capture

This optimization reduces tree depth significantly since string lookups are O(1) regardless of length.

## Pattern Tree Structure

```cpp
struct PatternTreeNode {
    // Child nodes keyed by literal tokens
    std::unordered_map<std::string, std::shared_ptr<PatternTreeNode>> children;

    // Patterns that end at this node
    std::vector<ResolvedPattern*> patterns_ended_here;

    // For variable slots ($): child node for expression matching
    std::shared_ptr<PatternTreeNode> expression_child;

    // For lazy captures ({expression:lazy}): child node for deferred matching
    std::shared_ptr<PatternTreeNode> lazy_child;
};

class PatternTree {
    PatternTreeNode root;

    void add_pattern(ResolvedPattern* pattern);
    PatternMatch* match(const std::string& input);
};
```

## Tree Construction

Patterns are added to the tree by walking through their elements and creating/reusing nodes.

### Example: Building the Tree

Given patterns:
```
effect print $           → ["print ", $]
effect set $ to $        → ["set ", $, " to ", $]
expression $ + $         → [$, " + ", $]
expression $ * $         → [$, " * ", $]
```

Tree structure (with merged literals):
```
root
├── "print " → [$] → [END: print effect]
├── "set " → [$] → " to " → [$] → [END: set effect]
└── [$] → " + " → [$] → [END: add expression]
       └── " * " → [$] → [END: mul expression]
```

### Algorithm

```
function add_pattern(pattern):
    node = root
    for token in pattern.tokens:
        if token == "$":
            if node.expression_child is null:
                node.expression_child = new PatternTreeNode()
            node = node.expression_child
        else if token starts with "{" and ends with "}":
            if node.lazy_child is null:
                node.lazy_child = new PatternTreeNode()
            node = node.lazy_child
        else:
            if token not in node.children:
                node.children[token] = new PatternTreeNode()
            node = node.children[token]

    node.patterns_ended_here.append(pattern)
```

## Pattern Matching with Expression Substitution

The key insight is that **expression slots ($) can match any sub-expression**, not just literals. This requires recursive matching.

### Matching Algorithm

```
function match(input, node, position, arguments):
    # Base case: end of input
    if position >= input.length:
        if node.patterns_ended_here is not empty:
            return best_match(node.patterns_ended_here, arguments)
        return null

    results = []

    # Try literal match first (most specific)
    for (token, child) in node.children:
        if input starts with token at position:
            result = match(input, child, position + token.length, arguments)
            if result:
                results.append(result)

    # Try expression substitution
    if node.expression_child:
        # Try to match a sub-expression at current position
        for expr_end in possible_expression_ends(input, position):
            sub_input = input[position:expr_end]
            sub_match = match_expression(sub_input)
            if sub_match:
                new_args = arguments + [sub_match]
                result = match(input, node.expression_child, expr_end, new_args)
                if result:
                    results.append(result)

    # Return best match (most specific pattern)
    return best_by_specificity(results)
```

### Expression Matching

When a `$` slot is encountered, the matcher tries to find a valid expression:

```
function match_expression(input):
    # Try expression patterns only
    for pattern in expression_patterns:
        match = try_match(pattern, input)
        if match:
            return match

    # Try literals (numbers, strings)
    if is_number(input):
        return NumberLiteral(input)
    if is_string(input):
        return StringLiteral(input)

    # Try variable reference
    if is_identifier(input):
        return VariableReference(input)

    return null
```

### Finding Expression Boundaries

The tricky part is determining where an expression ends. We use **greedy matching with backtracking**:

```
function possible_expression_ends(input, start):
    ends = []

    # Try progressively longer substrings
    for end in range(start + 1, input.length + 1):
        substring = input[start:end]

        # Check if this could be a valid expression boundary
        if end == input.length:
            ends.append(end)
        else if input[end] is whitespace or operator:
            ends.append(end)

    # Return in reverse order (longest first) for greedy matching
    return reversed(ends)
```

## Example: Matching "print 5 + 3"

Input: `"print 5 + 3"`

1. **Start at root, position 0**
2. **Try literal "print"** → matches, advance to position 5
3. **At " " node** → matches space, advance to position 6
4. **At $ node** → need to match an expression starting at "5 + 3"
5. **Try sub-expression matching:**
   - Try "5 + 3" as expression
   - Root $ node matches "5"
   - " " matches
   - "+" matches
   - " " matches
   - $ node matches "3"
   - END: matches `$ + $` expression
   - Result: `AddExpression(5, 3)` → evaluates to 8
6. **Expression slot filled** → continue at position 12 (end)
7. **END reached** → match found: `print $` with arg `AddExpression(5, 3)`

## Operator Precedence

For expressions like `2 + 3 * 4`, precedence determines parse order.

### Priority Rules

Patterns can declare priority:
```
expression $ * $:
    priority: before $ + $
```

This means `*` binds tighter than `+`.

### Precedence Handling

When multiple expression matches are possible, use precedence to choose:

```
function match_expression_with_precedence(input):
    matches = find_all_expression_matches(input)

    # Sort by precedence (higher precedence = evaluated first)
    sort(matches, by: pattern.precedence, descending)

    # Return highest precedence match
    return matches[0]
```

### Example: "2 + 3 * 4"

Possible parses:
1. `(2 + 3) * 4` → `$ * $` matches outer, `$ + $` matches inner
2. `2 + (3 * 4)` → `$ + $` matches outer, `$ * $` matches inner

Since `$ * $` has higher precedence, parse 2 is chosen:
- Outer: `$ + $` with left=2, right=(3*4)
- Inner: `$ * $` with left=3, right=4

## Typed Captures

3BX supports typed captures in patterns to control how arguments are matched and captured. The syntax is `{type:name}` where `type` specifies the capture behavior.

### Capture Types

| Syntax | Description | Matching | Scope |
|--------|-------------|----------|-------|
| `{expression:name}` | Lazy expression capture | Greedy (longest match) | Caller's scope |
| `{word:name}` | Single identifier capture | Non-greedy (single word) | As string literal |
| `$` | Eager expression capture | Greedy, evaluated immediately | Current scope |

### Expression Captures `{expression:name}`

Expression captures are **lazy** - they capture the expression text/AST without evaluating it. The captured expression is re-evaluated each time it's used, in the **caller's scope**.

```
section while {expression:condition}:
    execute:
        @intrinsic("loop_while", condition, the caller's child section)
```

This allows control flow patterns where conditions need to be re-evaluated:

```
set x to 0
while x < 10:
    print x
    set x to x + 1
```

The condition `x < 10` is captured as an unevaluated expression and re-evaluated on each iteration in the caller's scope (where `x` is defined).

### Word Captures `{word:name}`

Word captures match a **single identifier** (non-greedy) and capture it as a string. This is useful for member access and reflection-like patterns.

```
expression {word:member} of obj:
    get:
        @intrinsic("member_access", obj, member)
    set to value:
        @intrinsic("member_set", obj, member, value)
```

The `{word:member}` captures just the member name (e.g., `x`, `width`, `name`) as a string, not as an expression to evaluate.

### Matching Algorithm for Typed Captures

```
function match_typed_capture(input, position, capture_type):
    if capture_type == "expression":
        # Greedy: find longest valid expression
        end = find_expression_end(input, position)
        return LazyCapture(input[position:end], caller_scope=true)

    else if capture_type == "word":
        # Non-greedy: match single identifier
        end = find_word_end(input, position)
        word = input[position:end]
        if not is_identifier(word):
            return null
        return StringLiteral(word)

    return null

function find_word_end(input, position):
    end = position
    while end < input.length and is_identifier_char(input[end]):
        end++
    return end
```

## Alternatives [a|b] - Branch and Merge

Alternatives create **branches** in the tree that **merge back** when paths converge. This avoids exponential expansion.

### Example: `[i|we|you] [am|are|] here`

Instead of expanding to 9 separate patterns, we branch and merge:

```
root
├── "i " ──┐
├── "we " ─┼──→ [MERGE] → "am " ──┐
└── "you " ┘              "are " ─┼──→ [MERGE] → "here" [END]
                          "" ─────┘
```

The `[MERGE]` nodes are **shared within a single pattern** - all branches from one alternative converge to the same continuation.

### Node Separation: Overlapping Patterns

**Problem**: What if another pattern partially overlaps?

```
Pattern 1: [i|we|you] [am|are|] plural
Pattern 2: we are plural
```

With naive sharing, "i am plural" could incorrectly reach P2's endpoint!

**Solution**: Different patterns create **separate paths** from shared prefixes. Merging only happens **within** a single pattern's alternatives.

```
root
├── "i " ──→ [P1 merge] → "am "  ──┐
│                         "are " ──┼→ [P1 merge] → "plural" [END: P1]
│                         "" ──────┘
├── "we " ──┬─→ [P1 merge] → ... → "plural" [END: P1]
│           └─→ "are plural" [END: P2]  ← SEPARATE path!
└── "you " ──→ [P1 merge] → ... → "plural" [END: P1]
```

Now:
- "i am plural" → only P1 ✓
- "we are plural" → both P1 and P2, **P2 wins** (more specific) ✓
- "you plural" → only P1 ✓

**Rule**: Patterns can share **prefix nodes** (like "we "), but each pattern's internal alternative structure stays isolated.

### Complex Example: `[i|we|you] [am|are|] about to [break|lag|] [the|a] [pattern|] tree`

Without merging: 3 × 3 × 3 × 2 × 2 = **108 patterns** (exponential in # of alternatives)

With branch-and-merge:
```
root
├── "i " ──┐                                    ┌── "break " ──┐
├── "we " ─┼→ "am " ──┐                         ├── "lag " ────┼→ "the " ──┐
└── "you " ┘  "are " ─┼→ "about to " → [MERGE] ─┤              │  "a " ────┼→ "pattern " ──┐
              "" ─────┘                         └── "" ────────┘           │  "" ──────────┼→ "tree" [END]
                                                                           └───────────────┘
```

Node count: 3 + 2 + 1 + 2 + 2 + 1 + 1 + ~5 merges = **~17 nodes** (linear: sum of options per alternative)

### Algorithm

```
function add_pattern_with_alternatives(pattern):
    # Parse alternatives into a tree structure
    hierarchy = parse_alternatives(pattern)  # Returns nested structure

    # Track current nodes (can be multiple due to branches)
    current_nodes = [root]

    for element in hierarchy:
        if element is Alternative([options...]):
            # Branch: create paths for each option
            next_nodes = []
            for option in options:
                for node in current_nodes:
                    next_node = add_literal_path(node, option)
                    next_nodes.append(next_node)
            # Merge: all branches converge to shared continuation
            current_nodes = [merge_nodes(next_nodes)]
        else if element is Literal(text):
            current_nodes = [add_literal(node, text) for node in current_nodes]
        else if element is Variable:
            current_nodes = [get_or_create_expr_child(node) for node in current_nodes]

    # Mark end
    for node in current_nodes:
        node.patterns_ended_here.append(pattern)

function merge_nodes(nodes):
    # All nodes share the same continuation node
    merged = new PatternTreeNode()
    for node in nodes:
        node.continuation = merged
    return merged
```

### Empty Alternative (Optional)

`[word|]` means "word or nothing":

```
pattern: [loop |]while {expression:condition}:

root
├── "loop " ──┐
└── "" ───────┼──→ "while " → [$] → ":" [END]
```

The empty string branch `""` is a special case - it immediately merges without consuming input.

## Variable Deduction

Variables are deduced from usage, not declared:

1. **Direct intrinsic usage**: `@intrinsic("print", msg)` → `msg` is a variable
2. **Braced capture**: `{expression:captured}` → `captured` is always a variable
3. **Propagation**: If `set var to val` is called with `set foo to bar`, then `foo` and `bar` are variables in the calling pattern

## Pattern Resolution Order

1. **Phase 1**: Add all pattern definitions to the tree
2. **Phase 2**: For each pattern reference, try to match against the tree
3. **Phase 3**: Recursively resolve sub-expressions
4. **Phase 4**: Propagate variable information from resolved calls
5. **Repeat** until no progress or all resolved

## Implementation Notes

### Performance Considerations

- Use `std::unordered_map` for O(1) child lookup
- Cache compiled patterns to avoid recomputation
- Use string views to avoid copies during matching

### Memory Management

- Pattern tree nodes are shared (use `std::shared_ptr`)
- Pattern data is owned by the resolver
- Match results are short-lived (stack allocated where possible)

### Error Handling

- Ambiguous matches (multiple patterns with same specificity) → error
- Unresolved patterns after max iterations → error with context
- Type mismatches detected during code generation

## Summary

| Component | Purpose |
|-----------|---------|
| PatternTree | Stores patterns in trie structure |
| PatternTreeNode | Node with child map and end markers |
| match() | Recursive matching with expression substitution |
| match_expression() | Matches sub-expressions for $ slots |
| `$` | Eager expression capture (evaluated immediately) |
| `{expression:name}` | Lazy expression capture (re-evaluated in caller's scope) |
| `{word:name}` | Single identifier capture (as string literal) |
| Precedence | Resolves ambiguous expression parses |

### Key Principle

**Pattern resolution outputs strings, not LLVM values.** The code generator is responsible for interpreting these strings:
- Variable names (lvalues) stay as strings for `@intrinsic("store", varName, ...)`
- Expressions (rvalues) are recursively evaluated to produce LLVM values
- This separation keeps pattern matching simple and backend-agnostic
