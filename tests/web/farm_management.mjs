import assert from "node:assert/strict";
import { createFarm, createWorker, routeTo, moveToward } from "../../web/farm-world.js";
import { createRoles, createRole, copyRole } from "../../web/farm-roles.js";
import { parseFarmPlan } from "../../web/farm-program.js";
import { assignRole, tickFarm } from "../../web/farm-simulation.js";
import { plannedRoute } from "../../web/farm-actions.js";
import { canCross } from "../../web/farm-boundaries.js";

const roles = createRoles();
roles[0].program = parseFarmPlan("LOOP|-1\nDO|1|3|-1|-2|\nEND");
roles[0].applied = "loop forever:\n    turn right";
const copy = copyRole(roles, roles[0]);
assert.notEqual(copy.id, roles[0].id);
assert.deepEqual(copy.program, roles[0].program);
copy.program[1].argument = 2;
assert.equal(roles[0].program[1].argument, 3, "Copied programs can be changed independently");
assert.notEqual(copyRole(roles, roles[0]).name, copy.name);
const deletedId = createRole("First").id;
assert.notEqual(createRole("Next").id, deletedId);

const world = createFarm(), first = world.workers[0], second = createWorker("worker-2", "Bo", 5, 12);
world.workers.push(second);
assignRole(first, roles[0].id); assignRole(second, roles[0].id);
first.paused = true;
const pausedRoutine = structuredClone(first.routine);
tickFarm(world, roles);
assert.equal(first.direction, 0); assert.deepEqual(first.routine, pausedRoutine);
assert.equal(second.direction, 1, "Other workers continue while one is paused");
first.paused = false;
tickFarm(world, roles);
assert.equal(first.direction, 1);

const target = world.barn, route = routeTo(world, first, target);
let previous = first;
for (const point of route) {
  assert.ok(canCross(world, previous, point)); previous = point;
}
for (const point of route) {
  moveToward(world, first, target);
  assert.deepEqual({x:first.x,y:first.y}, point, "Preview and actual movement use the same path");
}
assert.deepEqual(routeTo(world, first, target), []);
const walking = parseFarmPlan("DO|2|0|-1|0|");
assignRole(first, "walker");
assert.equal(plannedRoute(world, first, walking).label, "the barn");
assert.equal(plannedRoute(world, first, null), null);
console.log("Role copies stay independent; pauses isolate workers; route previews agree with movement.");
