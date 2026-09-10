import assert from "node:assert/strict";
import { Vector3 } from "../../web/vendor/three/three.module.min.js";
import { ActorMotion } from "../../web/farm-motion.js";

for (const duration of [.5, .25]) {
  const motion = new ActorMotion(new Vector3(), 1);
  const samples = [];
  const dt = 1 / 120;
  for (let frame = 0; frame < 120 * duration * 5; frame++) {
    if (frame % (120 * duration) === 0) motion.retarget(new Vector3(frame / (120 * duration) + 1, 0, 0), 1, duration);
    const previous = motion.position.x;
    motion.advance(dt);
    samples.push((motion.position.x - previous) / dt);
  }
  // Crossing successive tile boundaries must not restart an ease-out animation.
  for (const speed of samples.slice(120 * duration)) assert.ok(speed > .95 / duration && speed < 1.05 / duration);
  const beforeTurn = motion.yaw;
  motion.retarget(new Vector3(5, 0, 1), 2, duration);
  motion.advance(dt);
  assert.ok(Math.abs(motion.yaw - beforeTurn) < .2, "Facing must not snap ninety degrees");
  for (let i = 0; i < 240; i++) motion.advance(dt);
  assert.ok(motion.position.distanceTo(new Vector3(5, 0, 1)) < 1e-6);
  assert.ok(motion.weight < .001, "The walk cycle blends back to rest");
  assert.ok(Math.abs(motion.yaw) < .001, "The character finishes facing its destination");
  motion.place(new Vector3(8, .4, 8), 3);
  assert.deepEqual(motion.position.toArray(), [8, .4, 8]);
  motion.advance(.1);
  assert.deepEqual(motion.position.toArray(), [8, .4, 8], "Dropping a worker clears the old walking path");
}
const backward = new ActorMotion(new Vector3(), 0);
backward.retarget(new Vector3(0, 0, 1), 0, .5);
for (let i = 0; i < 120; i++) {
  backward.advance(1 / 120);
  assert.ok(Math.abs(backward.yaw - Math.PI) < 1e-6, "Stepping backward preserves the character's facing");
}
const regularFrames = new ActorMotion(new Vector3(), 1), slowFrames = new ActorMotion(new Vector3(), 1);
for (let tile = 1; tile <= 4; tile++) {
  for (const motion of [regularFrames, slowFrames]) motion.retarget(new Vector3(tile, 0, 0), 1, .5);
  for (let frame = 0; frame < 60; frame++) regularFrames.advance(1 / 120);
  slowFrames.advance(.5);
  assert.ok(slowFrames.position.distanceTo(regularFrames.position) < 1e-6, "Slow rendering must not slow down walking");
}
slowFrames.advance(1);
assert.ok(slowFrames.position.distanceTo(new Vector3(4, 0, 0)) < 1e-6, "Releasing a held key must not leave a backlog of animation");
console.log("Walking keeps its speed across tiles at both paces, turns smoothly, settles, and resets on placement.");
