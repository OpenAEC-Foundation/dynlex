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
