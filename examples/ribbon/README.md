# Ribbon Studio

A native PowerPoint-style ribbon for trying the DynLex UI authoring model.
All nine tabs switch with the mouse. Tab focuses a control; Left and Right
switch focused ribbon tabs, including wrapping at either end. Enter activates
the focused control. Escape closes the window.

Commands and fields are visual samples. They show hover, press and focus feedback
without modifying the presentation. The slide, thumbnails and application chrome
are an illustration drawn behind the real ribbon.

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
            large tool "Paste" with icon "paste"
            tool column:
                tool "Cut" with icon "cut"
                tool "Copy" with icon "copy"
    tab "Insert":
        ribbon group "Images":
            large tool "Pictures" with icon "picture"
```

`tool row:` arranges controls horizontally. `tool column:` stacks them vertically.
`icon tool "Bold" with icon "bold"` makes a compact icon command.
`tool field "20" sized 48` makes a compact sample field. All of these are ordinary
data sections from `lib/ui_ribbon.dl`; the compiler contains no ribbon syntax.
`theme preview "Office" in "orange"` adds a colored slide card to a gallery.

Tab selection is stored per ribbon, per UI instance. It does not use the window's
page selection. A command may be left unbound, or given a `when the "..." control
changes:` handler in the form's `code:` section. The selected tab is available as
`the selected tab of ribbon in instance`. The demo selects Home initially with:

```text
set the value of "Main ribbon" in this window to 1
```

Native rendering and layout live in `lib/ui_native_ribbon.dl` and
`lib/ui_native_ribbon_layout.dl`. Vector icons are static data in
`lib/ui_icons.dl`, with drawing in `lib/ui_native_icons.dl`. The editor illustration
is separate in `workspace.dl`; `main.dl` binds it to the form's canvas. The form
imports only the UI description library and can be inspected without a renderer.

The native check uses a private X display and exercises every tab, unbound commands,
keyboard navigation, short repeated key taps, resizing, and a stable document area:

```sh
python3 -B scripts/test_ribbon_ui.py . ./build/dynlex --snapshots /tmp/ribbon-snapshots
```
