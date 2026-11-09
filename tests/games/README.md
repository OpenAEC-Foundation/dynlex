# Games and visual studies

Run these commands from the repository root. Build the compiler once, then use
the launcher to compile a game, prepare its shaders, and open its native window:

```sh
./scripts/build.sh --lint=false
python3 scripts/play_game.py breakout
```

| Launcher name | Experience |
| --- | --- |
| `snake` | Snake Studio, including pace, palette, wrapping and motion preferences |
| `breakout` | LEXI-8 Breakout, with a live scoreboard and adjustable phosphor display |
| `geometry` | Classic cube auto-runner with spikes, gaps, elevated platforms and automatic retries |
| `shaders` | Five live studies: Mandelbrot, Plasma, Julia, Rings and Event Horizon |
| `mandelbrot`, `plasma`, `event-horizon` | Open the gallery at the named study |
| `cube`, `city` | Spatial Studio, with shared camera controls and pause/reset |
| `typography` | A resizable specimen of the native UI's typography |

The native interfaces use declarative DynLex windows, pages, panels, controls,
canvas callbacks and fitted layouts. Fonts are bundled in `fonts/`. Mouse and
Tab/Enter navigation work throughout; each window displays its keyboard shortcuts.
Resizing preserves the layout and pointer coordinates. Simulation and camera
movement use elapsed time rather than the display's refresh rate.

`geometry` is a playable classic cube game. Press Space/Up or click the playfield
to jump; holding repeats the fixed-strength jump on landing. Block sides,
ceilings, spikes and falling into gaps kill the cube. Death automatically retries
the same seeded level, preserving the best percentage. P/Escape pauses, R retries,
and Q closes. The pause menu can generate a new level.
The generator builds 32 jumps, spike groups, gaps, ascending and descending chains,
and overhead solids. It replays the entire course through the gameplay collision
code with each jump timing shifted by up to three ticks at 120 Hz. See
[the generation design](geometry_dash/PLAN.md) for the algorithm and debug tools.

The gallery's generated shaders and all launcher-built
executables live under `build/games/`.
The city uses one block/road/lot geometry definition, including across negative
map coordinates, so buildings stay on their pavement and the opening camera
starts on a clear road.

The browser farm and river games retain their current web renderer. Their game
vocabulary lives in `lib/farm_challenge.dl` and `lib/river_challenge.dl`; the farm
vocabulary is generated from `web/farm-vocabulary.js`. The river's event protocol
and existing player programs remain unchanged.
The generated farm vocabulary uses a file-scoped 32-bit numeric rule instead of
casting every instruction constant, keeping its protocol stable under caller
number rules.

## Verification

```sh
./scripts/test.sh --integration
python3 -B scripts/test_games_ui.py --snapshots /tmp/dynlex-game-snapshots
python3 -B scripts/test_snake_ui.py . ./build/dynlex
```

The UI checks use Xvfb and a Vulkan software driver on Linux, explicitly selecting
the private X11 display even when the desktop uses Wayland. They exercise the real
executables, pause and resume scenes, reset sessions, change controls, resize
windows and save screenshots. `--game NAME` limits a UI check to one experience.
The headless integration fixtures independently verify gameplay and UI callbacks.
