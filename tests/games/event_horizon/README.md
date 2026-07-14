# Event Horizon

An interactive, real-time black-hole artwork written entirely in DynLex. It
combines gravitational lensing, layered procedural nebulae, three star strata,
a turbulent accretion disk, Doppler color shift, lensed polar arcs, a photon
ring, filmic exposure, and a click-triggered shockwave.

Build from the repository root:

```bash
./build/dynlex tests/games/event_horizon/vertex.dl --emit-spirv --shader-stage=vertex -o tests/games/event_horizon/vertex.spv
./build/dynlex tests/games/event_horizon/horizon.dl --emit-spirv --shader-stage=fragment -o tests/games/event_horizon/horizon.spv
./build/dynlex tests/games/event_horizon/main.dl -o tests/games/event_horizon/event_horizon
```

Run:

```bash
./tests/games/event_horizon/event_horizon
```

- Use WASD or the arrow keys to move the camera across the lens.
- Move the mouse to bend the viewpoint.
- Click to emit a gravitational shockwave.
- Press R to recenter the camera.
- Press Escape to close the window.
