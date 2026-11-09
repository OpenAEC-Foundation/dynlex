# Snake Studio

From this checkout's root:

```sh
./scripts/build.sh
python3 scripts/play_game.py snake
```

The native interface uses the project's Vulkan, GLFW and FreeType dependencies.
Noto Sans regular and semibold are bundled in `tests/games/fonts`, under
`LICENSE-NotoFonts`. UI fonts are rasterized at twice their logical resolution,
and text and shapes retain fractional positions. Font atlases are prepared before
interaction, and each text run uses one graphics draw. Keep this checkout as the working directory
when launching the game so its font paths resolve.

Use the arrow keys or WASD to steer. Brief taps are retained until the next input read.
Space or P pauses and resumes; Escape pauses,
returns from settings, resumes a paused run, or quits from the main menu. Tab and
Enter navigate the interface, and every control also accepts mouse input.

Settings change the movement pace, edge wrapping, grid, motion and snake color.
The board previews visual settings immediately. Settings and the session best survive
restarts during the current session. Preferences opened from pause preserve that run;
“Apply & play” starts a new one.

- `interface.dl` declares the screens and binds their events.
- `rules.dl` contains the independent, deterministic game simulation.
- `motion.dl` calculates the current pose and impact compression along the body.
- `projection.dl` maps that pose into playfield coordinates without a renderer.
- `painting.dl` draws the board and glossy snake through a canvas callback.
- `application.dl` connects keyboard input, simulation time and UI values.

The menu's opening trail is a static preview. A new run begins with five segments.
Food appears only in free cells; entering a vacating tail cell is legal, and filling
all 576 cells completes the board.

Movement shows the current cell toward its next destination, rather than a past
simulation tick. Turning reuses the current fractional progress immediately:
at 60%, `(4, 5.6)` becomes `(4.6, 5)` when turning right from the cell `(4, 5)`.
A turn back into the neck is rejected. A wall collision ends the run when the
attempted move reaches the next tick; the stopped run then plays a half-second
compression and rebound before showing the end screen. Reduced motion suppresses
decorative animation while preserving the actual movement position.

The native renderer supplies gradient surfaces, soft shadows, subtle reflections,
antialiased strokes and hover fades. These are reusable UI-library features;
the game declaration only selects the theme and components.

The interface uses reusable DynLex sections from `lib/ui_components.dl` and the
native renderer in `lib/ui_native.dl`. It does not require HTML, CSS, or game-specific
compiler syntax.

Run `scripts/test.sh` for the compiler and application tests. On Linux, the native UI
test runs the real application on a private Xvfb display and exercises navigation,
settings, movement, pause, restart and resizing:

```sh
python3 -B scripts/test_snake_ui.py . ./build/dynlex --snapshots /tmp/snake-snapshots
```

The same test also measures steady rendering and checks a completely filled board
and the native impact animation. Its setup is separate in `tests/runtime/snake_rendering.dl`.
