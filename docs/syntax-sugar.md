The source migrations save **165,233 characters** across **792 `.dl` files**. Counts include spaces, tabs and LF newlines. The initial migration saved 54,675 characters; the first follow-up saved 3,653; the complete repository pass saves another 106,905. Implementation changes and new source additions between those snapshots are excluded. Documentation and embedded source snippets were also migrated and are excluded from this program-source total.

The [per-file audit](syntax-sugar-savings.json) records before/after counts and hashes for each migration snapshot. Those historical hashes describe the measured rewrites. The separate HEAD comparison includes the current implementation and every new `.dl` file.

| Migration | Characters saved |
|---|---:|
| Boolean negation | 2,328 |
| Line printing | 17,332 |
| Boolean printing | 48 |
| Modulo | 3,744 |
| Grouped assignment | 1,608 |
| Explicit line wording | -20 |
| Element iteration | 20,018 |
| Grouped append | 6,989 |
| Membership and comparisons | 2,117 |
| String interpolation | 295 |
| Scope-preserving rewrites and obsolete definitions | 216 |
| Grouped addition | 111 |
| Printing several lines | 3,542 |
| Declaration shorthands | 100,528 |
| Single-pattern declaration blocks | 132 |
| Inline action declarations | 375 |
| Nested element iteration | 4,581 |
| Extended range comparisons | 243 |
| Grouped pure-value assignment | 1,042 |
| Generated source formatting | 4 |

Compared with repository HEAD, existing `.dl` files are **690,276 characters smaller**, including implementation changes and code moved into new modules. The 79 new `.dl` files add 576,169 characters of library definitions, moved CAD implementations and regression cases. Including those additions, the entire `.dl` tree is **114,107 characters smaller**. The CAD split adds 2,028 characters of imports, module documentation and explicit shared pattern names. This net repository reduction is separate from the source-rewrite savings above.

The following examples give exact savings for each form. `⏎` denotes one LF newline; the spaces around that marker are for display. A final newline is excluded from the examples. Savings depend on identifier and expression lengths.

| Form | Before | After | Saved |
|---|---|---|---:|
| Grouped assignment | `set x to value ⏎ set y to value ⏎ set z to value` (44) | `set x, y and z to value` (23) | 21 |
| Grouped append | `append x to values ⏎ append y to values ⏎ append z to values` (56) | `append x, y and z to values` (27) | 29 |
| Membership | `mode = x or mode = y or mode = z` (32) | `mode is one of x, y and z` (25) | 7 |
| Exclusive bounds | `value > min and value < max` (27) | `value between min and max` (25) | 2 |
| Lower bound included | `value >= min and value < max` (28) | `value from min until max` (24) | 4 |
| Upper bound included | `value > min and value <= max` (28) | `value after min through max` (27) | 1 |
| Both bounds included | `value >= min and value <= max` (29) | `value from min through max` (26) | 3 |
| Double-sided comparison | `min >= x and x < max` (20) | `min >= x < max` (14) | 6 |
| Negation | `the inverse of ready` (20) | `not ready` (9) | 11 |
| Modulo | `the remainder of x divided by y` (31) | `x mod y` (7) | 24 |
| Modulo symbol | `the remainder of x divided by y` (31) | `x % y` (5) | 26 |
| Line printing | `print score as a line` (21) | `print line score` (16) | 5 |
| Interpolation | `"Score: " + the string form of score` (36) | `formatted "Score: {score}"` (26) | 10 |
| Element iteration | `loop index from 0 to the length of values: ⏎     set value to the item at index in values ⏎     print line value` (108) | `for each value in values: ⏎     print line value` (46) | 62 |

`between` excludes both bounds. The other word forms specify exactly which endpoints are included:

| Form | Meaning |
|---|---|
| `value between min and max` | `min < value < max` |
| `value from min until max` | `min <= value < max` |
| `value after min through max` | `min < value <= max` |
| `value from min through max` | `min <= value <= max` |

`min >= x < max` means `(min >= x) and (x < max)`. Double-sided comparisons evaluate the middle expression once and skip the right expression when the first comparison is false. Boolean `and`, `or` and conditional selection infer all operands for typing; runtime evaluation follows only the selected path.

`print value` writes without adding a newline. `print line value` adds one newline. `print lines x, y and z` prints each value on its own line, in order. `formatted "Score: {score}"` converts inserted values to text. `{{` and `}}` produce literal braces, and expressions inside holes may contain quoted strings.

`x mod y`, `x modulo y` and `x % y` have the same remainder operation. Grouped assignment evaluates its right-hand value once, then assigns that value to each target. Grouped append preserves item order. It evaluates its destination once. Grouped addition also evaluates its destination once, then invokes the ordinary scalar `add` definition for each item, preserving custom overloads.

Argument packs use this literal declaration syntax:

```dl
function set {arguments:variables} to value:
    replacement:
        @intrinsic("store", {assigned}, value)
        for each {variable} in variables:
            set variable to assigned
        set the subject to assigned
```

`{arguments:variables}` accepts any non-empty number of expressions. No ellipsis is written. A pack retains the source expressions and their caller bindings. It can be forwarded between replacement definitions and used by the same `for each` macro as arrays and lists. Length and element selection use the existing extent and extraction intrinsics. Pack iteration adds no dedicated intrinsic. Passing a pack to an ordinary runtime function produces a source diagnostic.

Section macros now support typed overloads. Omitted captures receive local storage and start without a value; replacement code supplies any initialization. Each invocation resets its local inference state. Pack iterations have separate local storage and inferred bodies. Grouped and scalar assignments of a runtime value read from the command-line argument count produce identical LLVM instructions at `-O2`, after normalizing SSA names.

An iterator named in a `for each` header uses the caller’s scope. The migration gives iterators distinct names where moving an earlier body-local declaration into that header would otherwise capture another variable. Array elements are value copies; pack elements refer to their captured expressions.

Existing reductions remain available: `means:` replaces a value-producing function/replacement declaration, `to` and `to get` replace execute declarations, explicit multi-word names can be used unbraced after declaration, and `it` refers to a subject set by the relevant action. Mathematical quaternion inversion and explicit string conversion retain their separate meanings.

The complete pass covers libraries, example games, CAD programs, required fixtures, embedded Python/C++/JavaScript sources, the IDE starter, webpage examples and source generators. Single-pattern declaration blocks are collapsed, short actions use inline bodies up to 120 characters, and 52 additional indexed collection loops use `for each`. Eighteen iterators are renamed using their resolved language-server bindings to preserve scope.

Full declarations remain where metadata or visibility modifiers need them, action replacements return `nothing`, or an action name beginning with `get` would be consumed by the `to get` value shorthand. Fixtures that specifically test explicit declarations, mixed void/value returns, assignment lifecycle effects, or control-flow diagnostics keep the form that expresses their test. Indexed loops remain when their indices, collection mutation, outer assignments or subject effects require them. Historical before/after examples keep both forms.

Captured assignment targets now pass through ordinary binding and replacement resolution before writable-storage checks. The import-free `argument_pack_property_assignment` regression uses forwarded field getters and dereferenced local pointers with a runtime value. It is also compiled and executed by the Web smoke test.

For example, `function value squared:⏎    replacement:⏎        value * value` (62) becomes `value squared means: value * value` (34), saving 28 characters. `function greet:⏎    execute:⏎        print line "Hi"` (52) becomes `to greet: print line "Hi"` (25), saving 27.

The accepted follow-up forms are implemented:

| Form | Before | After | Saved |
|---|---|---|---:|
| Implicit subject addition | `set x to 1. set the subject to it + 5` (37) | `set x to 1. add 5` (17) | 20 |
| Several output lines | `print line x ⏎ print line y ⏎ print line z` (38) | `print lines x, y and z` (22) | 16 |
| Grouped addition | `add x to total ⏎ add y to total ⏎ add z to total` (44) | `add x, y and z to total` (23) | 21 |
| Negative membership | `not (mode is one of x, y and z)` (31) | `mode is not one of x, y and z` (29) | 2 |

`set x to 1. add 5` leaves `x` equal to `1` and makes `it` equal to `6`. Public assignment publishes a value copy; it does not turn the subject into an alias of the assignment target. Internal loop and formatting assignments do not publish their private counters or caches as the subject. Explicit `add 5 to x` still changes `x`.

Each grouping candidate infers its arguments in source order, allowing the left action to establish the subject and refine storage types before the right action uses them. A rejected candidate restores the type equations, variable metadata, constants, pointer facts and subject. Static typing of skipped conditional operands remains separate from their runtime execution.

In the CAD taper case, restricting the graph to unfinished structure reduces tracked storage roots from 1,742 to 30, equation endpoints from 644 to 10, and captured subject sources from 2,415 to 33. Compilation drops from 28.3 seconds to 9.4 seconds.

Pointer-alias refinement now propagates structural type requirements through copied pointers, fields, aggregate elements and subjects. Storage roots distinguish macro invocations, and function parameter/global refinements propagate back to their corresponding caller storage. Implicit assignment conversions wrap the assigned value rather than modifying an expression inside its replacement body; conversion materialization preserves source execution metadata.

Pure evaluation keeps subjects in each function call frame. Pure type functions remain usable after local assignments, managed subject lifecycle effects remain impure, and recursive assignments can defer their subject until their value type is resolved.

Assignment subjects retain managed value copies until their owning function ends. The lifecycle expectations include these copies; the array release-order test uses a 64-bit counter to hold the longer sequence without overflow.

The initial complete verification found two lifecycle reachability crashes in filesystem operations and a changed negative-fixture diagnostic trace. The lifecycle collector now follows captured expressions through the existing binding resolver and their caller scopes. Unused replacement arguments remain outside the runtime graph. Both filesystem crash cases pass after that correction, and all 31 focused argument-pack/lifecycle fixtures pass. The CAD fixture still rejects the same incomplete NURBS surface layout; its expected notes reflect the runtime-driven inference path's extra grouping frame.

The clean complete `./scripts/test.sh --lint=false` run passes **837/837 checks**, with zero failures or skips. The native compiler's executable hash remained unchanged throughout this run. An earlier run had 28 launch permission errors because the executable was rebuilt during testing; the clean rerun verifies all of those fixtures against the finished compiler.

The Web compiler and all deployment assets have been rebuilt after the backend corrections. The full Web smoke test, 12 memory-layout regressions, all three path-library WebAssembly fixtures, runtime filesystem/host checks and the Web UI suite pass. Both games pass execution-stepping tests in background Chrome, and the Vite-served compiler worker compiles and runs successfully. All four compiler artifact copies match; runtime imports, memory-layout readers and compiler workers match across the source, IDE distribution and static webpage. All **82 changed C++ files** pass formatting checks and remain below 1,000 lines; `git diff --check` passes.

The additional full WebGPU browser scenario reaches farm initialization but fails to create its WebGL context (`BindToCurrentSequence failed`), including on a serial retry. The focused WebGL game-stepping scenario passes. Renderer files were not changed; this broader graphics failure remains to investigate separately.

The entry-point checker reads dependencies and selected syntax configurations from the compiler's `--dump-dependencies` JSON. It independently compiles each entry point for native LLVM and the vertex, fragment and compute SPIR-V stages, and reads the emitted shader execution models from `OpEntryPoint` records. A source passes when at least one mode produces its expected output; crashes, timeouts and inconsistent compiler outputs fail the check even if another mode succeeds. `--report` preserves every mode's result and complete diagnostics. The checker has no DynLex source-pattern scanner or shader-library heuristics. Obsolete source stage-directive comments have been removed.

The font-render smoke test's root cause was stale test code. Commit `ba9476d8` replaced the font's `texture` member with a glyph `cache`; textures now belong to cached glyphs. The test now checks cache allocation, verifies that drawing `"A A"` produces two cached glyphs with a loaded texture for `A`, and checks cache/library/face clearing on release. It compiles and passes under Xvfb with Lavapipe. No valid grouping could make the former property access valid; a separate diagnostic improvement would report the missing-property error instead of the first rejected grouping's Boolean-condition error.

C-ABI target support is now intrinsic metadata. Inference rejects `call` and `variadic call` for Vulkan SPIR-V and retains native CPU and WebAssembly support. The existing native-only `call pointer` restriction uses that same preflight path. These invalid operations no longer reach the LLVM heap/variadic failures previously reported.

Generated internal functions now use their runtime arguments' actual pointer address spaces, caching each storage ABI on the inferred instantiation. Local and global calls to the same function preserve shared storage; function-local variable and subject state is restored across generation of another ABI. Native wrappers and managed lifecycle calls use the same generator. Import-free forwarding and subject tests pass in all three shader stages, and native mutation and recursion results match. The 12 checker regressions also pass. These focused checks verify emitted storage signatures, but do not establish full SPIR-V validity.

The two additional causes exposed by expanded validation are now corrected:

- Lifecycle inference follows runtime expressions from main, exposed functions and their selected callees. Type-only operands use a shared intrinsic classification and do not make retain/release bodies reachable. Owning fields, arrays, runtime values and explicit destruction do. A worklist follows any further runtime types needed by those lifecycle bodies. Import-free `size of`, function-result type and pointer-only cases pass for all three shader stages, while runtime, nested-resource and explicit-destruction cases still reject an unsupported C-call destructor.
- The SPIR-V binary patcher builds the entry-point interface from all emitted module-global declarations and excludes function-local variables. The former global-reference reproduction and the local/global forwarding regression now pass `spirv-val --target-env vulkan1.3`. The checker validates every successful shader output and treats invalid output as fatal even when native output succeeds. Reports are saved after each entry point, preserving diagnostics from an interrupted run.

All 13 checker regressions and the shader-target tests pass. Windows dependency installation now provisions `spirv-val` through the existing pinned vcpkg baseline with the `spirv-tools` tools feature; installer and release manifest checks pass on Linux. Windows execution itself has not been verified on this host.

The fresh complete entry-point scan passes **49/49 additional entry points**, with **39 emitted shader outputs** validated by `spirv-val`; **1,122 source files** are reachable and all **5 syntax configurations** are selected. The rebuilt Web compiler passes the new type-only resource/global-reference shader regression, and the refreshed gallery pipeline verifies all three actual shader programs. The current font render smoke test passes under Xvfb with Lavapipe. Native verification is summarized above; the WebAssembly layout defect and its correction are explained below. `endless-terrain.dl` alone contains definitions and compiles an empty program in additional modes; its generated pipeline wrapper is required to validate its actual shader bodies.

The refreshed Web smoke test exposed a pre-existing WebAssembly layout defect. Before the layout fix, the import-free program below returned `1` with both Web compilers: its literal occupied address zero, which DynLex also uses for null:

```dl
to get pointer is null:
    @intrinsic("return", @intrinsic("equal", pointer, @intrinsic("cast", 0, @intrinsic("type", "string"))))
@intrinsic("return", "x" is null)
```

The former backend emitted a relocatable object, exported main by guessing its function index, and stripped linking/relocation sections. Static data could begin at zero, and the runtime supplied a zero `__memory_base`. The ABI correction changed constant creation order, exposing the existing defect: `"saved"` became the first string, had length 5, and failed `contents is valid` before any filesystem runtime call. The previous compiler's filesystem example passed only because other constants preceded it.

The backend now embeds the pinned LLD WebAssembly driver in both compilers. It links the object, reserves addresses 0–15, resolves code/data relocations, and exports main by symbol name. The linked module owns its stack and function table and exports the aligned `__heap_base`; the runtime reads this value instead of computing a second layout or supplying relocation-base/stack/table globals. This also accounts for BSS, which need not have active data bytes. The old binary patcher and the duplicate runtime stack calculation are removed. The linker libraries are built separately against the existing pinned LLVM profiles, including native Windows's static CRT contract.

The new native-to-WebAssembly regressions cover first/shifted/empty literals, literal byte access, local arrays, managed heap strings and global string references at O0 and O2. All 12 cases pass, and all three path-library WebAssembly fixtures pass. The runtime filesystem/host tests and 13 toolchain tests also pass. The rebuilt browser compiler passes its complete smoke test, and all compiler/runtime deployment copies have been refreshed and hash-verified. The single-object linker is configured for one thread, matching single-threaded Web Workers; three consecutive calls through the embedded linker API pass. The clean complete native suite passes all 837 checks against the finished executable.

The previously reported unsigned compile-time cache-key collision is also outside this change pending approval. Its cache encoder omits unsigned integer values, allowing distinct unsigned compile-time arguments to share a key.

The six oversized CAD entry points now import 18 cohesive implementation modules covering topology, NURBS, sweeps, presspull, splitting and mesh operations. The largest module has 968 lines. All 315 original public function patterns are preserved; 49 shared helpers are explicit module interfaces, while 210 helpers remain file-local. The [module table](../lib/bim/README.md) records the layout. Thirteen focused O0 fixtures pass, covering the six original domains plus mass, silhouette, surfaces, wireframe, closed-face splitting, tapered extrusion and NURBS ownership. All 18 modules also compile independently, and their dependency graph is acyclic. Structural checks confirm that each original executable source line moved exactly once.

Three pre-existing per-domain Rust comparison runners reference absent `probe.dl` files, including in HEAD, and also require external pinned Rust reference artifacts. Their source-provenance lists now include the split modules, but full Rust comparisons cannot run as written. The retained native fixture checks pass; no probes or reference results were fabricated.

The existing browser-test cleanup kills its npm server process but leaves the Vite child running. The test server started for this verification was stopped explicitly. Updating that cleanup to terminate its whole process group is another proposed improvement.

The IDE production build reports chunks larger than 500 kB. Splitting those bundles is another potential improvement. Superseded managed subject snapshots currently live until function exit; a future lifetime analysis could identify snapshots whose values are no longer used and reduce retained resources.

The Web smoke helper now checks each program's exit value as well as its output, including the import-free null-pointer regression. A wrong result therefore fails even when no output differs.

Visibility modifiers on short declarations are another possible extension: for example, `local to get total:` could replace a local full declaration. Action replacements also need a short spelling separate from the value contract of `means:`. These are proposals; current declarations keep their required full forms.

The frontend installation's `npm audit` reports four affected dependency packages (one critical, two high, one low). The pinned DOMPurify version is covered by the [DOMPurify advisories](https://github.com/advisories/GHSA-p98j-92pf-mc4p); Vite and source-map-js also have reported advisories. Dependency updates are a separate proposed follow-up; these package versions were not changed by the compiler/layout correction.
