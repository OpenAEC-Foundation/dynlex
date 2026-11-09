# Event Horizon

An interactive, real-time black-hole artwork written entirely in DynLex. It
combines gravitational lensing, layered procedural nebulae, three star strata,
a turbulent accretion disk, Doppler color shift, lensed polar arcs, a photon
ring, filmic exposure, and a click-triggered shockwave.

Build from the repository root:

```bash
python3 scripts/play_game.py event-horizon --build-only
```

Run:

```bash
./build/games/event-horizon.out
```

- Use WASD to move the camera across the lens.
- Move the mouse to bend the viewpoint.
- Click to emit a gravitational shockwave.
- Press R to recenter the camera.
- Press Space to pause and resume the animation.
- Use the gallery buttons or Left/Right to switch between the five studies.
- Press Escape to close the window.

Event Horizon shares the declarative gallery in `tests/games/gallery`. Its image
and pointer coordinates use the canvas's framebuffer bounds, including after
resizing and on displays with scaled pixels.
