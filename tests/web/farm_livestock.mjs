import {action,itemId,targetId} from "../../web/farm-vocabulary.js";
import assert from "node:assert/strict";
import { createFarm, moveToward, distance, animals, relocate, draggableById, addFlag, removeFlag, moveFlag } from "../../web/farm-world.js";
import { FENCES, canCross, penAt } from "../../web/farm-boundaries.js";
import { tickFarm, deliverOrder } from "../../web/farm-simulation.js";
import { destination, perform } from "../../web/farm-actions.js";
const world = createFarm(), farmer = world.player;
assert.equal(world.chickens.length, 2);
for (const fence of FENCES.filter(f => !f.gate && !f.locked)) {
  assert.equal(canCross(world, fence.a, fence.b), false);
  assert.equal(canCross(world, fence.b, fence.a), false);
}
Object.assign(farmer, {x:10,y:7,direction:1});
perform(world, farmer, action("step"));
assert.equal(farmer.x, 10, "Manual steps cannot cross a fence");
const target = {x:11,y:7};
const route = [];
for (let i=0;i<30 && distance(farmer,target)>0;i++) {
  const before = {x:farmer.x,y:farmer.y};
  moveToward(world,farmer,target,false);
  assert.ok(canCross(world,before,farmer));route.push({...farmer});
}
assert.equal(distance(farmer,target),0,"Pathfinding enters through the gate");
assert.ok(route.some(p=>p.x===12 && p.y===8));
Object.assign(farmer,{x:5,y:14});
Object.assign(world.stock,{carrots:4,logs:2});deliverOrder(world);
Object.assign(world.stock,{wheat:4,eggs:2});deliverOrder(world);
const seen = new Map(animals(world).map(a=>[a.id,new Set()]));
for(let i=0;i<150;i++) {
  const previous = animals(world).map(a=>({...a}));
  tickFarm(world,[]);
  for(const animal of animals(world)) {
    const before=previous.find(a=>a.id===animal.id);
    assert.ok(canCross(world,before,animal,true));
    assert.equal(penAt(animal.x,animal.y).id, animal.id==='cow'?'cow':'chickens');
    seen.get(animal.id).add(`${animal.x},${animal.y}`);
  }
  assert.equal(new Set(animals(world).map(a=>`${a.x},${a.y}`)).size,3);
}
assert.ok([...seen.values()].every(positions=>positions.size>2),"Each animal wanders around its pen");
const chicken = destination(world,farmer,"chicken");
assert.ok(world.chickens.includes(chicken));
assert.equal(distance(farmer,chicken),Math.min(...world.chickens.map(a=>distance(a,farmer))));
chicken.feed=1;
for(let i=0;i<20;i++) tickFarm(world,[]);
const egg=destination(world,farmer,"egg");
assert.ok(egg.items.some(item=>item.kind==='eggs'));
for(let i=0;i<80;i++) if(moveToward(world,farmer,egg).done) break;
perform(world, farmer, action("collect",{argument:itemId("eggs")}));
assert.equal(farmer.inventory.find(i=>i.kind==='eggs').count,1);
assert.equal(destination(world,farmer,"egg"),null,"Collected eggs disappear from the ground");
console.log("Fence collisions, gate routes, wandering livestock and individual chicken/egg targets passed.");

const yard=createFarm(), bird=yard.chickens[0];
assert.equal(draggableById(yard,bird.id),bird);
assert.equal(relocate(yard,bird.id,3,4),false,"Chickens cannot be dropped in the pond");
assert.equal(relocate(yard,bird.id,yard.chickens[1].x,yard.chickens[1].y),false);
assert.equal(relocate(yard,bird.id,7,14),true);
bird.held=true;
const heldPosition={x:bird.x,y:bird.y};
for(let i=0;i<20;i++) tickFarm(yard,[]);
assert.deepEqual({x:bird.x,y:bird.y},heldPosition,"Held chickens wait for placement");
bird.held=false;
const outside=new Set();
for(let i=0;i<60;i++) { const before={...bird};tickFarm(yard,[]);assert.ok(canCross(yard,before,bird,true));outside.add(`${bird.x},${bird.y}`); }
assert.ok(outside.size>2,"A relocated chicken can wander outside a pen");
assert.equal(addFlag(yard,"red"),false,"Each flag color is unique");
assert.equal(addFlag(yard,"purple"),true);
assert.equal(destination(yard,yard.player,"purple"),yard.flags.find(f=>f.id==="purple"));
removeFlag(yard,"purple");
assert.equal(moveFlag(yard,"purple",7,14),false,"A removed flag cannot finish a pending drag");
assert.equal(destination(yard,yard.player,"purple"),null,"Deleted flags are no longer destinations");
assert.equal(addFlag(yard,"purple"),true,"Removing a flag frees its color");
