# Geometry Dash Rebuild Plan

## Goal

Rebuild the game around one rule: generate the scene from a simulated reference route, then let the real player use the same physics with free jump input.

The game is not rhythm-gated. Rhythm shapes the baseline route generation, not player input.

## Hard Requirements

### Input And Physics

- The player may jump at any time when standing on a valid surface.
- Holding jump must still trigger jumps.
- The real player and the reference player must use one shared implementation for movement and collision.
- Shared behavior includes gravity, jump velocity, horizontal speed, hitbox size, standing, landing, wall hits, ceiling hits, and step-up.

### Generation

- Generate a reference trajectory first, then place geometry to match it.
- For every planned landing, set `platform_top_y` to the reference player bottom `y` at landing time.
- For every planned slide, keep continuous support under the reference player for the full slide duration.
- Use the reference player hitbox to reject any geometry that intersects the route.
- Quick jump sequences must create visible staircases.
- Some higher platforms should only be reachable by chained jumps from earlier platforms.
- Bottom and top geometry must both exist, and the top geometry must form a real ceiling.
- Floor and ceiling geometry must move with the scene.
- Additional platforms are generated at random positions.
- Any additional platform that intersects the reference player hitbox path is immediately destroyed.

### Timing

- Target an average cadence of about `2 jumps / second`.
- Mix short bursts with longer reset intervals.
- Track height trend so the route does not only climb or only descend.
- Never require a follow-up jump before a safe post-takeoff minimum time, and keep margin above that minimum.

### World Lifetime

- Platforms must not disappear while the player is standing on them.
- Recycle geometry only when it is almost fully out of view on the left.
- Spawn distance on the right should mirror recycle distance on the left.

## Core Invariants

1. The reference route is collision-free.
2. Every planned landing has valid support.
3. Every planned slide stays supported until the next jump.
4. Every placed platform has positive width and non-negative height.
5. No non-ground floor platform is rendered at height `0`.
6. Top and bottom solids never overlap.
7. Extra geometry never blocks the baseline route.
8. Runtime and validation use the same physics and collision rules.

## File Layout

Keep the game in `tests/games/geometry_dash/` and split responsibilities cleanly.

- `main.dl`: game loop, rendering, input, shared scene consumption
- `physics.dl`: shared movement and collision logic
- `generator.dl`: timing generation, reference simulation, baseline platform placement, optional extra geometry
- `scene.dl`: world tables and spawn/recycle helpers
- `debug_sim.dl`: headless invariant validation
- `inspect_scene.dl`: scene metrics and sampled geometry output
- `print_trajectory.dl`: generated timing and landing data for iteration

If DynLex limitations force a different split, keep the same separation of concerns.

## Generation Pipeline

### Step 1: Generate timing plan

Produce:

- `jump_time_ms[i]`
- `slide_time_ms[i]`

Rules:

- target average near `500ms` between jump starts
- allow bursts for staircase sequences
- include longer resets
- enforce a minimum legal re-jump time plus margin
- occasionally plan multi-step climbs

### Step 2: Simulate reference player

Run the reference player through the exact gameplay physics.

The simulation output that matters for generation is:

- the reference player `y` position at relevant moments
- the reference player hitbox over time

Do not treat times or x coordinates as primary generated data. They are derived from the control plan and movement rules.

If any planned action is impossible, generation fails.

### Step 3: Build baseline platforms

For each landing/slide segment:

- place the platform top from the reference player bottom `y`
- size the platform so the full planned slide remains supported
- keep a small landing margin around the intended landing point
- use the hitbox path to verify that the placed geometry does not intersect the reference route
- use the simulated higher landing `y` values directly for staircase runs

Discrete levels may bias generation, but final placement must come from simulated positions.

### Step 4: Add optional extra geometry

Add non-baseline platforms only after the baseline route is valid.

Rules:

- place additional platforms at random positions
- immediately destroy any additional platform that intersects the reference trajectory hitbox volume
- do not let additional platforms intersect baseline platforms

### Step 5: Build scrolling world tables

Store stable world-space geometry once. Rendering and simulation must consume the same tables.

## Collision Model

Use axis-aligned hitboxes with explicit queries for:

- `is standing on support`
- `would land on support this frame`
- `would hit wall this frame`
- `would hit ceiling this frame`
- `can step up onto support`

Step-up must only handle small edge transitions. It must never act as a generic upward snap.

## Verification Strategy

Do not rely on the rendered window alone.

### `debug_sim.dl`

Must:

- simulate the full reference route
- use the same movement code as `main.dl`
- report the exact step, time, player box, and solid box on failure
- exit with error on any collision or unsupported slide

### `inspect_scene.dl`

Must print metrics such as:

- average, minimum, and maximum jump interval
- minimum legal re-jump margin used
- number of baseline platforms and extra platforms
- highest platform top and lowest ceiling bottom
- longest staircase run
- number of landings above ground-only jump reach
- top-platform vs bottom-platform counts
- zero-height or suspicious platform counts

Also print sampled early-step data:

- step index
- jump time
- slide time
- landing bottom y
- platform left/right
- platform top y
- support type

Optional: add a coarse ASCII slice view to inspect platform spans and landing points without opening a window.

## Definition Of Done

The rebuild is done when:

- `main.dl`, `debug_sim.dl`, and `inspect_scene.dl` compile quickly
- the reference player completes the generated route with zero collisions
- staircase sequences are visible
- clearly elevated and chained-jump platforms exist
- top and bottom geometry both appear and scroll consistently
- challenge comes from geometry, not input gating
- no physics or generation logic is duplicated across main/debug/inspect paths
