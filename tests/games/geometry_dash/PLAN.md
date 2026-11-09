# Classic cube gameplay and obstacle generation

This game uses the classic auto-running cube mode. The player runs continuously,
has one fixed-strength jump, rotates in the air, and squares itself on landing.
Holding Space, Up or the mouse button repeats jumping whenever support is reached.
Spikes, block sides, ceilings and gaps are lethal. Death plays a short particle
burst and retries the same level. Attempts and best completion persist through
retries; generating another seed starts a new level.

The native declarative UI supplies the start, pause and completion screens over a
full-width playfield. The artwork uses outlined blocks and spikes, a green cube,
parallax geometry, a moving trail and a visual pulse. No reference player is drawn
or used to move the real cube. The route is a generator witness, not an input gate.

## Path first, geometry second

1. Seed the generator and select 32 jump motifs. The opening teaches single,
   double and triple spikes. Later motifs mix gaps, elevated landings, descents
   and repeated ascending/descending chains, with longer intervals between phrases.
2. Use the same gravity integrator as gameplay to calculate each jump's descending
   crossing of the next surface. Horizontal speed determines the landing X.
3. For flat jumps, continue the supporting floor and center a spike group beneath
   the safe part of the arc. For gaps and height changes, end the departure
   support after takeoff and begin the next platform just before the landing.
   Height changes are limited to one cube at a time; successive platforms reach
   heights unavailable to a single jump from the original ground.
4. Add overhead collision solids above the reachable envelope. All solids and
   hazards live in one stable world-space table. Scrolling only changes rendering
   coordinates; it never creates or deletes collision objects during an attempt.
5. Replay the complete course through the actual movement and collision function
   at every whole-tick input offset from -3 to +3. All seven routes must land,
   survive and reach the finish. A failed generation is an explicit error.

Three ticks at 120 Hz is 25 ms on either side of the reference input. This is a
minimum tested timing tolerance, not an input rhythm lock. Real input remains free.
The repeatable seed preserves the level between attempts so the player can learn it.

## Physics and collision

World coordinates use pixels with downward Y. Simulation runs at 120 Hz independent
of rendering. The cube is 36 pixels wide, speed is 3.1 pixels/tick, gravity is
0.28 pixels/tick squared, and takeoff velocity is -7.8 pixels/tick. Falling landings
use the previous bottom position to cross a solid's top; side and ceiling overlaps
kill instead of snapping the cube upward. The spike collision tests the actual
triangle against a slightly inset lethal hitbox rather than a rectangular spike box.

The finite level uses at most 256 obstacle slots and 32 jump actions. The generator
checks its capacity and positive geometry dimensions. The runtime and validation
consume the same objects and `step ... through ... holding jump ...` function.

## Files and verification

- `physics.dl`: cube, obstacle and level data; jump, landing and lethal collisions.
- `generator.dl`: seeded construction and complete-route timing validation.
- `session.dl`: input state, attempts, death, retry, pause and completion.
- `interface.dl`: renderer-independent native UI descriptions and callbacks.
- `artwork.dl`, `painting.dl`: scenery, solids, spikes, cube and particles.
- `application.dl`: window input and elapsed-time integration.
- `debug_sim.dl`: replays the witness through a real gameplay session.
- `inspect_scene.dl`, `print_trajectory.dl`: geometry metrics and jump schedule.
- `tests/integration/dash_physics`: jumps, repeated input, landing, walls, ceilings,
  triangle hitboxes and falling.
- `tests/integration/dash_generation`: 100 seeds, every timing offset, determinism,
  elevated chains and failure without jumping.
- `tests/integration/geometry_session`: pause, retry, best progress and completion
  through the real session.

Run from any directory:

```sh
python3 /home/jan/Documents/Github/dynlex/.worktrees/modern-games/scripts/play_game.py geometry
```
