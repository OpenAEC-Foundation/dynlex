import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {mkdtempSync,writeFileSync,rmSync} from "node:fs";
import {tmpdir} from "node:os";
import path from "node:path";
import {FARM_PREFIX,parseFarmPlan} from "../../web/farm-program.js";
import {createFarm,createWorker,addItem,removeFlag,addFlag,relocate,dropItem} from "../../web/farm-world.js";
import {perform,sense,plannedRoute} from "../../web/farm-actions.js";
import {targetPoint} from "../../web/farm-targets.js";
import {action,condition,itemId,targetId,SUBJECT_TARGET,ALL_ITEMS} from "../../web/farm-vocabulary.js";
import {assignRole,stepWorker,updateEnvironment} from "../../web/farm-simulation.js";

const directory=mkdtempSync(path.join(tmpdir(),"dynlex-farm-arguments-"));
try {
  const source=path.join(directory,"main.dl"), binary=path.join(directory,"main.out");
  writeFileSync(source,FARM_PREFIX+`walk to the nearest tree
if it's still there:
    if i can reach it:
        chop it down
walk to the barn
store 2 logs
store everything
grab an axe
put down it
if i can reach the nearest chicken:
    feed it
plant wheat in the nearest empty plot
water the nearest thirsty crop
take fertilizer from the compost heap
add scraps to the compost heap`);
  execFileSync(path.resolve("build/dynlex"),[source,"--trace-execution","-o",binary]);
  const program=parseFarmPlan(execFileSync(binary,{encoding:"utf8"}));
  const actions=program.filter(entry=>entry.op==="action");
  assert.equal(program[1].key,"exists");
  assert.equal(program[1].target,-1);
  assert.equal(program[2].key,"reach");
  assert.equal(program[2].target,-1);
  assert.equal(actions.find(entry=>entry.key==="store").amount,2);
  assert.equal(actions.find(entry=>entry.key==="drop").argument,-2);
  assert.ok(program.every(entry=>entry.range!==null));

  writeFileSync(source,FARM_PREFIX+`walk to the nearest cow
if i can reach it:
    feed it
walk to the nearest chicken
walk to the nearest tree
walk to the nearest egg
walk to the nearest barn
walk to the cow`);
  execFileSync(path.resolve("build/dynlex"),[source,"-o",binary]);
  const destinations=parseFarmPlan(execFileSync(binary,{encoding:"utf8"})).filter(entry=>entry.key==="walk");
  assert.deepEqual(destinations.map(entry=>entry.target),["cow","chicken","tree","egg","barn","cow"].map(targetId));

  writeFileSync(source,FARM_PREFIX+`walk to the nearest tree
if it's still there:
    if i can reach it:
        chop it down`);
  execFileSync(path.resolve("build/dynlex"),[source,"--trace-execution","-o",binary]);
  const role={id:"woodcutters",program:parseFarmPlan(execFileSync(binary,{encoding:"utf8"}))};
  const world=createFarm(), first=world.workers[0], second=createWorker("worker-2","Bo",10,6);
  world.workers.push(second);
  for(const worker of world.workers) {
    addItem(worker,"axe");assignRole(worker,role.id);
    for(let step=0;step<40 && worker.routine.pc===0;step++)stepWorker(world,[role],worker);
    assert.equal(worker.routine.pc,1);
  }
  const firstTree=targetPoint(world,first.subject.value), secondTree=targetPoint(world,second.subject.value);
  assert.notEqual(firstTree,secondTree);
  firstTree.tree.regrow=180; // Another worker has removed this tree between instructions.
  stepWorker(world,[role],first);
  assert.equal(first.routine.trace.branch,false);
  assert.equal(first.routine.pc,role.program.length);
  stepWorker(world,[role],second);
  assert.equal(second.routine.trace.branch,true);
  stepWorker(world,[role],second);
  assert.equal(second.routine.trace.branch,true);
  for(let hit=0;hit<3;hit++)stepWorker(world,[role],second);
  assert.equal(secondTree.tree.regrow,180);
  assert.equal(second.routine.pc,role.program.length);
} finally {rmSync(directory,{recursive:true});}

{
  const world=createFarm(), actor=world.workers[0];
  Object.assign(actor,{x:3,y:14});addItem(actor,"axe");addItem(actor,"logs",3);
  perform(world,actor,action("store",{argument:itemId("logs"),amount:2}));
  assert.equal(world.stock.logs,2);
  assert.deepEqual(actor.inventory.map(({kind,count})=>({kind,count})),[{kind:"axe",count:1},{kind:"logs",count:1}]);
  perform(world,actor,action("drop",{argument:itemId("logs"),amount:1}));
  assert.equal(actor.inventory.length,1);assert.equal(actor.inventory[0].kind,"axe");
  perform(world,actor,action("store",{argument:ALL_ITEMS}));assert.deepEqual(actor.inventory,[]);
  perform(world,actor,action("collect",{argument:itemId("bucket"),amount:1}));actor.inventory[0].water=3;
  const receiver=createWorker("worker-2","Bo",4,14);world.workers.push(receiver);
  perform(world,actor,action("give",{argument:itemId("bucket"),amount:1,target:targetId("worker")}));
  assert.equal(receiver.inventory[0].water,3);assert.deepEqual(actor.inventory,[]);
}
{
  const world=createFarm(),actor=world.workers[0],other=createWorker("worker-2","Bo",10,5);world.workers.push(other);
  perform(world,actor,action("focusTarget",{target:targetId("tree")}));
  perform(world,other,action("focusTarget",{target:targetId("tree")}));
  assert.notDeepEqual(actor.subject,other.subject,"Workers remember independent targets");
  const chosen=targetPoint(world,actor.subject.value);addItem(actor,"axe");Object.assign(actor,{x:chosen.x+1,y:chosen.y});
  assert.equal(sense(world,actor,condition("reach",{target:SUBJECT_TARGET})),true);
  for(let hit=0;hit<3;hit++)perform(world,actor,action("chop",{target:SUBJECT_TARGET}));
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),false);
  chosen.tree.regrow=1;updateEnvironment(world);
  assert.equal(chosen.tree.regrow,0);
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),false,"A replacement tree does not revive the previous subject");
  assert.equal(sense(world,other,condition("exists",{target:SUBJECT_TARGET})),true);
  perform(world,actor,action("focusTarget",{target:targetId("red")}));
  removeFlag(world,"red");addFlag(world,"red");
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),false,"A new flag of the same color is a different entity");
  perform(world,actor,action("focusTarget",{target:targetId("chicken")}));
  const chicken=targetPoint(world,actor.subject.value);assert.ok(relocate(world,chicken.id,8,14));
  assert.equal(targetPoint(world,actor.subject.value),chicken,"Dragging preserves the referenced animal");
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),true);
}
{
  const world=createFarm(),actor=world.workers[0],role={id:"walker",program:[action("walk",{target:targetId("tree")})]};
  assignRole(actor,role.id);stepWorker(world,[role],actor);
  const remembered=structuredClone(actor.subject);
  const tile=world.tiles[actor.y][actor.x+1];tile.tree={serial:world.nextEntity++,hits:3,regrow:0};
  const route=plannedRoute(world,actor,role.program);
  assert.equal(route.target,targetPoint(world,remembered.value));
  stepWorker(world,[role],actor);
  assert.deepEqual(actor.subject,remembered,"Walking keeps its selected tree when a nearer tree appears");
}
{
  const world=createFarm(), actor=world.workers[0];
  Object.assign(actor,{x:3,y:14});world.stock.logs=5;
  perform(world,actor,action("collect",{argument:itemId("logs"),amount:5}));
  assert.deepEqual(actor.inventory.map(item=>item.count),[3,2],"Explicit quantities can span both hands");
  assert.equal(world.stock.logs,0);
  perform(world,actor,action("store",{argument:itemId("logs"),amount:4}));
  assert.equal(world.stock.logs,4);
  assert.deepEqual(actor.inventory.map(item=>item.count),[1]);
  actor.inventory=[];
  dropItem(world,actor,"eggs",1);
  perform(world,actor,action("focusTarget",{target:targetId("egg")}));
  const remembered=structuredClone(actor.subject);
  perform(world,world.player,action("collect",{argument:itemId("eggs")}));
  // The player is two squares away, so those eggs have not been collected.
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),true);
  Object.assign(world.player,{x:3,y:15});
  perform(world,world.player,action("collect",{argument:itemId("eggs")}));
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),false);
  dropItem(world,actor,"eggs",1);
  assert.deepEqual(actor.subject,remembered);
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),false,"Fresh eggs at the same square are a new pile");
  Object.assign(actor,{x:8,y:8});
  perform(world,actor,action("focusTarget",{target:targetId("ripe")}));
  perform(world,actor,action("harvest",{target:SUBJECT_TARGET}));
  assert.equal(sense(world,actor,condition("exists",{target:SUBJECT_TARGET})),false,"Harvesting removes the remembered crop");
}
console.log("Farm verbs accept targets, item selections, quantities, and DynLex subjects.");
