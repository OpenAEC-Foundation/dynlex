import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { tmpdir } from "node:os";
import path from "node:path";
import { createFarm } from "../../web/farm-world.js";
import { addArea, reshapeArea, removeArea } from "../../web/farm-areas.js";
import { destination, sense, perform, plannedRoute } from "../../web/farm-actions.js";
import { FARM_PREFIX, parseFarmPlan, advanceRoutine, newRoutine } from "../../web/farm-program.js";

const world = createFarm(), actor = world.workers[0];
const area = addArea(world, "Far orchard", {x:11,y:5}, {x:8,y:2});
assert.deepEqual([area.left,area.top,area.right,area.bottom], [8,2,11,5]);
const target = destination(world,actor,"tree",area.name);
assert.ok(target.x>=8 && target.x<=11 && target.y>=2 && target.y<=5);
assert.notEqual(destination(world,actor,"tree"), target, "The scope excludes nearer trees outside the area");
assert.equal(destination(world,actor,"tree","Missing"), null);
assert.throws(() => addArea(world,"Far orchard",{x:1,y:1},{x:2,y:2}), /already/);
assert.throws(() => addArea(world,"Locked",{x:14,y:1},{x:15,y:2}), /available/);
reshapeArea(world,area.id,{x:0,y:0},{x:1,y:1});
assert.equal(destination(world,actor,"tree",area.name), null);
reshapeArea(world,area.id,{x:8,y:2},{x:11,y:5});

const directory=mkdtempSync(path.join(tmpdir(),"dynlex-farm-areas-"));
try {
  const file=path.join(directory,"main.dl"), binary=path.join(directory,"main.out");
  writeFileSync(file,FARM_PREFIX+`if there are fewer than 3 logs in the barn:
    walk to the nearest tree in the area "Far orchard"
else:
    wait
if there are at least 2 axes in the barn:
    turn left`);
  execFileSync(path.resolve("build/dynlex"),[file,"--trace-execution","-o",binary]);
  const program=parseFarmPlan(execFileSync(binary,{encoding:"utf8"}));
  assert.equal(program[0].amount,3);
  assert.equal(program[1].area,area.name);
  assert.equal(program[4].amount,2);
  const routine=newRoutine();
  advanceRoutine(routine,program,(...args)=>sense(world,actor,...args),(...args)=>perform(world,actor,...args));
  assert.equal(routine.pc,1);
  actor.routine=routine;
  const route=plannedRoute(world,actor,program);
  assert.equal(route.target,target);
  assert.match(route.label,/Far orchard/);
  assert.equal(sense(world,actor,{...program[0],amount:3}),true);
  world.stock.logs=3;
  assert.equal(sense(world,actor,{...program[0],amount:3}),false);
  assert.equal(sense(world,actor,{...program[4],amount:4}),true);
  assert.equal(sense(world,actor,{...program[4],amount:5}),false);
  removeArea(world,area.id);
  assert.equal(destination(world,actor,"tree",area.name),null);
  assert.match(perform(world,actor,program[1]).message,/Far orchard/);
} finally { rmSync(directory,{recursive:true,force:true}); }
console.log("Named areas constrain live destinations and routes; compiled barn conditions respect quantity boundaries.");
