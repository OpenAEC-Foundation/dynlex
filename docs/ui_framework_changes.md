# UI framework extraction: fixes found during implementation

The compiler retains general section, callback, memory and graphics facilities.
UI descriptions, layout, interaction and rendering now live in the separate
`dynlex-ui` repository. No window, ribbon or browser-canvas syntax was added to
the compiler.

## Compiler fixes

| Symptom | Root cause and change | Regression coverage |
| --- | --- | --- |
| Recursive functions failed when a recursive result passed through a local, comparison or condition. | Pending dependencies were observed only on the immediate expression. Local reads, stores and call operands now preserve their owning inference pass's observation. Fixed Boolean results and section outcomes remain available while recursive operands await reinference. Unrelated errors still fail. | `recursive_local_alias`, `recursive_tree_local_alias`, `recursive_local_comparison`, `recursive_boolean_condition`, `recursive_local_alias_error_isolation` |
| A static callback table failed when its function pointer passed through an ordinary conversion pattern. | Compile-time values had no symbolic function identity. They now retain the definition, pattern path and inferred instantiation through pure calls and address casts. Codegen emits the callable symbol directly into the constant aggregate. | `static_callback_cast`; `tests/web/host_callbacks.mjs` |
| A function referencing a multiword global data declaration could not resolve. | Function-body matching had to finish before global calls, while declaration names were discovered only by resolving those calls. Once section signatures are known, the ordinary matcher indexes their declaring captures without executing or resolving the global initializer. | `data_declaration_function_globals` |
| Static nested data callbacks using managed records reached codegen without inferred lifecycle bodies. | Lifecycle reachability did not follow resolved data-field values and symbolic function constants. Both now participate in the normal graph traversal. | `nested_data_callback_lifecycle` |
| Reading an unassigned ordinary function local crashed during codegen. | Local validation ran for deferred callbacks and main, but not ordinary function instantiations. The same validation now runs after each converged ordinary function inference, reporting a source diagnostic. | `uninitialized_function_local` |
| A callback following typed ownership links reported that an existing member did not exist. | Recursive layout resolution treated an active symbolic inline field as an unspecified generic class. Its transaction now retains the symbolic graph until completion and diagnoses infinite inline storage. | `recursive_inline_pointer_layout`, `recursive_inline_array_layout`, `recursive_inline_storage_invalid`, `recursive_array_storage_invalid` |
| Intrinsic member types failed on nested commas, crashed without a named outer pattern, or retained unresolved nested types. | Listing separators ignored brackets and strings; member metadata and expansion assumed a named root. Lists now preserve nested expressions, type metadata follows their pattern references, and pending arguments expand under intrinsic roots. | `member_nested_type_expressions`, `recursive_inline_array_layout`; `listing_item_failure_propagation` retains a malformed first item so later items remain unprocessed. |

The required fixtures above import no application libraries. The host callback test
checks both `call pointer` and typed `invoke`, including invocation after `main`
returns, at `-O0` and `-O2`. `tests/web/ui_framework.mjs` checks the rebuilt browser
compiler, including the extracted UI and current UTF-8 library. WebAssembly linking exports the function table; SPIR-V
still reports unsupported pointer calls explicitly.

## Library and browser runtime fixes

- Native font measurement and drawing walked bytes instead of Unicode scalars.
  They now share strict UTF-8 decoding. Encoding, invalid sequences, forward/backward
  boundaries and out-of-range byte offsets have integration coverage.
- LLVM-generated browser math imports were missing for trigonometry and other
  floating-point intrinsics. Runtime implementations and import validation now
  share one definition table, covering float32 and float64 imports.
- Browser `fmin`/`fmax` propagated a single NaN through JavaScript's `Math.min` and
  `Math.max`, unlike LLVM's `minnum`/`maxnum`. They now return the numeric operand.
  C rounding also preserves ties away from zero and signed zero.
- A graphics test launched Xvfb while retaining Wayland connection variables.
  Its environment now explicitly selects the private X11 display.

## Shared UI correctness checks

The new framework preserves press/release pairs queued before a paint and consumes
each key once. Focus updates between queued events instead of waiting for drawing;
pointer motion preserves keyboard focus. A dedicated burst test covers repeated
ribbon navigation and queued Tab/Enter.

Ordinary flow layout applies its margins at construction and stretches automatic
children during resize. Ribbon groups retain their natural sizes and scroll in a
single clipped strip. Text fields clamp their scroll and selection when their
contents shrink. Browser text masks are clipped before rasterization, including
long lines. Native and browser text selection/editing use the same DynLex code. Moving a clip
reuses fully visible browser text masks; changed crops get distinct masks.

Fitted viewports initialize before input mapping. Zero-sized surfaces retain pending
damage, stop scheduling frames, and repaint when restored. Static views stop rendering. Hover, underline, switch and scrolling transitions
settle; Snake requests frames only while playing or showing its finite impact.
Native scheduling retries an unavailable swapchain frame when its surface is
still drawable, and waits for events while static or minimized.

Control, table, ribbon and popup captions read instance values while descriptor
names remain stable. Package imports are file-relative and external-client checks
compile both adapters from a separate project and working directory.

Platform checks use private displays and browser profiles. They exercise real
entry-point lifetime, populated dropdowns, nine ribbon tabs, scrolling endpoints,
independent resizing, SVG scaling, Unicode editing, clipboard actions and idle
paint counts. Test instrumentation is outside the application renderer.

## General browser graphics support

`web/graphics-backend.js` now supplies device selection and WebGL shader-program
creation to the homepage, IDE and UI SDK. The UI build packages that same module.
WebGPU is preferred when device acquisition succeeds; an absent API, null adapter
or rejected device request selects WebGL2 before a canvas is bound. Initialization
and rendering errors after selection remain errors rather than changing backends.

The UI transports share vertex batching, clipping and the DynLex renderer. WebGL2
uses an sRGB attachment for linear-light blending and sRGB presentation. Browser
checks cover the ribbon, Snake and editor with WebGPU, no adapter, and no WebGPU
API, including pixel checks for blending, odd-width masks and fractional scaling.

Extracting shader-program creation also fixed a resource leak: if fragment
compilation failed, the previously compiled vertex shader was never released.
The helper releases shaders and failed programs and keeps detailed errors in the
log. The stale homepage manifest compiler digest was regenerated against the
already rebuilt compiler. No additional compiler changes were needed.

The isolated browser test setup now explicitly uses SwiftShader for both graphics
APIs. Its previous Vulkan/lavapipe configuration acquired a WebGPU device but
failed to create a WebGL2 context even in a minimal page, blocking the homepage's
farm renderer. A browser check covers both APIs together before application tests.
The web farm test also now reads its moved integration fixture from the current
location instead of the removed required-suite path.
The IDE navigation check had stale default-sketch text and line numbers. A focused
browser probe confirmed that the invocation is on line 6 and F12 reaches its
definition on line 3; the test now uses those current source positions.
The mobile homepage's stacked grid also retained code-card intrinsic minimum
widths, pushing the language card's Run button past the viewport. Its shared
single-column rule now uses a zero-minimum flexible track; the existing overflow
assertion remains in place.
Finally, the editor test helper no longer starts an unawaited module import in
every future document. That leaked instrumentation into unrelated pages and
produced uncaught import failures during navigation. Access is now awaited and
document-scoped; tests that reload explicitly reinstall it in the new document.

## Website and shared editor fixes

Studio and River now share the same source-pane controls, completion menu,
outline and inferred-instance chooser. The choice popover is a reusable DynLex
UI component with shared placement, clipping, disabled rows, scrolling and input
capture. F9 visits nested authored sections; F8 selects an inferred instance at
the caret. Inspection works in read-only imports and uses the same Unicode and
hidden-prefix mapping as other editor features. Semantic colors and hover facts
refresh only after an instance selection is acknowledged; edited documents
reject obsolete replies. No application-specific editor event handlers are
needed for these tools.
The shared interaction lifecycle also cancels queued automatic completion when
an explicit tool opens. Choice padding and disabled rows now use the arrow
cursor instead of retaining the source editor's text cursor.
Website filenames now come from the shared project model. Source links and
shader loads configure that model directly, preserving provided project files.
The duplicate filename paragraphs and per-frame metadata polling were removed.
Source navigation also restored a captured initial read-only flag, discarding
later policy updates. Controls now retain application policy while their bound
documents supply content protection; the shared editing gate combines both.
Regression coverage includes locking, unlocking, edits after returning, and
policy changes made while viewing an import.
Quick fixes now use shared diagnostic selection, Unicode ranges, action menus,
command handling and atomic source-text transactions. Staging an edit does not
apply part of it when another target is protected, read only, unavailable or
stale. Parked sources retain independent undo and caret state. Application
callbacks run after all affected models and buffers are updated.
Fixes requested during analysis initially captured an empty diagnostic list.
Source queries now support a parameter-preparation callback at dispatch, after
analysis settles. Fixes use it to refresh their diagnostic context, and reusing a
feature slot clears the previous callback.
Semantic presentation previously ignored token modifiers and their legend. The
shared editor now retains both, matches generic type/modifier style rules, and
includes modifier legends in its color cache identity. Default nested-call
colors are generated from the existing canonical palette for both ordinary and
embedded editors; no application-specific color decoder is needed.
Disabled document controls computed muted colors in a local appearance, but
paragraph and editor drawing read the original colors. Shared foreground
presentation now covers document captions, inline links, semantic text, desktop
fields, switches, buttons and ribbon captions. Authored appearances and semantic
colors stay intact when controls change state.
Disabling a hovered inline link also removes its underline before another pointer
event; the recording-driver regression checks the emitted geometry and its
restoration alongside text colors.
Responsive documents now lay out and paint ordinary single-line fields, switches
and choice fields, using the same editing, undo, composition, activation and
focus code as classic windows. Editing no longer replaces a document field's
declared font and padding. Classic fields also refresh their retained line
geometry before drawing, and all single-line horizontal scrolling uses the same
text-scroll state. Initial and programmatic field values use the existing
single-line input policy.
Choice fields now keep their stable names separate from their initial selected
text. Ribbon declarations supply that initial value explicitly as well.
Dropdowns previously required every option to fit in the window and maintained
their own keyboard and drawing paths. They now use the same bounded choice
surface as editor completion, outlines and diagnostic actions. Placement,
scrolling, disabled selection and keyboard input are shared. Dropdown pointer
acceptance remains on release, including cancellation when dragging away;
completion retains acceptance on press. Document popups inherit the control's
font and colors. The old popup-index state and its readers and writers were
removed. Shared regressions cover long lists, small viewports, capture loss and
authored presentation alongside editor and ribbon behavior.
Choice placement also replaces its fixed 24-pixel line assumption with actual
anchor bounds. Tall controls and transformed source carets use the same
above/below placement without overlapping an anchor when there is room beside it.

The website migration also exercised the following compiler paths. These are
language and compiler fixes, with no website-specific syntax or inference rules.

River's hover checks exposed two further inspection defects. Owner lookup could
choose an unrelated called function merely because it could see the caller's
variable. Candidate owners now require a binding in their own section. Value
lookup also searched for the first expression sharing a variable's identity,
which reused earlier values after assignment or runtime input. Hover and semantic
constant markers now inspect the actual source occurrence and its inferred body.
Import-free ownership, reassignment, unknown-input and local-instance fixtures
exercise the same protocol through the native and browser compilers.

| Symptom | Root cause and change | Regression coverage |
| --- | --- | --- |
| Static descriptions could not read nested aggregate values. | Immutable aggregate loads now fold through initialized globals; static-reference globals are published without fabricated zero initializers. | `static_aggregate_reads` |
| Long associative expressions exhausted time or stack. | Grouping enumeration now prunes spans that have no eligible root. | `associative_chain`, `grouping_bounded_stack`, `grouping_fixed_root_stack` |
| Replayed arguments lost assignments or used state from another invocation. | Replay retains complete input/output transitions and checks binding frames, assignment roles, execution state and result fingerprints. | `argument_reference_inference_replay`, `cached_callee_grouping_replay` |
| Declared names were parsed as calls, or absorbed trailing literal words. | Declaring captures honor literal continuations and named-operand priority. Header snapshots refresh the declaration index to a fixed point and remove stale provisional names. | `data_declaration_operand_priority`, `data_declaration_suffix`, `scripts/test_data_declaration_cache.py` |
| A named operand at an expression root lost to a colliding literal-prefix call. | Explicit named captures take operand priority while enclosing literal continuations retain their precedence. | `explicit_named_operator_operand` |
| Unsupported numeric widths reached codegen invariants. | Builtin type construction now rejects unsupported storage widths with source diagnostics. | Required integer, unsigned, numeric-constraint, Boolean and floating-point width fixtures |
| An early return leaked managed subjects introduced later in a loop. | Return cleanup is emitted after the complete owning-variable registry is known, preserving initialization flags and retained return values. | `managed_subject_early_return` |
| A guaranteed first loop iteration used the return branches of a later pass. | Runtime body annotations join the first and later entries independently of the precise post-loop state. | `guaranteed_loop_early_return` |
| Managed cleanup produced invalid debug locations. | Shared return-cleanup emission preserves valid debug metadata. | `debug_managed_cleanup` |
| Nested inference trials lost constants retained by their enclosing trial. | Lookup follows the complete trial chain; each trial keeps ownership of its writes and unknown-value shadows. | `nested_trial_constant_scope` |
| Cached recursive type identities expanded shared graphs into trees. | Encoding visits each reachable class layout once and fingerprints the graph. | `check_shared_class_type_graph` in `scripts/test_incremental_program_cache.py` |
| Edited builds repeatedly specialized equivalent recursive layouts. | Refinement retains the established type when it already contains the incoming structure. | `check_equivalent_recursive_layouts` in `scripts/test_incremental_class_layouts.py` |
| Reordered calls exchanged cached bodies for different class layouts, or lost their cached LLVM function. | Symbols use stable declaration identities and full concrete type identities, including nested arrays and callback signatures. Codegen shares the cache's graph encoder. | `check_class_symbol_identity` in `scripts/test_incremental_class_layouts.py`, `unbound_class_symbol` |
| Hover reported unresolved types inside instantiated functions. | Hover reads the selected inferred body for expression and variable types; type and constant lookup share instance selection. | `scripts/test_lsp_hover_instances.py`, Web compiler smoke, actual shared-client connection |
| Hovering a parameter declaration returned the whole function signature. | Declaration lookup uses the scope containing the recorded reference, independently of the header line's outer syntax section. | `scripts/test_lsp_hover_instances.py`, Web compiler smoke |
| Hover markup corrupted values containing backticks, quotes or control characters. | Code spans now choose safe delimiters. Parsing and all three compiler string-display paths share one escape alphabet and canonical quoted-string formatter. | Import-free `tests/lsp/hover_markup`, native LSP suite and Web smoke |

The shared editor additionally fixes UTF-16 workspace-edit planning, protected
prefix ranges, completion edits and continuation commands, Unicode insertion and
IME completion triggers, clipboard event normalization, and editor-themed popup
geometry. Studio, River and ordinary embedded source controls use the same
workspace, document, language client and interaction code.

Two borrowed-list traversals incorrectly reassigned their by-reference arguments:
palette comparison cleared the retained palette, causing repeated syntax-color
application and idle redraws; query cancellation discarded the owned feature
slots after an edit. Both now walk a local cursor. Regressions check retained list
identity, repeated unchanged presentation, slot reuse after edits and context
changes, and zero retained allocations through the actual Wasm runtime. These
checks run at both optimization levels alongside the native editor fixtures.

Symbol identity distinguishes type descriptors from runtime layouts: an unbound
class used as a type argument does not need an allocated instance. Exposed
C-callable wrappers retain their public names; the object-output test links and
runs its existing C caller at O0, O2 and O3.

LLVM can replace a constant `printf` with `puts`. The shared Wasm host now
recognizes and implements that import, preserving UTF-8 text and its trailing
newline. Host tests cover empty, ASCII and Unicode strings; the unbound-class
regression also executes the optimized output through the Wasm host.

Native helper test builds honor `CMAKE_BUILD_PARALLEL_LEVEL`, with one job by
default, instead of forcing two jobs over the caller's limit.

The shared language client distinguishes server requests from editor responses
before matching IDs. A server request can legally use the same ID as an editor
request; the preceding implementation treated it as the answer and aborted while
reading its missing result. Server requests now receive replies with their exact
IDs, including strings and integers beyond JavaScript's exact-number range.
Queued replies preserve pending editor work and its ownership. Semantic refreshes
request tokens without resending source, and changed token results invalidate
colors independently of source revisions. Identical token data retains cached
colors. Native and Wasm regressions cover the two protocol directions, suspended
work, repeated refreshes and resource cleanup.

A server refresh emitted after `didClose` initially invalidated the closed
document too. The workspace then reopened that project repeatedly, producing
background compiler traffic and paints while the UI revision stayed unchanged.
The client tracks the open-document lifecycle and acknowledges closing refreshes
without invalidating closed results. `source_close_refresh` reproduces the cycle
without a renderer; the River browser check also requires idle scenes to stop
painting.
The same lifecycle rule also preserves cached diagnostics: the server clearing
its report on close does not erase the editor's retained analysis results.

Source projects now own their main file, imported models and configuration. The
language connection retains one open model and version per file, synchronizes
main-file changes before imported queries, and closes all models before changing
projects. Queries and edits carry project identity and revision, so identical
URIs in separate examples cannot share results.

Diagnostics previously accepted only the active URI. Project snapshots now retain
reports for loaded and unopened sources; the main token response commits the
complete report batch, including empty reports for clean imports. Diagnostic
presentation has its own generation, so changing or clearing a report updates
unchanged source while identical reports preserve cached presentation.

Reconfiguring provided files previously left their loaded models stale. Replaced
sources update their snapshots, removed sources become unavailable, and restoring
a file revives its model. External library snapshots remain independent of that
provided file map. Read-only controls pull their document content instead of
writing preceding control text over a refreshed import. The project still
synchronizes its main file when the selected imported source is unavailable.

Source controls retain independent per-file edit states, including undo/redo,
selection, scrolling, semantic spans and execution marks. Definition visits no
longer replace the main program used by Run and Save. Website actions use these
shared project APIs, and River execution marks target the main document even
while an imported definition is displayed. Source panes are static compositions
of ordinary controls with scoped contextual callbacks, rather than a second
set of hit-testing and button behavior.

Fresh program loading now clears edit history and retires the matching IME
session even when the new bytes equal the preceding source. The prior
text-equality shortcut retained another program's redo entries and accepted its
late input events. Background loads preserve unrelated editor focus and
composition. Borrowed input is retained before clearing active or parked state.

A source-hover regression also exposed an invisible editor animation: document
editors approached a hover value that their renderer never used. Only buttons
and links now animate that tint, so an unchanged editor tooltip can become idle.

Selectable popovers now publish ordinary floating-layout geometry to the shared
hit tester. Their surface blocks controls behind it, including padding, and
dismissing it retires that geometry between paints. Completion menus use that
same path and keep their captions out of document selection. Hovering a text
popover preserves the source caret and keyboard input; clicking its text scopes
selection and immediately retires the preceding input context. Escape returns
source tools to their retained editor location. Tiny viewports hide popovers
until their padding and content can fit.

Opening an import enabled Back and Main file before the source control. Focus
reconciliation retained only a numeric position, so those new controls took the
editor's keyboard focus and prevented Alt+Left. The shared focus collector now
preserves the focused layout's identity when its ordinal was not explicitly
changed. Disabled or hidden targets still leave the focus list, and explicit
Tab navigation retains its normal ordering. Source-pane and queued-focus tests
cover the change after painting, rather than calling navigation actions directly.

Literal Unicode matching now lives in `lib/text_search.dl`, independent of any
editor or platform. It returns original UTF-8 byte ranges and uses linear KMP
matching, Unicode 17 simple case folding and explicit identifier-word boundaries.
Pinned upstream data and its license are stored in the repository; generation
works offline and the generated tables remain immutable static arrays. Tests
cover every code point's property checksum and compare 7,024 search cases with
an independent oracle across native, WebAssembly and the shipped compiler.
This is the shared matching engine; the Find/Replace controls remain unfinished.
The full integration suite passes all 503 checks with the new library modules.

Text background marks now belong to named layers with independent source
snapshots and paint priorities. Refreshing or clearing one feature preserves the
other layers. The painter caches snapshot checks per layer and frame, then walks
ordered marks across rows, resetting its cursor for repeated or backward paints.
Borrowed names and source strings remain valid when replacing their own layer.
All declarations, fixtures and application consumers migrate to the new API;
the old single-layer state and aliases are removed. River uses an execution
layer and its controller tests keep an independent search layer while resetting
execution in a parked main buffer.

Plain `text area` and `read only text area` declarations reuse the shared text
input, selection, undo, wrapping, scrolling, annotation and appearance paths.
They provide multiline input without registering a language source document.
The shared multiline classifier covers both these controls and source editors;
workspace discovery still registers only source editors. This allows search
queries and program text to use the same editing behavior with distinct roles.

A final field regression exposed a rendering split: classic single-line fields
used the shared row painter during composition but bypassed it for normal text.
Their normal path now uses the same painter, so named marks render in classic
fields as well as responsive fields and editors. Classic field appearances also
carry their actual surface color into the shared disabled-ink calculation.

Verification for these shared controls includes 17 fixture identities at both
optimization levels on native and WebAssembly, with no retained Wasm allocations;
the extended classic-field painter regression and related input cases pass on
both targets too. Actual native field/area input and marked-source rendering,
WebGPU/WebGL2 text/editor input, all website controllers, SDK artifact checks and
four full Studio/River browser runs pass. This phase changes DynLex library/UI
code and embeds the new standard modules in the browser compiler; it adds no
C++ compiler fix.

Literal matching now accepts an explicit line-break equivalence policy. It can
match CRLF, LF and lone CR while retaining the original UTF-8 endpoints, without
normalizing a second source string. All callers use the new signature. The
independent corpus now covers 11,024 cases at both native/Wasm optimization
levels and through the rebuilt browser compiler, with no retained allocations;
the full integration suite again passes 503 checks. A library-notice manifest
also makes the compiler-supplied notices available to application packagers.

Shared Find/Replace exposed two library/compiler issues. The workspace-edit index helpers used the callable class name `sequence` for a local parameter; those locals now use `indexes`, with a fixture importing both libraries. Address-provenance inference treated `static reference` like an ordinary operation on its initializer. It could diagnose a static record with null fields as a null pointer, miss writes to immutable scalar storage, and repeatedly traverse nested static descriptions. The intrinsic now identifies static storage directly; import-free fixtures cover null fields, nested copies, immutable writes and mutable pointees behind a static pointer.

Edited native builds could mix two equivalent recursive layout graphs: constraint restoration saved the layouts without their request-to-layout map. An unchanged function then reused the old graph while re-inference built a new one, making arrays of the same declared class fail element matching. The import-free reproducer allocates two mutually recursive records, edits one function, and compares the edited cached build with a clean build. Class snapshots now include portable request bindings and construction argument types; request keys are rebuilt against current declaration identities. Both fixed and parameterized recursive cases require actual constraint and function-summary reuse in the regression.

Studio and River now use one retained Find/Replace controller, including Unicode and line-break matching, per-pane query fields, independent match annotations, read-only policy and atomic workspace-edit undo. Showing a field refreshes focus before reading its ordinal; forgetting a field retires its native input context. These changes fix focus lost to newly inserted controls and typing contexts retained after the owning source pane closes. Run shortcuts activate each editor's existing Run control through the same scoped command dispatch and enabled policy as pointer input. Per-event handled receipts let the browser suppress defaults only when the shared dispatcher handles the key.

Native text, annotations and Snake interaction/rendering checks pass; WebGPU and WebGL2 command, editor and form checks pass. Four full website browser runs pass, including Find/Replace and keyboard Run in Studio and River, execution, language tools, progress, audio, mobile layout and downloads. The integration run passed 502 checks; its remaining shipped-library check passes after rebuilding the browser compiler with the renamed workspace helpers. Fresh shipped core/search fixtures and the independent 11,024-case search oracle also pass without retained allocations.

Shared scene camera and triangle-picking helpers now reuse the core vector and column-major graphics libraries. Tests cover perspective clipping, viewport resizing, projection/ray agreement, mixed numeric precision, ground intersections, triangle winding, edges and transformed geometry. Native, WebAssembly and rebuilt-browser-compiler executions pass. These helpers prepare Farm's scene integration; Farm's renderer and interface are still unfinished.

Typed shader inputs now declare a numeric type explicitly. Float32, signed int32
and unsigned int32 scalars/vectors preserve their GPU interface types; float32
matrices use row vectors with sixteen-byte strides. The SPIR-V patcher previously
retargeted uniform globals without retargeting optimized element pointers. A
minimal, import-free 2x2 matrix/vector multiplication reproduced a Workgroup
pointer into a Uniform block at O2. Derived access-chain pointers now inherit the
base pointer's storage class. All 21 supported input shapes validate at O0/O2,
and the 350 required compiler checks pass.

The shared UI shader driver now transfers packed numeric bytes. Owned DynLex
values provide scalar/vector/matrix construction, integer range and whole-number
checks, float32 finiteness checks, zero padding and column-major graphics-matrix
conversion. This removes per-frame JSON values and host-side numeric packing.
Cross-stage linkage compares complete input types. The website's existing shader
previews use the new uploads and reject inputs their preview controls cannot
supply during linkage. Native and WebAssembly value, driver and linkage fixtures
pass at both optimization levels; Wasm allocations return to zero. Driver-width
and numeric-range tests pass as well. Browser reflection, WGSL/GLSL translation and actual GPU pixel checks now pass
for all 21 input shapes on WebGPU and WebGL2.

GPU translation checks also exposed two portability issues. LLVM's byte-address
optimization introduced 64-bit constant indices for small matrix rows. Shader
address lowering now reconstructs typed accesses and narrows only constants
whose signed value is preserved. Native optimized matrix modules no longer need
ShaderInt64 and translate through Naga. A subsequent background WebGPU pixel
probe proved that WGSL emission dropped SPIR-V array strides: a matrix read the
padding slot instead of the next row. Uniform matrices now use explicit padded
row structures; reflection derives their logical dimensions from the validated
physical rows and offsets. The native compiler validates all 21 shapes at both
optimization levels with that layout. The rebuilt browser compiler and complete
GPU/GL corpus pass with this layout.

The complete GPU corpus caught signed arithmetic that looked valid to SPIR-V
validation but changed meaning during translation. LLVM's signless values were
declared unsigned, so Naga converted a negative int32 to a positive float and
interpreted signed division and shifts through unsigned types. Shader lowering
now makes each opcode's signed/unsigned interpretation explicit in its operand
and result types, preserving the original SSA bits at its boundaries. Fourteen
independent signed/unsigned arithmetic fixtures pass native SPIR-V validation at
O0/O2 and actual background WebGPU pixel checks after translation. The rebuilt browser compiler passes the combined 35-case matrix/arithmetic
corpus on both WebGPU and WebGL2.

WebGL2 additionally exposed GLSL's different signed-remainder behavior for
negative operands. Signed remainder now lowers to exact truncating integer
division, multiplication and subtraction. All fourteen arithmetic cases pass
actual WebGL2 pixels too. Final verification includes 350 required checks,
native shader validation at O0/O2, browser reflection/translation, and all 35
compiled shaders rendered through both backends alongside the shared form and
buffer-submission regressions. The website SDK is repinned to the verified compiler/UI revisions. Website
shader models, buffer updates and all four Studio/River browser runs pass on
WebGPU and WebGL2. The local preview is updated; the complete integration suite
is running separately.
