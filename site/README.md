# DynLex website

The interface is declared in DynLex and rendered by the shared dynlex-ui library.
Page content and metadata come from the same records. Browser JavaScript supplies
platform capabilities, compiler workers and the Wasm execution host.
The application receives its renderer and service provider independently. Shared
controllers use `ui/services.dl` for calls, requests and cancellation; `main.dl`
selects the browser adapters.
Program requests target `cpu`; the browser compiler service builds Wasm and
returns an opaque executable artifact. Shader requests retain their explicit
shader stages.
Program descriptions also declare loading and execution budgets. Farm role
preparation allows 30 seconds for worker loading, then five seconds for execution;
Stop cancels either phase. The worker reports its execution boundary, while
DynLex owns the timers, status and completion policy.

Build and serve from the compiler checkout:

```bash
python3 scripts/build_site.py
./scripts/view_web.sh 7000
```

Open `http://localhost:7000/` in a browser. The host prefers WebGPU and also
supports WebGL2. The build uses the checked-in browser compiler and pinned UI
source SDK; it does not open a browser or desktop window.

Page declarations live in `pages/`, with shared appearances in `appearance.dl`.
`interface.dl` composes the pages. Controllers in `programs/`, `editor/`,
`downloads/` and `shaders/` own behavior and retained state. `games/` contains
the shared execution source reader and river event/scene model. The Farm world,
routes, inventory, areas, environment, actions, worker roles and simulation live
in `games/farm/`. Its saved-world codec validates external data before reading
typed state, keeps unapplied drafts and active routines, and omits transient
pointer capture and request generations. Its visual declaration is in
`games/farm/interface.dl`, with shared model-backed fields for roles, workers,
areas, flags and supplies. Its controller manages playback, worker roles, manual
tools, pointer gestures and saved profiles. `rendering.dl` composes the landscape,
characters, equipment, fences, flags, work areas and navigation markers through
the shared scene API. Route segments reuse one mesh and follow the displayed
person. Rain is one cloud mesh whose shader advances the drops and fades their
opacity; weather changes retain the mesh and sunlight shadow data.
Farm source uses the same source workspace and program service as the IDE and
River. `application.dl` connects the interface, controllers and platform events.

To use changes from a dynlex-ui checkout, update the package and lock together:

```bash
python3 scripts/update_site_ui.py --ui /absolute/path/to/dynlex-ui
python3 scripts/build_site.py
```

Shared code links accept `code` or UTF-8 source encoded in `code64` using Base64
URL encoding, plus a `.dl` filename in `name`. `autorun=1` or `autorun=true` runs
the entry once when the source link changes. Repeated location notifications and
returning from another page retain edits and do not repeat autorun. A new entry
stops the previous program. Invalid links report a generic error and log details.

The Studio has separate Program and Shader tabs with retained source and undo
history. Shader edits compile to both WGSL and GLSL, then use the same DynLex
linker and renderer as the homepage. A failed edit keeps the last successful
preview. Stop, pause/resume and reset control its clock; offscreen previews and
reduced-motion pages do not advance it. Unchanged frames reuse their texture.
Shader descriptions use the shared UI pass graph: named assets and output,
reflected numeric/image/sampler bindings, explicit color/depth attachments and
raster state. The DynLex linker checks resource types and attachment compatibility
before allocation. The same browser renderer supports depth-only passes, HDR,
multisample resolves, independent samplers and preserved/blended passes on both APIs.

Use `/ide/?mode=shader` for a new shader, or
`/ide/?mode=shader&scene=endless-terrain` to edit a packaged scene. Scene links
load their actual source and geometry from `shaders/catalog.dl`. Inline shader
links also accept `code`, `code64` and `name` and compile on entry. Adding a
scene to that catalog supplies its source, stages, geometry and duration to the
build, homepage and editor together.

Controller tests run without windows:

```bash
python3 tests/web/site_runtime.py
python3 tests/web/site_runtime.py --case site_entry
python3 tests/web/site_runtime.py --case site_farm_interface
python3 tests/web/site_runtime.py --case site_farm_controller --case site_farm_profiles
python3 tests/web/site_runtime.py --frontend native --case site_farm_markers
node tests/web/project_sources.mjs
python3 tests/web/source_package.py
node tests/web/farm_models.mjs build/dynlex --native-wasm --shipped
node tests/web/library_values.mjs build/dynlex --shipped
node tests/web/site_games.mjs
node tests/web/site_program_protocol.mjs
```

Controller and model checks use both the native compiler and the compiler shipped
to browsers. `--frontend native` or `--frontend shipped` selects one for an
independent preflight check.

`/river-character-sheet.html` declares the 17 production character states in
`pages/river_characters.dl`. The game and inspection page share artwork, eyelid
geometry, expression poses, tongue motion and scuffle transforms. Pause and
Restart control a shared timeline; the range control scrubs every loop to the
same fraction. Overlay bounds use the production painter. Leaving the page
pauses its timeline, and automatic playback respects reduced motion.

```bash
python3 tests/web/site_runtime.py --case site_river_inspection_interface
DYNLEX_SITE_SCENARIO=river-inspection node tests/web/site_browser.mjs build/site webgpu
```

Source editors use `dynlex-ui/ui/source_tools.dl`. Its workspace discovers
source controls, retains one document model per control, synchronizes UI and
programmatic edits, and schedules active editors through one language connection.
`editor/language.dl` supplies website project metadata and transports the shared
client's work to the compiler service. It contains no tokenizer or diagnostic
coordinate conversion. Studio, River and the homepage sketches use this path.
Completion requests, suggestion selection, text edits, continuation commands and
undo use the shared overlay and standard text-edit planner. Website code enables
the tools once and forwards language-service messages.
Delayed hover inspection uses the same selectable text popover in every source
pane, including imported read-only files. Hover preserves the editor's keyboard
focus; selecting the popover text uses ordinary Unicode selection and copy.
Source panes compose the editor with ordinary Back/Main file buttons and a
filename. F12 opens imported definitions read-only, and Alt+Left returns with the
previous selection and scroll. Per-file undo and annotations stay with their
source. Run, Save and River's execution marks use the project's main document,
even while an import is displayed. Loading a new linked program or saved plan
starts fresh editing/navigation state through the same shared library API.
Run and language analysis package the same project files, filename and protected
prefix through one shared source builder.
Control/Command+Enter activates each editor's Run control through shared scoped
key bindings, including both IDE tabs. Find/Replace, its keyboard shortcuts and
atomic undo come from the same source-pane controller in Studio and River.
River's hidden vocabulary import is part of the analysis document; the shared
mapper converts its UTF-16 results back to the visible UTF-8 source. Language
results use separate presentation slots from River execution feedback.

The River challenge uses `games/river/interface.dl` for its retained description,
`controller.dl` for actions and program callbacks, `playback.dl` for its testable
clock, and `rendering.dl` for its responsive world. Run, Stop, one-instruction
steps, pause/resume, replay, speed, reset and coding hints use the shared UI.
Execution marks follow the compiled source snapshot, including UTF-16 trace
columns converted to UTF-8 editor offsets. Character expressions, music, ambience,
effects, idle calls, visibility and mute/pause/speed policy also live in DynLex.
The browser audio adapter owns only decoded buffers and playback primitives.
Its program prefix and tracing options live in `programs/types.dl`; protocol
output is retained for replay instead of being shown as console text.

The background interaction test requires Chrome and Xvfb:

```bash
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET DYNLEX_BACKGROUND_BROWSER=1 XDG_SESSION_TYPE=x11 xvfb-run -a -s '-screen 0 1400x1000x24' node tests/web/site_river_browser.mjs build/site webgpu
```

Migration is in progress. DynLex source defines the homepage, documentation,
downloads, missing page, Studio, River and Farm. Shared source tools provide
editing, language features, execution annotations and themes. Farm's retained
person labels, routes and rain, and River's character inspection page are also
implemented in source. Their final packaged browser verification is pending.
The HTML/Monaco implementation has been retired. Model oracles, generic runtime
adapters and their portable replacement checks remain in the repository.
The deployed website has not been replaced by this worktree.
