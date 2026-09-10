import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {FARM_PREFIX,parseFarmPlan} from '../../web/farm-program.js';
import {createFarm,createWorker,addItem,dropItem} from '../../web/farm-world.js';
import {perform} from '../../web/farm-actions.js';
import {action,itemId,targetId,NO_TARGET,SUBJECT_TARGET,ALL_ITEMS} from '../../web/farm-vocabulary.js';

const verbs=['grab','take','collect','pick up'];
const directory=mkdtempSync(path.join(tmpdir(),'dynlex-collection-'));
try {
  const source=path.join(directory,'main.dl'), binary=path.join(directory,'main.out');
  const commands=verbs.flatMap(verb=>[`${verb} fertilizer`,`${verb} 2 fertilizer from the compost heap`]);
  writeFileSync(source,FARM_PREFIX+commands.join('\n'));
  execFileSync(path.resolve('build/dynlex'),[source,'-o',binary]);
  const plan=parseFarmPlan(execFileSync(binary,{encoding:'utf8'}));
  for (let index=0;index<plan.length;index++) {
    const explicit=index%2===1, instruction=plan[index];
    assert.equal(instruction.key,'collect',commands[index]);
    assert.equal(instruction.target,explicit?targetId('compost'):NO_TARGET,commands[index]);
    const world=createFarm(),actor=world.workers[0];
    Object.assign(actor,{x:4,y:8});world.compost.ready=4;
    perform(world,actor,instruction);
    assert.equal(actor.inventory[0].kind,'fertilizer');
    assert.equal(actor.inventory[0].count,explicit?2:4,commands[index]);
    assert.equal(world.compost.ready,explicit?2:0);
  }
} finally {rmSync(directory,{recursive:true});}

const collect=(kind,options={})=>action('collect',{argument:itemId(kind),...options});
{
  const world=createFarm(),actor=world.workers[0];Object.assign(actor,{x:3,y:14});
  world.stock.fertilizer=8;dropItem(world,actor,'fertilizer',2);
  perform(world,actor,collect('fertilizer',{amount:5}));
  assert.equal(actor.inventory[0].count,5);assert.equal(world.stock.fertilizer,5);
  assert.deepEqual(world.tiles[14][3].items,[],'Nearest loose items are used before equally reachable storage');
  actor.inventory=[];
  perform(world,actor,collect('fertilizer'));
  assert.equal(actor.inventory[0].count,5);assert.equal(world.stock.fertilizer,0);
  world.stock.wheat=18;actor.inventory=[];
  perform(world,actor,collect('wheat'));
  assert.equal(actor.inventory[0].count,6,'An unnumbered item takes one bag');
  assert.equal(actor.inventory.length,1,'Leave room for another kind of supply');
  actor.inventory=[];world.stock.logs=5;
  perform(world,actor,collect('logs',{amount:5,target:targetId('barn')}));
  assert.deepEqual(actor.inventory.map(item=>item.count),[3,2],'A specified amount can span both hands');
}
{
  const world=createFarm(),actor=world.workers[0];Object.assign(actor,{x:4,y:8});
  dropItem(world,actor,'fertilizer',3);world.compost.ready=2;
  perform(world,actor,collect('fertilizer',{target:targetId('compost')}));
  assert.equal(actor.inventory[0].count,2);
  assert.equal(world.tiles[8][4].items[0].count,3,'An explicit source excludes other nearby items');
  actor.inventory=[];
  perform(world,actor,collect('fertilizer',{target:targetId('compost')}));
  assert.deepEqual(actor.inventory,[],'An empty explicit source does not switch to nearby items');
  perform(world,actor,collect('fertilizer',{target:targetId('barn')}));
  assert.deepEqual(actor.inventory,[],'An out-of-reach explicit source does not switch to nearby items');
  actor.subject={kind:'target',value:{kind:'compost'}};world.compost.ready=1;
  perform(world,actor,collect('fertilizer',{target:SUBJECT_TARGET}));
  assert.equal(actor.inventory[0].count,1,'A remembered source works with from it');
}
{
  const world=createFarm(),actor=world.workers[0];Object.assign(actor,{x:8,y:11});
  world.cow={id:'cow',x:8,y:10,direction:0,feed:0,progress:0,produce:3};
  perform(world,actor,collect('milk',{target:targetId('cow')}));
  assert.equal(actor.inventory[0].count,3);assert.equal(world.cow.produce,0);
  actor.inventory=[];world.cow.produce=2;
  perform(world,actor,collect('milk'));
  assert.equal(actor.inventory[0].count,2);assert.equal(world.cow.produce,0);
  actor.inventory=[];Object.assign(actor,{x:10,y:7});
  dropItem(world,{x:11,y:7},'eggs',2);
  perform(world,actor,collect('eggs'));
  assert.deepEqual(actor.inventory,[],'Fences block collection');
  Object.assign(actor,{x:12,y:9});dropItem(world,{x:12,y:8},'eggs',2);
  perform(world,actor,collect('eggs'));
  assert.equal(actor.inventory[0].count,2,'Open gates allow collection');
  assert.equal(world.coding.eggs.count,2);
  actor.inventory=[];Object.assign(actor,{x:3,y:14});world.stock.eggs=5;
  perform(world,actor,collect('eggs'));
  assert.equal(world.coding.eggs.count,2,'Withdrawing barn eggs does not earn the egg collection goal');
}
{
  const world=createFarm(),actor=world.workers[0],other=createWorker('worker-2','Bo',8,10);
  world.workers.push(other);Object.assign(actor,{x:8,y:11});addItem(other,'bucket');other.inventory[0].water=3;
  perform(world,actor,collect('bucket'));
  assert.deepEqual(actor.inventory,[],'Unqualified collection leaves other workers holding their equipment');
  perform(world,actor,collect('bucket',{target:targetId('worker'),amount:1}));
  assert.equal(actor.inventory[0].water,3);assert.deepEqual(other.inventory,[]);
  perform(world,actor,action('drop'));
  perform(world,actor,collect('bucket'));
  assert.equal(actor.inventory[0].water,3,'Ground collection also preserves bucket water');
  addItem(actor,'axe');dropItem(world,actor,'logs',2);
  perform(world,actor,action('collect',{argument:ALL_ITEMS}));
  assert.equal(actor.inventory.length,2);assert.equal(world.tiles[11][8].items[0].count,2,'Full hands leave items at their source');
}
{
  const world=createFarm(),actor=world.player;addItem(actor,'wheat',3);
  const inventory=structuredClone(actor.inventory);
  perform(world,actor,collect('wheat',{target:targetId('player')}));
  assert.deepEqual(actor.inventory,inventory,'Collecting from oneself leaves inventory unchanged');
}
console.log('Collection aliases share sources, quantities, capacity, subject references, fence rules and egg-goal accounting.');
