This document explains how the compiler is supposed to behave.

All code should be as DRY, agnostic, user-friendly, and performant as possible. Do more with less code.

When we encounter an error in the users code, we add an error diagnostic and return `false` if this error could cause dependent errors. When a child function returns `false`, return `false` as well. This will make the compiler exit cleanly with a single diagnostic. We do not continue scanning for other diagnostics, because one error will cause lots of other errors most of the time and make it unclear for users what they need to focus on to fix.

When we encounter an error in the compilers code, the compiler should SCREAM. crash with crashCompilerBug.

Later stages of the compiler are very dependent on earlier stages. Every line of code has to be carefully thought out.

# Import Stage

The compiler combines all files into one large file.

Each source file fixes its URI and internal-library classification when created.
Content edits preserve that identity. Syntax lookup selects the current compilation's
builtin or project configuration from that classification without repeating filesystem
canonicalization for each source range.

`--dump-dependencies` exposes the importer's loaded source files and selected syntax
configuration paths as JSON. It uses the normal configured import resolution and
does not parse executable syntax or generate code. Dependency inspection completes
the import graph even for diagnostic fixtures with invalid executable characters;
ordinary compilation still validates those characters while loading sources.
An import or configuration failure returns a nonzero status and marks the dependency
report incomplete; selected configuration paths remain available for diagnosing it.

# Parse Stage

Sections are analyzed. We do basic parsing **WITHOUT hardcoding**.

- Which line opens a new section?
- What patterns does each section have?
- We parse inline sections and multiline statements (f.e. statements with multi line arrays) too, here.

Every array element is parsed as a complete expression. A single-element array uses its entire interior text and nested
bracket hierarchy, just as comma-separated elements do; a nested string or parenthesis is not the element by itself.

String boundaries use backslash parity: a quote preceded by an odd run of backslashes is escaped; an even run leaves
the quote unescaped. Bracket parsing, comment scanning, and source-character validation share this rule.

Function declaration shorthands are normalized before section analysis. An action declaration using `to` and a value
declaration using `to get` become ordinary function and `execute` sections. A one-line declaration using `means:` becomes
an ordinary flex function and `replacement` section. Logical indentation and source slices preserve the authored nesting
and diagnostic locations; later stages do not have a separate shorthand execution path.

Scope rule blocks (`in this file`, `in this class` and `in this function`, optionally followed by `, by default`) are
parsed here, because locality must be known before pattern matching. The header names the source file or nearest
enclosing class or function definition, independently of ordinary body sections. Rules inside executable nesting or
file rules inside functions warn but still apply statically to the entire named scope. Section parsing collects all
rules before applying locality to definitions, including preceding definitions, and validating `local` and `public`
modifiers. Every other rule line is an ordinary pattern reference resolved like code, but never executed. See
`docs/scope_rules.md`.

Data-section metadata (`body`, `value`, `declares`, `result`, `receiver`, and `returns`, with spellings
from the syntax configuration) describes a section's body and value. A body schema creates
an anonymous record with ordinary generated property accessors. Each field's constraint
is an ordinary expression. The parser does not recognize domain-specific child syntax.
See `docs/data_sections.md`.

Integer literals are parsed exactly rather than through floating-point storage.
Values fitting the existing scalar ranges use signed 32 or 64 bits or unsigned
64 bits. Larger positive values use the smallest power-of-two signed width that
holds the magnitude, starting at 128 bits. The `2^63` boundary is unsigned in ordinary expressions;
direct unary negation produces signed 64-bit minimum. Stored unsigned values,
including that boundary, use ordinary wrapping negation. Integer literals retain
their exact bits through parsing and incremental caches. Explicit floating-point literals remain 64-bit
floating-point values, rounded to nearest with ties to even independently of the
host floating-point environment. Representable subnormal values are accepted;
overflow and nonzero magnitudes that round to zero are rejected.

# Pattern Matching Stage

Patterns are matched. Here we identify:

- What is a variable
- What is an argument

Pattern resolution commits explicit validity boundaries. Definitions and all
references inside their bodies become valid together before top-level references
are attempted. Top-level references become valid at the next boundary, followed
by the precedence graph. Expression expansion and variable binding complete the
full resolved-pattern stage. Language tooling may consume only data guaranteed by
the latest completed boundary; failed later work does not make its partial state
observable.

Named data-body fields become section patterns whose visibility is limited to their
owning section calls. A declared data name is indexed before expression expansion, so
multiword names and forward references use the normal variable matcher. After matching,
the declaring capture binds the result variable and supplies its spelling as a string
argument to the value expression.

Once section signatures are available, the normal matcher indexes declaring captures
before function-body resolution must finish. This discovers only declaration names;
the global call remains unresolved until the ordinary global-reference boundary.
Function bodies can therefore refer to multiword data names without creating a
dependency cycle between their own resolution and that of the declaring call.

A section with `receiver` metadata moves its already parsed body into an ordinary
callable function with one receiver parameter. This occurs before expression expansion
and variable grouping, including for nested deferred bodies. The original section call
retains the callable definition; its value expression decides whether to store or invoke
it. Deferred bodies do not execute as caller-body control flow.

When a child inference context retains a function instantiation during a trial, its
expression constants, operand groupings, and callable selections join the caller's
trial state. The outer transaction commits or discards those results together with
the inferred body; retaining only types loses metadata needed by code generation.

Incremental inference summaries omit functions in sources named by inference-stage
diagnostics and their notes. Warnings already produced during pattern resolution
do not exclude unrelated function summaries in the same file.

After the pattern trees stabilize, definitions connected through shared trie
endpoints form one syntax family, including transitive connections through choice
alternatives. Precedence is a partial order between these families. Each family's
membership and successor reachability are computed once; typed overloads share
that order. Matching then repeats with that order before committing expression
trees. An unparenthesized operand cannot contain an operator declared to bind
later than its parent, including operands inside variadic captures. If implicit
parameter discovery changes a declaration during this pass, resolution rebuilds
the families and repeats to a fixed point. Operand grouping also consumes that
same family order during type inference.
The rematch pass sorts only references whose current match can be affected by
precedence, preserving source order among equal locations.
Generated class accessors precede authored syntax through a shared graph node,
so graph edges grow with accessor families and precedence targets rather than
their product. Incremental build statistics report the resulting edge count.

All pattern definitions are stored in a pattern tree, a trie structure containing pattern elements.

Matcher lookup tables deduplicate complete control and continuation states. Search priority
comes from the ordered work queue and parent/completion vectors, independently of hash-table
layout. Continuation groups for equivalent states share a canonical parent and
completion set. Both existing results and later additions propagate through that
shared group, so revisiting a memoized state cannot lose a subsequently discovered
parent. Inline storage grows when needed and never limits matching alternatives.

We match with multiple iterations. This makes sure that patterns earlier in the file can call functions later in the file.
Failed body matches wait on the specific trie edges and endpoints inspected by the matcher.
Adding a definition wakes a waiting reference only when one of those conditions changes;
deadlock recovery still sees all waiting references. This avoids rescanning every failed
reference on each resolution pass.
An expression reference that cannot mention a live implicit parameter candidate waits
until definitions are indexed, even when another reference in the same function must
classify that candidate. Unchanged functions without global access can replay their
matcher keys after a top-level execution edit when the nonexecution source projection
is unchanged.
An edit inside a flex function can also preserve other functions' matcher keys:
matching checks the resolved pattern signatures and lexical declarations separately
from the flex's executable body. Scope-rule edits still invalidate that proof, and
body-driven parameter reclassification invalidates callers through the changed tree.
The saved matcher projection describes the boundary before receiver bodies become
generated functions. It stores declaration digests and only the match entries that
differ from the expanded projection; `matcher_projection_ms` reports its capture cost.
Flex bodies also participate in inference and native-code invalidation even though
they have no standalone native function. Inference summaries retain observed pure
call results; a changed function becomes a stable dependency boundary only when
those results, types, and effects are all preserved. Unserializable observed results
leave that boundary unproven, so callers are checked again.

Incremental matcher keys reuse each parsed reference's candidate literal spellings and numeric literal arguments.
Each indexed definition contributes to a tree of its required literal words. A reference fingerprints only
the signatures whose words occur in its matcher input, including wildcard-start patterns. A new overload
therefore invalidates a cached result when it could match that input, even if the previous result was a failure.
Lexical scope is fingerprinted again for every lookup, so a new declaration can invalidate the previous match.
Resolved declaration snapshots are reused after a function-body edit when the source outside function bodies is
unchanged and each edited body retains its resolved declaration. Scope-rule bodies and nested definitions exclude
this reuse, since their changes can affect declarations outside the edited function. The projection reuse counter
counts declarations carried forward without serializing them again.

We discover what is a variable based on these principles:

- A single-word function pattern is never a variable. Therefore, all functions with single-word patterns are parsed in the first round.
- Words authored as `[word]` or `{literal:word}` are always literal and are never eligible for implicit parameter promotion.
- A single word as an argument to an intrinsic is always a variable, unless it references a single-word function. Since functions are parsed before references to them, we are guaranteed that single-word functions exist from the start.
- Alphanumeric strings in argument positions of pattern calls are variables.
- `{name}` explicitly introduces or references a variable. The name can contain multiple words separated by spaces.
- `{type:name}` does the same and constrains the variable to values accepted by the type expression. This syntax works both in pattern definitions and in executable code.
- After a multi-word name has been introduced explicitly, later unbraced occurrences of that full name are matched as one variable in the same lexical scope and its children.
  An exact visible explicit name is resolved before attempting a pattern call; whitespace surrounding an intrinsic argument does not change its variable identity.

A square-bracket group without a top-level `|` is an explicit literal and is flattened into its ordinary literal elements.
`{literal:text}` produces the same representation. Square-bracket groups with alternatives remain choice elements.

`{arguments:name}` captures a non-empty sequence of source expressions, joined
with the configured comma separator and final conjunction. A normal capture wins
for a single argument when both spellings exist; a variadic capture also accepts
one argument when no ordinary spelling matches. Parentheses delimit an element's
expression normally. There is one trie path for a variadic declaration, independent
of argument count. Each matched element retains its caller-owned pack, element index and caller bindings;
resolution does not generate declarations for different arities. Captures resolve the current element root, so operand regrouping updates the original argument tree. The compilation context retains captured source bodies for its arena lifetime.

Capture names are type-agnostic unless a type expression appears before the colon. There is no lexical word-capture type; finite concepts such as shader stages are represented by literal patterns, and text values use an ordinary string type constraint.

We use this logic to determine what is a variable and what is not, all the way from the simplest intrinsics to the most complex functions.

The consequence: an unused argument is not an argument.

Retain and release bodies are independent function scopes for local variables.
Implicit declaration grouping and explicit lookup include the lifecycle body
itself, then stop there instead of binding a same-named top-level declaration.
Nested sections and flex replacements still share variables within that body.
Ordinary functions retain their explicit `globals` declaration behavior.

Pattern matching is type-agnostic. This is because we cannot easily match based on types if we do not even know whether a variable exists yet. Also, variables come from the callee to the caller (the function signature defines what is a variable), while types come from the caller to the callee (the arguments define the type).

The consequence: we cannot know what order nested expressions should have.

Example:

```text
print line value + other
```

Without type inference, a match cannot establish whether `print line value` produces a value usable by `+`.

Since we are fully agnostic, we will make all left expressions subexpressions:

```text
(print line value) + other
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

`size of` requires a concrete runtime type with a complete layout. A class with uninferred members remains generic even
after constructing instances of it; requesting its size reports a source diagnostic. Use the type of a particular instance
to measure that instantiation. Pointers to generic classes are sized independently of their pointee layout.

Class property access follows every pointer level to the owning object. Reading, writing, and taking a property's
address use that same owner, and pointer provenance follows the same chain. The owner expression is evaluated once.
Synthetic properties of other types, such as a C string's `data`, retain their existing representation.

Declaration return contracts are checked on the inferred result of the ordinary function body: `to` requires `nothing`,
while `to get` and `means:` require a value. The check participates in operand-grouping trials so the declaration error wins
over a secondary error at the call site.

We loop over the code like it would get executed.

Before inferring executable code, we infer every pattern argument type constraint with the same expression inference engine.
Freshly parsed constraint expressions expand their entire expression tree before inference,
including pattern calls nested inside intrinsics or array expressions.
Pattern matching has already recorded all candidates, but it has not selected an overload. Signature inference resolves
constraint dependencies first, selects overloads from inferred argument types, and requires every constraint expression to
produce a pure compile-time type or constraint value. An unresolved candidate defers a call only while that candidate can still
match its arguments. A known argument mismatch excludes it, including when another parameter is unresolved. On-demand signature
probes can establish necessary structural domains for exclusion, but cannot select a callee. Their cached domains omit speculative
class-layout indices, and nested class probes restore the enclosing layout transaction. If a full pass makes no progress, the
remaining signature dependency is cyclic and compilation fails.

Exact expression types and parameter requirements use separate models. `DataType` describes one exact expression type.
`TypeConstraint` describes the structural domain accepted by a pattern parameter and may additionally require a compile-time-known
value. The `constraint` meta-type carries `TypeConstraint` values just as the `type` meta-type carries type-reference values.
`@intrinsic("fix", value)` converts a type or constraint value into the same structural constraint with its compile-time-known
requirement enabled. Type and constraint shaping uses the same surface patterns; shaping a constraint preserves the constraint
category. A compile-time-known requirement is not an overload axis, so definitions which differ only by `fix` are duplicates.
Type values retain both meanings when used in a signature: their constraint view controls overload matching, while their exact type
view declares a concrete runtime representation when one is required for a callable ABI. For example, `integer` accepts every signed and
unsigned integer width as a constraint but denotes a signed 32-bit integer as a standalone type value. Code generation consumes that recorded exact
view; it does not reinterpret the constraint.

Number types are composed from properties with `@intrinsic("and", left, right)`, which accepts exactly the values both
type or constraint operands accept. `integer` leaves the sign unspecified and accepts signed and unsigned integers;
`signed` and `unsigned` restrict the sign. The result keeps an exact type view whenever its properties determine a
number kind, completing an unspecified sign and width with the built-in defaults.

Number rules (`numbers are ...`) are inferred in the declared type-constraint worklist. Constraints written in a scope
whose number rule is still pending wait for it. Number type phrases and numeric literals written in a scope with a
number rule receive the rule's properties where they leave them unspecified; flex replacements keep the rules of the
file that defines them. A literal whose type came from a rule yields to an explicitly typed parameter that rejects it,
as an implicit conversion candidate. Rule warnings are collected during inference and reported once it settles,
keeping the outermost of nested phrases.

Without number rules, an integer literal uses a 32-bit signed integer type when its value fits, a
64-bit signed integer type through the signed range, and an unsigned 64-bit
integer type above that range. The exact magnitude of signed 64-bit minimum is
also accepted so unary negation can form that signed value; it can be explicitly
cast to an unsigned 64-bit integer. Native and WebAssembly integer type values accept positive whole-byte
widths within LLVM's integer type limit, including 128, 256 and non-power-of-two
widths. Signed and unsigned arithmetic, casts, shifts, comparisons, division,
remainder and control-flow cases use the declared width at compile time and runtime.
Constant evaluation uses arbitrary-width integer bits and preserves signedness.
Pure execution reads each literal's finalized inferred value, including number-rule
typing. Integer-to-float casts round directly to the selected IEEE precision.
Specialization keys, editor value displays and persisted inference values retain
the complete value and width. Decimal formatting and digit counting operate on
the original integer type, without narrowing to a machine word.
Integer casts retain the target-width bits. Floating-point to integer casts
truncate toward zero and are compile-time-known only when the truncated value
fits the target signed or unsigned width, matching the native LLVM conversion.
The positive `2^63` literal participates in floating-point conversion, Boolean
conversion and mixed numeric comparison like other unsigned values.

Known numeric pointer addresses have a distinct compile-time value category. Null
construction, integer/pointer casts, pointer equality, and pointer arithmetic preserve
that category with the selected target's pointer width and pointee size. They never
dereference host addresses. Default scalar construction supplies its zero value to
ordinary constant evaluation, so null-guarded control flow follows the known condition.

Floor, ceiling, and round preserve an integer operand exactly, without converting it to floating point.
For floating-point operands they preserve the operand width; round chooses the nearest integer value,
with half-way values rounded away from zero. Constant evaluation uses the same operand precision and
rounding rules as native code generation.

After all constraints are concrete, we validate overlapping overload domains. Only then can normal call inference select
overloads. Declaration order never selects an overload.

this implies:
 - functions and classes are instantiated on USAGE. for functions when they're called, for classes with the construct intrinsic. so if a class or function is never used, NO instantiation is created.

The compiler NEVER expands flexes to look for something like an intrinsic. instead, the compiler walks the code normally and once intrinsics are encountered, it does something with them.

Recursive function inference runs complete body passes until the instantiation stabilizes.
A successful, type-valid body pass that encountered no return intrinsic establishes a
`nothing` result before the convergence decision, even if recursive calls need another
pass. Encountering a return whose operand is still unresolved is distinct from encountering
no return. Return observations belong to the active instantiation, reset for each body
pass, and participate in inference-trial rollback. Returns inferred inside a nested
function do not determine the caller's result.

An ordinary overload lookup with unresolved recursive operands is deferred until
the existing body reinference pass deduces those operands. Reads and assignments
of a pending local retain the dependency observed by their owning function pass.
Nested function passes keep separate observations, so an unrelated callee's error
remains a failure. Fixed Boolean results can be inferred while their operand checks
await the next pass; a condition whose type is still pending preserves its section
outcome and permits traversal to the return that resolves the recursive cycle.
Flex overloads explicitly accepting unresolved arguments still participate in
selection, including return wrappers that must record the return observation.

Binding scopes are immutable frames linked to their caller scope. Copying a scope or resolving a binding retains
that existing chain without copying its maps. Pushing and popping change only the active stack; snapshots and returned
caller scopes retain their original frames. Each invocation introduces all declarations from its definition scope,
including captures omitted by the selected choice path. Lookup searches from the innermost frame outward and stops at the
invocation that declares the requested variable; an omitted capture is that invocation's local variable. An actual argument
resolves in the binding frame's caller scope, so forwarding preserves its caller's identity. Constraints follow the same
scope boundaries. Type-resolution cache keys retain the ordered frames and declarations. Frame visitation runs from the
outermost frame inward.

Inference constants, pointer provenance, pure evaluation and native storage use the same lexical storage keys.
Each macro call site has its own storage scope. Recreating a body during inference preserves that call site's scope,
while separately elaborated argument-pack iterations have distinct scopes. Thus omitted captures cannot inherit
another invocation's values, and repeated inference of one runtime loop converges on the same storage slots.

Flex inference resets local variable types in the declaration scope before inferring its replacement bodies, including
parameters omitted by a choice path. Tracked local values and pointer facts reset when a macro invocation begins. Each invocation saves and restores local types in the declaration and its descendants,
so a nested invocation cannot replace its caller's inferred local types. Explicit declared type constraints remain in force.

Argument packs are compile-time source captures passed between replacement
definitions, rather than runtime array values. Ordinary `type extent` reads their
length and `extract element` selects a captured expression at a compile-time index.
Storing that reference binds the loop variable to the original expression; reading
it or assigning through it resolves in its captured caller scope. Inferring a
reference assignment target does not execute the expression previously bound there.
Thus the same `for each` replacement handles arrays, lists and heterogeneous packs.
No separate pack-iteration intrinsic exists.

A loop that selects argument references retains a separately inferred body for
each compile-time iteration, with fresh local types for that iteration. Every caller-body transfer inside those iterations
also owns its corresponding inferred caller body. Native generation emits those
bodies in order; pure evaluation consumes the same bodies. Conditions must remain
compile-time-known while elaborating such a loop. Ordinary runtime loops keep
their existing fixed-point inference and runtime branches.

Control-flow classification is an outcome of that normal inference walk. A control-flow intrinsic emits a typed section outcome, and a flex call may forward the outcome produced by its inferred replacement. A section flex forwards the outcome of its final top-level replacement statement after the preceding statements have executed. A function flex forwards a control-flow outcome only when its replacement is a single expression; control flow in a multi-line function flex remains internal to that function flex. When replacement inference transfers caller-body control, the outcome is recorded on the exact replacement expression that performed the transfer and enclosing flex calls forward it normally. A direct outcome already recorded on an active flex call takes precedence over the replacement expression's forwarded outcome. Transfer identity is the complete active flex-invocation path plus the transfer expression, with reusable clones normalized to their template nodes. This lets retries of one logical invocation share the inferred caller body while distinct direct, helper, and nested-helper call sites produce the required duplicate-transfer diagnostic. The section walker consumes outcomes in execution order: infer an `if` header, infer its reachable body, infer the next alternative header, then infer that reachable body. Before merging the fallthrough state of a conditional, the following section header may be trial-inferred through the ordinary inference transaction to determine whether its outcome continues the same chain. This uses the produced outcome only; it never inspects pattern text or searches an expansion for a particular intrinsic. Section overloads use the same type constraints and ambiguity checks as function overloads. Header inference selects the overload before its produced control-flow outcome is consumed. Parameters introduced by the section may remain unresolved until the selected replacement binds them; overload selection must be determined by the other header arguments, without depending on inference of the caller body.

The section walker records finalized fallthrough metadata on every top-level expression it reaches. An expression without that metadata is outside the inferred execution paths and remains intentionally uninferred. Code generation consumes this metadata before inspecting an expression, so it emits only paths whose overloads, types, and control flow were finalized by inference.

Data-section inference consumes the caller body structurally. Normal expression
inference produces each child's value, and the body's type constraints route it into
scalar fields or array elements. Named field sections select a destination explicitly;
omitted sequences are typed empty arrays. The value block reads the resulting anonymous
record through `section body` or a named `section field`. An optional result constraint
checks the value and supplies forward-reference types. A deferred body is inferred as
a separate callable instantiation; receiver lookup and local-variable validation stop
at function boundaries.

Section outcomes are value state stored through copy-on-write ownership. Ordinary expressions
share the empty outcome; a mutation detaches the complete outcome before writing. Clones and
retained inference snapshots therefore preserve independent conditions and condition types.

A section flex replacement is executed line by line in source order during inference, compile-time execution, and code generation. `execute body` transfers execution to the caller body at that exact point; if neither an outcome nor an executed `execute body` consumes it, the caller body executes after the replacement. Variables introduced by the active replacement scopes are bound structurally while the caller body runs, then those bindings end when control returns to the replacement. Every flex invocation allocates unbound runtime locals in its declaration scope as well as its replacement scopes. An optional parameter absent from the selected pattern path has local storage when the replacement assigns it; allocation supplies no initial value. Bound parameters continue to refer to caller arguments. Managed locals are released when their owning invocation ends. These rules use the already-selected flex definition, inferred replacement body, and section ownership stacks. They do not rediscover behavior from pattern text.

We track each variable that could possibly be a constant. A variable reference can be constant. Constant means compile-time-known here. It does not guarantee that the value does not change later. Execution-state maps retain an explicit unknown entry after a write or control-flow merge invalidates a previously known value, so an older expression value cannot reappear as the current variable value.

The same execution walk records pointer facts. Address provenance retains a set of possible variable-storage targets,
an explicit null alternative, static literal storage, and whether other origins are unknown. A null pointer and a value
containing no pointers are different facts. Assignments replace the destination's facts; alternatives and loop joins combine the possibilities.
Opaque writes discard certainty about affected pointer storage. Numeric operations and conversions on encoded addresses
retain their possible origins for alias tracking but make their storage uncertain. These facts are opportunistic: DynLex does not guarantee
memory safety, require ownership annotations, or run an additional safety-analysis pass. Memory diagnostics may report
only proven violations; a possible violation or unknown origin is insufficient. Deallocation remains implemented in DynLex.
The normal intrinsic walk records proven null memory accesses and writes to literal storage, including pointer calls,
atomic operations, and class property accesses. Expressions carry those proofs separately for value use and address use;
the section walker reports them when an executed statement consumes the corresponding operation. Flex replacements
forward their result's proofs and any preceding statement effects. Normal function instantiations summarize their body
violations for runtime calls. Type-only queries consume neither runtime form, and taking the address of a dereference
does not load the pointee. Asking for a function result's type does not suppress a violation at a later runtime call.
`check deallocation` consumes the recorded pointer facts and emits no runtime code. The standard-library deallocation
patterns perform that check before calling their allocator's release function; the compiler does not recognize external
function names. Known variable or literal storage cannot be deallocated, while null and unknown alternatives remain
accepted. Parameter storage can alias heap storage and is therefore unknown. Library deallocations use these patterns rather than bypassing the check with raw external calls.

Writing a concrete value through storage whose nested class subtype is still unspecified refines that storage type. Function instantiations record refinements to parameter storage and apply them to the caller, so a generic container can bind its element subtype on its first write without losing it at the function boundary. This only completes unspecified structure; it never changes an already concrete member type.

Assignments involving unfinished structure record type equations between lexical storage views. Fully determined types need no new equations; writes to existing roots still propagate through their earlier equations. Views project fields, aggregate elements and
pointer depth, so refinement through a copied pointer also completes the corresponding subtype in its source. These are
static type requirements, separate from runtime pointer provenance. Their state survives execution-only restoration,
including skipped conditional operands, and rolls back with a rejected grouping candidate. Macro invocations keep distinct
storage roots; equations from a completed invocation cannot overwrite another invocation's reusable variable metadata.
Subject values retain the source expression and its caller bindings for structural type refinement. Implicit conversions
contribute their result type rather than equating that result with the unconverted source storage.

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

Argument type inference and argument execution have separate effects. After selecting a replacement, inference restores the
caller's execution state from before the preliminary argument walk. The replacement then applies argument effects where its
bindings are used. An already inferred binding is reusable only when its inference did not change execution state and that same
state still holds. This state includes constants, address provenance, the subject and the owning instantiation's effect summary;
restoration retains inferred types, grouping selections and recursive inference dependencies.

Subject values belong to their function invocation during pure evaluation, just like local variable values. Publishing an unmanaged value is a local effect; publishing a managed value retains lifecycle effects and cannot make an effectful type function pure. A subject whose assignment depends on recursive inference stays pending with that dependency until a body pass resolves its value.

A replacement's result is the current root expression of its selected source line. Regrouping can move the original
template node beneath another expression, so template-node identity does not determine the replacement's result.

Boolean `and`, `or`, and `select` infer every operand for typing, while execution effects come only from selected paths.
Native code branches on the generated condition value; values known on one inference pass do not establish a constant
runtime condition across loop iterations. LLVM may simplify branches whose generated conditions are constant.

`type of` and `type extent` inspect an operand's inferred type without executing that operand. Their argument inference must
therefore leave the caller's execution state unchanged, and pure evaluation returns their stored result without walking the
operand. This also applies through ordinary replacement patterns. A type-producing function that actually performs I/O remains
impure and cannot be a type constraint.

after we have successfully inferred a function, if it is a pure function and all arguments are compile time known. we execute the function in compile time and retrieve the result from it. evaluating a pure function shouldn't modify anything, only give a compile time value as result.

Each root pure evaluation shares one execution budget across nested calls, expressions,
sections and loop iterations: at most 1,000,000 expression/section entries and 256
simultaneously active entries. Exhausting either resource unwinds the entire evaluation
and reports a compile-time evaluation limit at its root call. It must never resume an
enclosing caller with an unknown value or cache a partial result as a completed value.
Unwinding restores active body and flex scopes; an inference trial still owns its usual
diagnostics and cache journal. Compiler invariant failures remain compiler failures.

Pure Store execution resolves the destination through all caller binding layers
and already selected flex expansions, using the same shared traversal as purity
classification and code generation. The right-hand side still evaluates in its
original binding stack. An intermediate replacement parameter is not a new local
destination when it forwards a caller variable.

Section-flex inference assigns caller-body execution to one explicit transfer
site or to the implicit continuation after its replacement. Pure evaluation
consumes this static classification: an explicit transfer can execute zero or
many times in a loop, and does not become an implicit continuation when its
loop has no iterations. A function return from a caller body propagates before
checking the enclosing loop or conditional's normal header outcome. Distinct
duplicate transfer sites remain inference errors; runtime iteration does not
create another static transfer site.

Local storage is identified by its source declaration and the instantiated body
that declares it. Expression clones retain their owning body; nested bodies link
to their parent, and flex roots link to the call-site body. Resolving a caller
binding preserves the referenced expression's storage identity. Pure execution,
native allocations and finalized local types use this same identity, so nested
uses of one definition cannot overwrite each other's local storage. Each pure
flex invocation resets and restores only its own local keys.

Type phrases describe types. Direct value construction is explicit: `a new {type}` value-initializes a concrete runtime type, while `a new {type} from ...` supplies constructor values. `nothing` is not constructible. `return nothing` is the sole natural-language exception: it is a valueless return, whereas `return the type nothing` returns the compile-time type value.

`(print line value) + other` is incorrect because printing produces `nothing`, which numeric `+` does not accept.

We know this because we instantiate `print line value` and walk over the code just like we do with the code in the main section. We store the return type so we do not have to instantiate functions with the same (possibly incorrect) combinations again and again. We assume functions always return the same type for the same argument types and constants.

All instantiations of a function have the same operand reordering for each code line, but can use different overloads. The first valid instantiation determines reordering.

We reuse the same strategy (code) for flex functions where possible, keeping it DRY.

Each function body is contained in one source file. Includes at file scope may
join complete definitions; they must not insert statements, case labels or closing
braces into another file's function body.

Node handling, conditional branch merging, overload selection and flex-entry
bookkeeping have separate, non-inlined call boundaries. This keeps their scratch
space out of traversal frames for other expression kinds and recursive callees.
These boundaries preserve the compiler's native stack budget, including the
Windows default stack.

The grouping candidate generator also has a non-inlined entry point. This keeps
coroutine allocation elision from placing its suspended enumeration state in
the recursive inference stack.

Unary arithmetic intrinsics require numeric scalar or vector values. Classes, pointers, arrays, matrices, booleans, and type values are rejected during inference, before code generation. Floating-point vector negation uses the element type to select floating-point instructions.

## Operand Reordering

Binary infix patterns may declare `associativity: left` or `associativity: right`.
This is a syntax-family rule shared by typed overloads and choice spellings;
contradictory declarations are errors. It determines grouping, not algebraic
equivalence or reassociation by an optimizer. The rule applies to directly
connected parent and operand operators: an intervening function starts another
chain. Only binary infix uses of a family participate in its chains; prefix
choice spellings remain capture boundaries. Explicit operand parentheses take
precedence. The standard `+`, `*`, `and` and `or` families group to the left.
Unspecified families retain ambiguity checking. The grouping complexity limit
counts operators with unconstrained relative order, so declared chains can be
long without being classified as ambiguous.

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

A successful candidate keeps its complete inference transaction alive while the pull enumerator checks whether another candidate exists. If the enumerator finishes, that final successful transaction is promoted directly, including all nested subgrouping transactions; the one-candidate case follows the same path. If another candidate exists, the retained transaction is rolled back before that candidate is inferred. A later successful candidate replaces the retained transaction only if its complete grouping snapshot is identical, including argument subtrees and explicit-group flags. A different valid grouping inside an enclosed argument is an ambiguity even when the surrounding call is unchanged; the first valid complete grouping is retained.

A candidate whose validity depends on an unresolved recursive call is deferred, rather than counted as a second passing tree. A resolved alternative can establish the return type needed for the normal body reinference pass to check those deferred alternatives. Dependency observations belong to the individual candidate.

Nested trials and cached-grouping inference borrow the caller's fixed roots through parent-linked scopes. They collect only their own resolved roots. Type requests through flex bindings honor all active scopes, so the caller's selected expression tree cannot be regrouped behind its reference. Each scope is restored before its owning transaction or inference call ends.

Replaying a selected snapshot fixes only nodes actually present in that snapshot. Callee body groupings are journaled separately and must not be frozen after their transaction has been rolled back. A committed code line or an already-ordered cloned header fixes every node of its own snapshot, even when an enclosing caller supplies a different fixed-root scope. This keeps independently valid subexpressions from being regrouped into an invalid parent condition during reuse.

We do not clone the expression tree for reordering; we reorder it. Even when storing the correct state and continuing to search for the next valid state so we can give ambiguity warnings, we store our choices instead of cloning the expression tree.

reusable expression trees are cloned only to own per-usage inference state: once for each function instantiation and flex expansion. the original tree is unmodified.

Each instantiated body owns its cloned nodes independently of the mutable argument tree. Replacing a body releases those nodes when its last user ends. All inference-created expression storage goes through the inference context. Trial journals retain provisional bodies and standalone expression storage until rollback or promotion finishes, including conversion probes, section result constraints, body fields and receivers, and when nested journals are absorbed. A rollback restores borrowed expression and instantiation state before releasing that storage. Saved execution states retain the body and lexical owner chain of their subject assignment until the state is restored or discarded; owner references do not participate in subject identity or fixed-point comparison.

User conversions are ordinary unary pattern calls in the inferred tree. Candidate probes borrow the source expression
and use the trial journal to restore it; they do not clone its subtree. A committed conversion moves the existing source
root under its call and retains the argument storage through rollback or promotion. The call keeps the source's statement
placement and execution metadata. Grouping reads the authored operand beneath inserted calls, so its snapshots contain
only source operators. Type, value, memory, and code-generation passes consume the same ordinary call tree. There is no
conversion side link or late conversion-materialization pass.

We do not use pointers to expression-pointer locations (`expression**`), since those can be dangling pointers.

**ALL** functions can be seen as one of these 4 categories of operators.

The **ONLY** thing we check for this is:

- Does the operator start with an argument?
- Does it end with an argument?

All other arguments are **IRRELEVANT**. We can never start or end with 2 arguments, since `$$` would just merge into one name when the user supplies two concatenated names `arg1` and `arg2` as `arg1arg2`. A space (` `) is a **SEPARATOR**, a pattern element of type `other`.

Prefix operator:

```text
not $
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

Program optimization defaults to `-O2` for all compilation entry points. Explicit
optimization flags override that default; `-g` selects debug information without
changing optimization, while DAP compilation explicitly requests `-O0`.

We already know which patterns call which instantiations, the type of every variable, etc. But now, we branch off into compilation target: browser, machine code, SPIR-V, etc.

Conditional alternatives nest on the previous condition's false edge. Each `else if`
owns a merge block that forwards to its enclosing merge, while execution after the
chain continues at the original `if` exit. This preserves nested selections for
SPIR-V and its WGSL/GLSL translators. Terminating alternatives remove unreachable
forwarding blocks before the section exit is finalized.

Array literals and class construction compose SSA aggregate values directly, allowing
LLVM to fold static operands into constant aggregates. Named data declarations refer
to their inferred initializer instead of mutable variable storage. `static reference`
publishes an immutable global before lowering its initializer, allowing cyclic static
graphs; the initializer must lower to a constant. `deferred body` uses ordinary native
callable generation, while the body record and receiver intrinsics use their inferred
expressions. There is no runtime data-section builder pass.

Calls with an inferred pure, compile-time-known result materialize it directly
when their runtime operands lower to LLVM constants, using the same scalar emitter
as fixed parameters. Arguments are evaluated first and their temporary lifetimes
are released normally, preserving effects from argument expressions. Static
aggregates can therefore contain values computed by ordinary pure functions.
Operands requiring loads or runtime instructions retain the selected function call.
Inference-time loop observations alone do not justify replacing runtime computation.

Class declarations and synthetic section-body records retain their declaration
ranges for debug information. Debug types are cached per class instantiation;
temporary composite types allow recursive pointers to refer to the completed
record. Global variables carry the resulting debug expressions. Entering a new
generated function clears the caller's instruction location, so an empty or
fully folded body cannot inherit the caller's debug scope.

Raw pointers are non-owning, regardless of their pointee type or pointer depth. Managed-lifecycle classification checks
the pointer representation before inspecting array elements or class fields. Owning array values retain elements in forward
order and release them in reverse order; pointer copies, arguments, returns, and fields do not retain or release their pointees.

Lifecycle inference follows runtime expressions from main, exposed functions and their selected callees. Type-only
operands do not make their class lifecycle bodies reachable. Runtime owning values, nested owning fields and explicit
destruction do; lifecycle bodies can in turn require more runtime types. Retain/release declaration pairing is validated
independently of runtime use. Intrinsic type-only operand classification is shared with memory diagnostics.
Resolved data-field values and symbolic callable constants participate in this graph:
their generated arrays and callbacks can own managed values even when the containing
description is only emitted as static data.

External call signatures come entirely from their inferred DynLex operands; code generation does not identify or special-case
library function names. `@intrinsic("call", library, function, return type, arguments...)` emits a fixed signature.
These C-ABI calls are available for native CPU and WebAssembly output. Intrinsic target metadata rejects them during
SPIR-V inference, before code generation.
`@intrinsic("variadic call", library, function, return type, fixed argument count, arguments...)` emits the first
`fixed argument count` argument types as the fixed prefix and marks the remaining parameters variadic. Only the variadic suffix
receives C's default argument promotions: booleans and integers narrower than 32 bits become 32-bit integers, 32-bit floats become
64-bit floats, and pointers remain pointers. Platform-sized C types are expressed by standard-library type patterns built from
compile-time build information.

`target is` accepts execution backends (`cpu`, `wasm`, `gpu`) and native operating systems (`windows`, `macos`, `linux`).
Operating-system predicates use the same native host target as code generation and are false for WebAssembly and SPIR-V.
These are compile-time predicates, so platform-specific native calls in unselected branches are not inferred or linked.
`lib/platform.dl` exposes the operating-system checks as natural-language patterns.

Runtime-loaded native symbols use `@intrinsic("call pointer", callee pointer, return type, arguments...)`. The callee is an
opaque runtime pointer and the return type is a concrete compile-time type; the remaining operands define its fixed, nonvariadic C
ABI signature. Native pointers identify callable addresses; WebAssembly pointers identify entries in the module's function table.
Both pointer calls and typed `invoke` calls support CPU and WebAssembly targets; SPIR-V rejects them. Library authors
must ensure the runtime pointer and stated signature match, and should keep this intrinsic inside a natural-language wrapper pattern
that obtains the symbol from the relevant loader, so application code calls the wrapper rather than spelling ABI metadata at each use
site.

Atomic scalar access uses `@intrinsic("atomic load", pointer, order)`, `atomic store`, `atomic exchange`, `atomic fetch add`,
and `atomic fetch sub`. Orders are literal `relaxed`, `acquire`, `release`, `acq_rel`, or `seq_cst`; loads cannot use release or
acq_rel, and stores cannot use acquire or acq_rel. Operations are CPU-only. The pointee must be a concrete naturally aligned scalar:
Boolean, 8/16/32/64-bit signed or unsigned integer, 32/64-bit float, or pointer; fetch add/sub only accept integers. Boolean storage
is one byte. The caller owns correct scalar storage alignment and must not mix atomic and non-atomic concurrent accesses. `lib/atomic.dl`
keeps ordering metadata in natural-language wrappers, including default sequentially consistent and relaxed/release/acquire forms.

JSON number nodes retain their original validated decimal lexeme for serialization. `the signed 64 bit JSON integer read from value`
and `the unsigned 64 bit JSON integer read from value` return typed result values with a success status; they parse that lexeme
without a floating-point conversion and reject wrong JSON kinds, fractional or exponent syntax, negative unsigned syntax,
and out-of-range magnitudes. Signed `-0` reads as zero, while unsigned `-0` is rejected. `a new JSON number from` signed or unsigned 64-bit integers writes the exact
decimal lexeme. The existing `number` member and JSON-number getters remain approximate floating-point views for compatibility;
they are not an integer interchange API.

Native callable definitions and external-call declarations and call sites apply the same narrow-scalar ABI extension attributes.
Booleans use `zeroext`, and 8/16-bit integers extend according to signedness. Non-Darwin AArch64 (AAPCS64) leaves these scalars
unextended; Win64 extends only Booleans. Internal pattern calls keep their separate by-reference calling convention.
Internal function generation preserves each runtime argument's storage address space and caches the resulting ABI on
the inferred instantiation. Forwarding a global reference retains its original storage; local and global references can
therefore select distinct LLVM functions for the same inferred argument types.
A Boolean's LLVM `i1` type alone does not guarantee a canonical C Boolean.

WebAssembly object emission is followed by the pinned LLD WebAssembly driver, embedded in both native and browser
compilers. Each link consumes one compiler object and uses one thread, including in single-threaded Web Workers.
The linker resolves all code and data relocations, exports `main`, and reserves the first 16 bytes for null.
Static data precedes the aligned 8 MiB stack, and the immutable `__heap_base` export identifies the first heap address,
including any BSS that has no active data segment. The module defines its own stack pointer and exports its function table as
`__indirect_function_table`, so host imports can retain and invoke typed callbacks after `main` returns. The
browser runtime supplies linear memory and host functions, reads that linked heap boundary, and allocates from it;
it does not compute another stack or static-data layout. Relocatable objects are never treated as executable modules.

LLVM expands wide arithmetic according to the selected target. The browser runtime
provides LLVM's i128 compiler-runtime arithmetic and numeric conversion imports
using exact BigInt limbs. Floating-point conversions round directly to the target
precision, including f32 halfway cases, and wide results use the WebAssembly sret ABI.

Function references retain their symbolic definition, pattern path and inferred
callable instantiation as compile-time values. Pure calls and address casts preserve
that identity; code generation materializes the callable symbol when emitting a
constant. Static callback tables therefore remain constant data when their entries
use ordinary library patterns. Host addresses and WebAssembly table indices are
assigned by linking rather than guessed during inference.

SPIR-V vertex-to-fragment interpolants are identified by arbitrary non-empty semantic names. Each independently compiled shader
stage encodes those names into valid interface identifiers and assigns locations in lexical semantic-name order, so matching
vertex outputs and fragment inputs receive identical locations regardless of source order. The standard shader library exposes
the four floating-point components while the intrinsic layer owns the stage restrictions and interface storage classes.
The SPIR-V 1.4+ entry-point interface contains all emitted module globals, including private variables, while excluding
function-local storage. The entry-point checker validates emitted shader modules with `spirv-val --target-env vulkan1.3`.

# Incremental Native Builds

Native executable file builds use incremental compilation automatically. The default
cache lives under `dynlex/compiler` in the operating system's user cache directory:
XDG cache storage on Unix, Darwin user cache storage on macOS, and LocalAppData
on Windows. `--cache-dir <directory>` overrides that location, `--no-incremental`
performs a clean build, and `--incremental-stats` reports the actual cache directory
and reuse. Cache overrides and statistics apply only to native executable file builds.
A complete native result is reusable while every filesystem observation remains identical. Source edits that
only add, remove or change comments or trailing whitespace also reuse the result when
debug locations, execution traces and diagnostics cannot become stale. The semantic
comparison uses each source file's configured comment prefix and retains line boundaries
between executable text. Observations include source/config contents, canonical source
identities and failed lookup candidates; a newly created higher-priority import or
configuration therefore invalidates the result.
The cache records structured warnings and notes, including source locations and
configured message keys, variants and placeholders. Comment edits relocate their
source locations without rebuilding the frontend. An edit confined to messages in an
existing `config.dl` rerenders those diagnostics without recompiling.
The program entry is a small manifest of observations, diagnostics, object keys,
and content-addressed keys for generated LLVM bitcode, function projection, dependency
graph, inference summaries, native units, and optimization-unit inputs.
Unchanged and comment-only builds inspect the
manifest without parsing these larger payloads; they load LLVM bitcode only if an object
must be rebuilt. Executable source edits first load the function projection.
They load the dependency graph, inference summaries, and generated IR only if type
inference must run again. Missing optimized objects load only their own optimized bitcode.
`cache_payloads_loaded` reports the
number of payloads read on a build.
An evicted manifest payload invalidates the program entry. Evicting an individual
optimized unit or object only affects builds that need that artifact.
Failed builds that reached section analysis or later store diagnostic records and
failed pattern lookups separately from the last successful native result. An
unchanged source or message-only configuration edit replays the errors without
rerunning the frontend. A later code edit reuses a failed lookup only while its
source and candidate pattern tree are equivalent; a successful build retires the
failed snapshot.
The compiler linker build ID (or executable content digest when the format has no
build ID), effective compiler options and host CPU features identify the computation. Modification timestamps are never a correctness criterion.
The cache's size limit evicts entries by successful access time. Reading a
verified entry refreshes its cache-file timestamp, so repeated reuse keeps hot
optimization units ahead of older results that no longer participate in builds.
Validating a program manifest also records access to its payload references,
without loading their contents. Comment and unused-body edits can therefore
retain lazy payloads under cache pressure without evicting their current result.

After an executable source edit, the frontend reconstructs sections and pattern trees.
The native cache restores unresolved expression trees for unchanged expression text
before section analysis and for temporary type-constraint expressions during inference.
It also restores parsed declaration elements before pattern resolution. Every range
is rebased onto the current logical line. Ordinary expressions register their current
section's pattern references and explicit captures; temporary expressions keep their
references unregistered and resolve against the current tree when evaluated. Both
restore source tokens. Expressions that emitted parse diagnostics are parsed again.
`--incremental-stats` counts expression and definition parse hits and misses from
the preceding compiler invocation, including temporary expression hits separately.
After resolution, each successful type-constraint probe's pass and the sections
read by its expression are saved with the function projection. Variable
definitions are mapped to their owning sections; their source ranges can lie
on a function's header line, which belongs to its parent section. A later build
reuses unaffected first-pass probes before the first affected item when declarations,
pattern trees, precedence, scope rules, and source outside function bodies are
unchanged. The saved dependency graph also invalidates a probe when an edited
body can reach another candidate that could match its expression. Later-pass
probes and probes after the first affected item run again so a changed
constraint can resolve earlier and cascade in order.
An edit confined to top-level execution can retain a probe whose recorded
dependency roots exclude the top-level section. The nonexecution source
projection checks that declarations and other ambient code are unchanged;
probes that read top-level declarations, or call a function that reads globals,
run again through the saved reverse dependency graph.
Adding an ordinary function retains saved probes when prior function declarations
and scope rules are unchanged, the source outside functions is unchanged, and the
new pattern cannot match any saved constraint text or current pattern reference.
Potential matches rerun the probes. Global declaration identities use positions
within their source files, so adding lines to an earlier imported file does not
change the identity of an unchanged global declaration.
The cache also records resolved constraint values and their dependency roots. When a
probe is reusable and its result has a portable type identity, the compiler
installs that result directly in the current declaration and carries
forward its dependency roots. It also skips parsing that constraint expression when
the saved probe schedule still matches. If the schedule changes, omitted expressions
are parsed before inference. Other results are committed by inference as before.
Class references are keyed by stable class-section paths. Committed class
instantiation field types and function signatures are saved with the constraint
probe schedule. When the class declaration still has the same fields, the
compiler restores its instantiation table before decoding reusable constraint
values, including recursive class layouts. An edited class declaration makes
its saved constraint values ineligible for reuse.
`constraint_probes_reused`, `constraint_probes_run`, `constraint_values_reused`, and
`constraint_expressions_reused` report these separate decisions. The six entries
of `inference_stage_ms` measure declared constraints, pattern conflicts,
incremental preinference, root inference, exposed and managed functions, and final
variable and return validation, in that order.
It reuses pointer-free matcher results from the previous build in both the initial
resolution and precedence matching passes for unchanged references when the active pattern definitions, the reference's
source line and position mapping, relative lexical declaration visibility, and
non-message syntax configuration have the same fingerprints. Matcher keys cover wildcard patterns and
literal starts that could occur in the reference, in its own pattern tree and the
function tree used for nested matches. Adding an unrelated pattern can therefore keep
matches in unchanged calls, even when the added source line shifts their line numbers.
Definition fingerprints use pattern content and order without absolute line numbers, so
inserting an executable line before unchanged definitions also keeps their matches.
Matcher results live in a separate content-addressed cache entry with a compact index
and individually encoded results. An unchanged or comment-only build reads only the
program manifest, so it does not parse matcher results merely to relink the executable.
An evicted matcher entry causes
matching to run normally on the next code edit.
Successful matches are rebound to the new tree and expression objects. Failed matches
restore their trie dependencies, so later definition insertions still retry them.
New or removed matching overloads change the relevant tree fingerprint and invalidate
cached matches from that point in resolution. Precedence-pass results also include
the syntax families participating in associativity or precedence reachability in
their fingerprint, so a changed precedence rule cannot reuse a stale grouping and
an unrelated definition with no precedence relation leaves the fingerprint intact.
Within a compilation, a newly indexed more-specific definition requeues callers of
less-specific patterns only when at least one canonical path can occur in that
reference's matcher input. Missing literal words exclude a path; numeric arguments
use their matcher spelling. The specificity search uses the definition's indexed
canonical paths after insertion. This keeps unrelated callers resolved during cold and
edited builds while retaining rematches for genuinely competing patterns.
Body references whose parsed elements do not contain a pending implicit
parameter candidate wait until definitions are indexed. A substring of another
word is not a candidate. They resolve once against the completed tree, avoiding
failed matches and later specificity retries. A single-word alternative can
still promote an implicit parameter from its body,
so early indexing alone does not make its references eligible to wait.
Implicit-parameter candidate counts use one section-tree walk. Each body
reference contributes once to candidate words in ancestor definition sections,
including nested definitions. `variable_like_count_references_scanned` counts
those references.
An ordinary nested function has no implicit closure. Validation reports a
reference to an outer runtime local and directs the author to pass it as an
argument; global variables remain accessible.
For an unchanged ordinary function body, an edited build can replay the saved
matcher key for those deferred references after checking the indexed pattern-tree
signature, declarations of reusable functions, and source outside their bodies.
When that whole-tree signature changes, the cache compares saved digests for
each set of required literals in the indexed paths. An unchanged top-level
function may still replay its reference keys when its declaration, surrounding
source, and root lexical declarations are stable, and each changed path requires
a literal missing from its header, body, transformed references, and numeric
arguments. A changed path with no required literals can affect every body.
Potential matches compute keys normally, so newly relevant overloads and failed
lookups rematch.
Path snapshots are stored by compiler and tree fingerprint. An edit that keeps
the initial and final trees identical reuses their fingerprints without reading
or rebuilding the path lists. A changed tree loads the prior lists on demand.
`matcher_path_snapshots_loaded` and `matcher_path_snapshots_built` count that work.
The matcher still restores the result and its dependencies; changed bodies and
changed matching environments compute
their keys normally.
`body_resolution_reference_visits` counts references examined by the resolution
loop, including restored matches. `body_resolution_match_cache_hits` counts
restored successful and failed matcher results, while
`body_resolution_match_computations` counts actual matcher executions. The
remaining visits include explicit-variable handling and references waiting on
unchanged failed-match dependencies.
The body-reference ownership counters split parsed references by their nearest
definition. Ordinary functions are grouped by whether this compiler process
instantiated them; a function whose IR was imported from cache therefore appears
in the uninstantiated group. Flex, conversion, and other definitions are counted
separately. These counters expose the work still done for bodies whose code was
reused without current-process inference.
Unchanged deferred references in ordinary functions can also restore their initial
resolution as a unit after declarations are indexed. References that classify
implicit parameters run first. Reuse requires unchanged source, declarations and
lexical bindings, and a matching environment that cannot affect the saved results.
The existing binary matcher cache stores each unit's reference indices, direct
variable outcomes, exact source spans and positive or failed lookup keys. The
JSON projection stores only the unit's content key. Restored matches and variable
references rejoin the ordinary expansion and precedence-invalidation machinery.
Bodies with prior diagnostics run the normal resolver.
`body_resolution_units_reused` and `body_resolution_references_restored`
count this restoration, separately from references evaluated by the resolver.
For a successful prior build, unchanged, uninstantiated ordinary functions without nested definitions can
skip resolution when their parsed declaration already equals the prior resolved
declaration, their prior matcher entries remain available, and their patterns
cannot alter another reference's matcher key while definitions are indexed.
The source projection and body digest establish that their text is unchanged.
If a later match calls one of these functions, or the completed pattern tree
differs, its references rejoin the resolver. Other selected bodies replay their
complete cached resolution after indexing, before expression expansion and
variable-group materialization. This preserves valid body trees for later
function-value queries and keeps lexical bindings available to validation and
inference. `inactive_body_resolution_references_skipped` counts the early
resolver visits bypassed by this replay. This path runs only when the potential
skipped work is at least one twentieth of the program's references, so its
selection work does not dominate small edits in larger programs. Pattern-tree
keys combine active definition contributions independent of insertion order;
removing and readding the same definition restores its earlier key.
The cache records matcher input literals as hex-encoded byte sequences by
source file. When the initial pattern tree is unchanged, an edited build scans
references from changed files and carries the unchanged files' literal sets
forward. It considers both the previous and current literals when selecting
inactive bodies, then writes the current sets for the next build. A changed
initial tree rebuilds the index from current references.
`inactive_body_selection_ms` measures this decision.
Unchanged references replay saved precedence-pass keys when their first-pass
keys and precedence families still match. If the final indexed tree changed,
the cache compares the changed paths' required literals against each
owning function before replay; a potentially affected function rematches.
Changes that alter precedence rules or participating families also rematch. A precedence
rebuild never carries replay keys into a later pass.
An edit confined to the bodies of ordinary functions that had no inferred
instantiation in the prior build can stop after pattern resolution and validation.
An unchanged inactive body with no remaining implicit-parameter candidates can
skip resolution even when its pattern begins with a word used by other calls;
its declaration cannot change through body-driven parameter promotion.
The cache compares declarations and all source text outside those bodies. It
compares the current preinference diagnostics with their saved records and
reuses later warnings when their source files were not edited; pending scope-rule
warnings still prevent this path.
For inline functions, the source projection masks the mapped body span while retaining
the declaration on the same physical line.
It refreshes body and resolved-match fingerprints for edited inactive functions,
then reuses the prior inferred result and linked objects. Active function edits
traverse type inference again; eligible unchanged instantiations reuse cached summaries.
Blank and comment-only logical lines do not contribute to function-body fingerprints.
When inference is reused, the saved constraint-probe schedule and class instantiations
remain attached to the next snapshot even if its frontend projection was rebuilt.
A compile-time-folded function may have no emitted LLVM body. When its caller's
LLVM body is reused, the snapshot retains the folded callee as active through
the saved section-dependency graph, even if that callee was not instantiated
again during the current build.

For native builds without debug or execution tracing, the compiler
can import cached generated LLVM bodies of unchanged functions after a code edit at every
optimization level. Generated bodies are saved after target attributes and verification,
before LLVM optimization. The program saves that generated module once. Each optimized
unit stores its bitcode separately for native lowering and object regeneration.
Optimized native builds form units from symbol groups constrained by aliases, COMDATs,
resolvers and block-address users. Each unit records its raw definitions, referenced
declaration interfaces and module environment. Unchanged units restore bitcode with its
ThinLTO summary without cloning the module again. Changed inputs run the ThinLTO pre-link
pipeline before summary construction, so import decisions use simplified body sizes.
The combined ThinLTO index supplies
optimization cache keys that include imported callees, linkage decisions and target
configuration. A changed callee therefore reoptimizes affected importing units. The
single-worker backend stores optimized bitcode and a separate summary of each unit's
definitions and references. The native emitter follows these summaries from the
original exported roots. It omits units whose callers were fully inlined, without
combining their LLVM modules or lowering their bodies. Unit preparation keeps
link-once definitions available to other units; the root decision still uses the
original linkage. References use target symbol names, including references from
module assembly. Imported bodies that native code can still refer to contribute
their dependencies; unused imports do not keep other units live.

Each live optimized unit's bitcode and target configuration identify its native
object query. An unchanged optimized result skips native lowering even when its
input had to be optimized again. A missing object can be regenerated from that
unit's bitcode. If that bitcode was also evicted, only demanded units reenter the
optimizer. Object keys identify the emitted bytes, so an unchanged native result
can stop invalidation before linking.

`ir_generation_ms`, `generated_ir_snapshot_ms`, `optimization_input_ms`,
`optimization_index_ms`, `optimization_backend_ms`, `native_reachability_ms`, and
`native_unit_ms` separate these costs. `optimization_input_units_built`,
`optimization_input_units_reused`, `optimization_units_built`, and
`optimization_units_reused` expose input and optimization reuse. Index construction
still visits the input set on active edits. `native_functions` counts function
definitions in live units; `native_functions_lowered` counts those handed to native
code generation. `native_unreachable_units` counts optimized units that need no
native object.
The compiler compares
each function body separately, so changing one function does not invalidate its
unchanged neighbors in the same file. A newly added or removed ordinary function
does not invalidate an existing function when its own declaration, body, and
resolved reference matches are unchanged. The cache compares nonfunction
declarations and executable source outside ordinary function bodies separately;
changed overloads invalidate functions whose resolved matches change. Calls to other generated functions form a
reuse closure: every cached callee must also be unchanged. Cached functions that
read mutable globals are linked to the current globals only when their LLVM types
and initializers match. Class layouts use structural LLVM structs so
cached function imports do not renumber type identities or invalidate unrelated
object partitions. Reused function IR
retains the native libraries required by external calls, so a caller edit cannot
drop a link dependency.
Dependency reachability handles recursive call cycles; an unchanged cycle remains
reusable when none of its reachable functions changed. The compiler scans each
cached function's LLVM dependencies once per build, including references in
global initializers. The `cached_ir_dependency_scans` and
`cached_ir_dependency_hits` counters track that work. Internal function names
include the stable definition identity, concrete argument layouts, and the
destination of compile-time type arguments. Exported callable names keep their
ABI spelling.
Successful native builds persist direct section dependencies from resolved matches
and inferred call selections, with reverse edges for invalidation. When the next
build has the same matching environment, an edited section marks only its reverse
reachable callers dirty; a preinferred section with an unchanged interface stops
that propagation. Inference variants restored from cache carry their prior call
dependencies forward, while newly inferred variants capture current calls.
Imported LLVM functions retain their prior variant dependencies when their bodies
were not re-inferred.

When a body edit preserves its inferred interface, the compiler can reuse the
entry point's inference and IR. This checks the top-level execution source,
matching environment, typed constraints, class layouts, diagnostics, and the
cached entry's LLVM dependency closure. The current proof covers native entries
with no exposed functions or managed class lifecycle declarations. The
`root_execution_reused` counter records when the entry was imported.

The graph follows instantiated replacement bodies, caller bodies, static loop
iterations, and data expressions, including compile-time-only callable queries.
A callable lookup records its spelling, source visibility, visible candidates,
and the sections or declared constraint probes that read it. Inference records
the lookup even when a conversion or grouping trial is rejected. An empty
candidate set is a dependency, so adding a visible definition can invalidate
the earlier rejection. A definition local to another file leaves the lookup
valid. A conversion probe also records the body it tested, so changing that
body can make a previously rejected conversion applicable. Deleted sections
propagate invalidation through their saved reverse edges. The
`callable_lookups_checked` and `callable_lookups_changed` counters measure
validation after a pattern tree edit. Rejected trials and restored constraints
keep the bodies they read active across edited builds, even when those bodies
have no emitted IR.
The graph speeds preinference and codegen
eligibility checks; section parsing and resolution still rebuild their state.
Inference-summary payloads store structural type nodes in a shared table.
Argument and return types refer to those records by number, including nested
class layouts, array elements, and callback signatures. Loading restores the
same identities used by inference validation and summary keys.
The graph payload stores section paths once and indexes forward, reverse, and
per-instantiation edges by path number. Variants whose compile-time arguments
cannot form an inference-summary key store the union of their selected edges
under the reserved `*` variant key. Importing their IR retains those edges, so
successive edits do not invalidate unrelated callers. A reused inference or IR
variant without its saved edges is an internal inconsistency.
Loading it restores the same graph used
by the dependency checks while avoiding repeated long path strings in the cache.
Section cache identities use source URI, normalized header, section type, and a
same-identity sibling ordinal. Inserting a different definition before an existing
function leaves that function's identity and cached inference or IR intact.
Declaration fingerprints include each resolved pattern element's classification.
When a body edit removes the last use of an implicit parameter, the changed
declaration invalidates saved caller matches even though its spelling is unchanged.
When adding or changing a declaration prevents whole-program declaration reuse,
unchanged top-level functions can still reuse their saved declaration entries.
This checks their body and header, syntax configuration, scope rules, and the
required literals of paths changed in the initial and final indexed
pattern trees. Functions that declare root globals also require the same root
binding positions; other functions cannot bind those globals. The whole-program
declaration fingerprint includes new entries.
When the declaration and surrounding source projections are stable, an edited
build carries resolved-match fingerprints forward for unchanged function bodies
and recomputes fingerprints for edited bodies or changed matching environments.
Edited bodies retain a fingerprint
per reference and the matcher dependency key that produced it. A reference with
the same key after a later edit reuses its fingerprint; changed references are
fingerprinted again. The function fingerprint is assembled from those reference
fingerprints, so a later overload addition can still compare the edited body.
Precedence matcher-key replay compares the current matcher tree and a compact
signature of precedence-bearing declarations with their saved versions. When
both agree, it carries forward the saved full precedence fingerprint. A rule
edit recomputes the full fingerprint before any saved final matcher key is used.
If an added literal pattern cannot occur in any precedence-bearing declaration
or its rule text, the saved fingerprint also remains valid; wildcard changes
still trigger the full check.
If top-level execution changes but its global declarations keep the same names
and locations, directly declared functions also retain their resolved-match
fingerprints. Their inference and generated code still follow the ordinary
dependency checks for changed global values.
When only top-level execution changes, the all-function projection first compares
function-body digests, indexed pattern-tree signatures, precedence, global declaration
identity, and source with top-level execution masked. If those inputs agree, it
retains saved declaration and match fingerprints and refreshes only source views
that include execution. The reuse count is reported by `projection_top_level_sections_reused`.
These source projections use the configured comment scanner and ignore blank
lines and trailing whitespace, so a comment-only build does not invalidate the
next executable edit.
Each file's projection also stores its source digest and the section-mask
fingerprint. An edited build reuses the projection of an unchanged imported file
when both fingerprints match. `--incremental-stats` reports reused and scanned
source projections; semantic compatibility compares the projected text digest,
so a comment insertion does not invalidate the next executable edit.

For active edits, the native cache also persists inference summaries for functions
with concrete runtime argument and return types, including class pointers. A pure
function returning a value reuses its summary when its call arguments are not all
known at compile time. A call with known arguments uses a saved return value when
available; otherwise it infers the body for compile-time evaluation.
For a pure function without global access or matched function dependencies, the
cache also stores scalar return values by scalar argument values. A repeated
known-argument call can use that value without reinferring the body; a new
argument still infers the body and evaluates the call normally.
Summaries record return address provenance and storage-parameter relationships, plus
writes, known constants, address provenance, and address escapes for global variables.
Storage origins retain their lexical declaration and logical invocation across compiler
processes. Ordinary invocation identities include the owning function and its typed
instantiation key. Flex invocations add their parent invocation, source expression,
and argument-pack iteration. Body recreation retains the ordinary invocation's storage
scope. Source-expression identities ignore comment lines and preceding unrelated
statements, so moving an unchanged invocation preserves its cached effects.
Type-constraint probe identities also follow their declaring section and source
declaration. Moving later declarations after an earlier statement is removed preserves
their probe schedule and inferred constraint values.
Class type identities include the current instantiation's field types; a changed layout
prevents reuse. A summary that accesses globals requires the same structural global
types as the previous build. A summary
is reused only if its source body, pattern declarations, every matched function
dependency and its cached IR closure are unchanged. Other instantiations still
run ordinary inference. A function in a file with diagnostics gets no inference
summary, so its warnings are regenerated. Functions imported from cached IR count as active when
deciding whether an edited body can be ignored. This prevents a later callee edit
from reusing the preceding build's type inference and executable.
An edit to top-level executable code can retain global-reading function summaries
when those functions and their dependencies are unchanged. Replay compares the
current inferred global types; IR import separately compares the emitted global
types and initializers. The changed top-level body is inferred and generated.

For functions with one pattern path, restorable concrete argument types, and no transitive
global access, the compiler can infer affected instantiations in dependency order.
It compares every instantiation's return type and execution effects with the prior
summary. An equivalent interface stops invalidation at that function: higher callers
retain their inference summaries and LLVM bodies, while the function is generated
anew and linked to them. A changed interface continues through callers until an
equivalent interface is found or the entry point is reached. This path compares
inferred results rather than guessing from edited source text.

At O0, native emission divides the generated IR
into deterministic, independently linkable partitions. Local symbols receive a stable
program namespace and hidden external linkage. Aliases, resolvers, COMDAT groups, and
block-address users remain together. The native dependency snapshot records each
partition's definitions and referenced declaration interfaces, including attributes,
metadata and named types. Reused function IR retains its symbol dependencies. Global
initializers and module settings are fingerprinted separately. An unchanged partition
reuses its object before cloning or printing its IR; only affected or evicted units
construct a partition module. `native_partition_modules_built`,
`native_partition_modules_skipped`, `native_function_dependencies_reused`, and
`native_dependency_ms` expose this work. The native snapshot is a separate cache payload
loaded only when native partition processing is required.
Interned string literals use content-derived names and hidden ODR linkage, with
COMDAT selection on supported targets, so importing cached bodies preserves the
same literal objects instead of introducing renamed duplicates.
Each affected O0 partition's complete IR and target
configuration identify its object cache entry; code changes, changed declarations,
inlined callees, globals and debug metadata are reflected in that identity. Unchanged
objects are reused. The linker receives multiple object paths through a response file.
If the ordered object keys and required libraries are unchanged,
and the existing executable still matches the recorded output digest, an edited build
skips linking. An unchanged, comment-only, or message-only configuration build also
skips linking while that output identity matches. This is native partition reuse, matcher query reuse, the unused-body
fast path, function IR reuse, and inference-summary reuse. The last two paths skip
inference and code generation for eligible unchanged functions during active edits.
The program cache records its ordered object keys. An unchanged or
comment-only build with a missing or changed output links those cached objects directly
without parsing or splitting the LLVM IR. If an object was evicted, the saved IR
regenerates the missing unit or O0 partition before linking. The cache tracks DynLex source,
compiler, and output identities; changes made solely to native linker inputs are
outside its invalidation model.
After an executable edit, a successful frontend and code generation pass whose
LLVM IR is identical to the prior result also reuses the cached object keys, and links
only if the required libraries or output identity differ.
This still validates the edited source and updates diagnostics and matcher data.

Cache entries carry BLAKE3 integrity checks and are published with atomic rename.
Invalid entries and I/O failures are explicit errors. Linking uses private temporary
copies so cache eviction cannot remove linker inputs. Entries are limited to 256 MiB;
the disk cache is trimmed to 512 MiB after a build. The LSP and other output modes retain
their ordinary compilation pipeline.

# LSP Interaction

When hovering over an expression, the already-evaluated compile-time value and type are shown.

Compiler ranges and source slices use UTF-8 byte offsets. Editor-facing APIs use
UTF-16 columns: incoming edits and cursor positions convert to bytes, and outgoing
ranges, semantic tokens, web diagnostics, and execution traces convert back.
Normalized semantic tokens map each authored source slice separately. Formatted literal text and braces receive string annotations, while embedded expressions keep their own tokens.
Source files share line indexing; the semantic token encoder is shared by the
language server and tagged-token inspection tools.

Document symbols combine the authored ranges of their opening line, own code,
and descendant sections. Anonymous execution and branch sections contribute to
the enclosing symbol's extent; named nested definitions also remain child symbols.
