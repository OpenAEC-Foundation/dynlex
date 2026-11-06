# Ribbon Studio

A native PowerPoint-style ribbon for trying the DynLex UI authoring model.
All nine tabs switch with the mouse. Tab focuses a control; Left and Right
switch focused ribbon tabs, including wrapping at either end. Enter activates
the focused control. Dropdowns support Up, Down and Enter. Escape dismisses an
open dropdown; pressing it again closes the window. Clicking outside a dropdown
dismisses it and captures that click.

Keyboard commands consume queued press events in order. Repeated taps remain
distinct when they arrive between frames; holding a key does not retrigger a command.

Commands show hover, press and focus feedback without modifying the presentation.
Menus contain selectable options; font fields retain their selected value across
tab switches. The toolbar, search, Present, Share, profile and view buttons are
declarative controls. The slide and thumbnails are a canvas illustration.

Run from this checkout:

```sh
./build/dynlex examples/ribbon/main.dl -O2 -o build/ribbon-studio
./build/ribbon-studio
```

The executable stays in `build` rather than the temporary directory. The bundled
Noto fonts are resolved relative to the checkout, so keep it as the working directory.

`interface.dl` is the form. Groups size themselves from their children; adding a
command or rearranging a column requires no coordinates or event wiring:

```text
ribbon "Main ribbon" at 12 56 sized 1256 by 168:
    tab "Home":
        ribbon group "Clipboard":
            large menu "Paste" with icon "paste":
                menu item "Paste"
                menu item "Keep source formatting"
                menu item "Paste as picture"
            tool column:
                tool "Cut" with icon "cut"
                tool "Copy" with icon "copy"
    tab "Insert":
        ribbon group "Images":
            large menu "Pictures" with icon "picture":
                menu item "This device"
                menu item "Stock images"
```

`tool row:` arranges controls horizontally. `tool column:` stacks them vertically.
`icon tool "Bold" with icon "bold"` makes a compact icon command.
Fields declare their options as data:

```text
tool field "20" sized 48:
    menu item "16"
    menu item "20"
    menu item "24"
```

`tool menu` makes a compact menu; `large menu` makes a tall one. Plain tools
show no expansion arrow. All of these are ordinary data sections from
`lib/ui_ribbon.dl`; the compiler contains no ribbon syntax.
`theme preview "Office" in "orange"` adds a colored slide card to a gallery.

Tab selection is stored per ribbon, per UI instance. It does not use the window's
page selection. A command may be left unbound, or given a `when the "..." control
changes:` handler in the form's `code:` section. The selected tab is available as
`the selected tab of ribbon in instance`. The demo selects Home initially with:

```text
set the value of "Main ribbon" in this window to 1
```

Native rendering and layout live in `lib/ui_native_ribbon.dl` and
`lib/ui_native_ribbon_layout.dl`. Activation and popup capture live in
`lib/ui_native_interaction.dl`, which can be used without importing SVG drawing
or the icon catalog. Vector icons are static data in
`lib/ui_icons.dl`, generated from the Lucide SVG files in `lib/ui/icons/` by
`python3 scripts/generate_ui_icons.py`. Curves are preserved and FreeType supplies
antialiased coverage at the displayed size. Textures are cached per view and
replaced when pixel density changes. Controls keep their size when the window is
resized. The ribbon keeps a single row of groups. When a tab overflows, previous
and next arrows scroll between groups with an animated transition. Each tab
remembers its position; widening the window clamps it to the available content.
An arrow disappears when the strip reaches its end and keyboard navigation skips
it. The viewport keeps its width so hiding an arrow does not shift the groups.
Drawing and pointer hits share a clipping region, while keyboard focus reveals
offscreen controls. The ribbon height stays fixed. The slide
fits the editor's available space while preserving its own proportions.

Resize behavior is declared in the form's `style:` section:

```text
the interface adapts from 1280 by 800 pixels
the window is at least 960 by 600 pixels
pin "Editor" to all edges
pin "Main ribbon" to the left and right
pin "Share" to the top right
pin "Normal view" to the bottom right
```

Import `lib/ui_resizing.dl` to use these data declarations. Pins preserve the
original edge margins. Unpinned elements keep their position
relative to their parent. `center "..." horizontally` preserves a centered
offset. `lib/ui_resize.dl` applies these rules to retained geometry, preserving
control identity, selection and keyboard focus. A separate `the interface fits
...` rule remains available for fixed-coordinate scenes such as games; it
letterboxes the scene without constraining the native window's aspect ratio.

The editor illustration is separate in `workspace.dl` and the slide artwork in
`slide.dl`; `main.dl` binds the workspace to the form's canvas. The form
imports only the UI description library and can be inspected without a renderer.

The native check uses a private X display and exercises every tab, unbound commands,
34 hover targets, populated menus, selection persistence, keyboard navigation,
short repeated key taps, independent width/height changes, left/top edge geometry,
a burst queued while rendering is paused, clipped group scrolling, disappearing
endpoint arrows, native size hints, and a stable document area:

```sh
python3 -B scripts/test_ribbon_ui.py . ./build/dynlex --snapshots /tmp/ribbon-snapshots
```
