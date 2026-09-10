import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {mkdtempSync,writeFileSync,rmSync} from "node:fs";
import {tmpdir} from "node:os";
import path from "node:path";
import {createFarm,dropItem} from "../../web/farm-world.js";
import {createRoles} from "../../web/farm-roles.js";
import {createProgressStore,readProgress,SAVE_VERSION} from "../../web/game-progress.js";
import {upgradeFarmSource} from "../../web/farm-language-upgrade.js";
import {parseFarmPlan,FARM_PREFIX} from "../../web/farm-program.js";
import {targetId,itemId} from "../../web/farm-vocabulary.js";

const world=createFarm(), roles=createRoles(), actor=world.workers[0];
const oldSource=`# Keep "chop it down" in this comment.
loop forever:
    walk to the barn
    store everything
    grab an axe
    walk to the nearest tree
    if i can reach it:
        chop it down
        pick up nearby items`;
Object.assign(roles[0],{source:oldSource+'\n# unapplied changes',applied:oldSource,needsSourceMap:false,program:[
  {op:'loop',count:-1,end:9},
  {op:'action',key:'walk',argument:0,area:''},
  {op:'action',key:'store',argument:0},
  {op:'action',key:'grab',argument:0},
  {op:'action',key:'walk',argument:5,area:''},
  {op:'test',key:'reachTree',argument:0,amount:0,to:8},
  {op:'action',key:'chop',argument:0},
  {op:'action',key:'pickup',argument:0},
  {op:'next',to:0}
]});
roles.push({id:'composter',name:'Composter',source:'take finished compost',applied:'take finished compost',program:[{op:'action',key:'takeCompost',argument:0}],generation:1,needsSourceMap:false});
actor.role=roles[0].id;actor.routine.pc=6;actor.routine.loops={0:-1};
dropItem(world,actor,'eggs',2);
const data={world,roles,selected:actor.id,roleId:roles[0].id,speed:2};
const before={version:5,games:{farm:{updatedAt:100,data},river:{updatedAt:50,data:{source:'take the sheep',solved:true}}},lastGame:'farm'};
for(const who of [world.player,...world.workers]) {delete who.subject;delete who.routine.continuation;}
for(const tile of world.tiles.flat()) for(const entity of [tile.tree,tile.crop,...tile.items]) if(entity!==null) delete entity.serial;
for(const flag of world.flags) delete flag.serial;
delete world.nextEntity;
for(const role of roles) delete role.needsRebuild;
const encoded=JSON.stringify(before), memory=new Map(['guest','user:alice','backup:alice'].map(id=>['dynlex.games.'+id,encoded]));
const storage={get length(){return memory.size;},key:i=>[...memory.keys()][i],getItem:key=>memory.get(key)??null,setItem:(key,value)=>memory.set(key,value)};
createProgressStore(storage,error=>{throw error;});
const remote=readProgress(encoded);
for(const text of [...memory.values(),JSON.stringify(remote)]) {
  const save=JSON.parse(text), upgraded=save.games.farm.data;
  assert.equal(save.version,SAVE_VERSION);
  assert.deepEqual(save.games.river,before.games.river);
  assert.equal(upgraded.world.workers[0].routine.pc,6);
  assert.deepEqual(upgraded.world.workers[0].routine.loops,{0:-1});
  assert.equal(upgraded.world.workers[0].subject,null);
  assert.equal(upgraded.world.workers[0].routine.continuation,null);
  assert.ok(upgraded.roles[0].source.endsWith('# unapplied changes'));
  assert.ok(!upgraded.roles[0].applied.endsWith('# unapplied changes'));
  assert.ok(upgraded.roles.every(role=>role.needsRebuild && !('needsSourceMap' in role)));
  assert.equal(upgraded.roles[0].program[4].target,targetId('tree'));
  assert.equal(upgraded.roles[0].program[5].target,targetId('frontTree'));
  assert.equal(upgraded.roles[0].program[6].target,targetId('frontTree'));
  assert.equal(upgraded.roles[1].program[0].argument,itemId('fertilizer'));
  assert.equal(upgraded.roles[1].program[0].target,targetId('compost'));
  const entities=[...upgraded.world.tiles.flat().flatMap(tile=>[tile.tree,tile.crop,...tile.items]),...upgraded.world.flags].filter(entity=>entity!==null);
  assert.equal(new Set(entities.map(entity=>entity.serial)).size,entities.length);
  assert.ok(entities.every(entity=>Number.isInteger(entity.serial) && entity.serial<upgraded.world.nextEntity));
  assert.equal(JSON.stringify(readProgress(text)),text,'Current saves need no runtime compatibility conversion');
}
assert.equal(upgradeFarmSource('walk to the nearest tree in the area "chop it down" # i can reach it'),'walk to the nearest tree in the area "chop it down" # i can reach it');
const directory=mkdtempSync(path.join(tmpdir(),'dynlex-farm-upgrade-'));
try {
  for(const role of remote.games.farm.data.roles) {
    const file=path.join(directory,role.id+'.dl'), binary=path.join(directory,role.id+'.out');
    writeFileSync(file,FARM_PREFIX+role.applied);
    execFileSync(path.resolve('build/dynlex'),[file,'--trace-execution','-o',binary]);
    const plan=parseFarmPlan(execFileSync(binary,{encoding:'utf8'}));
    const structure=code=>code.map(({op,to,end,count})=>({op,to,end,count}));
    assert.deepEqual(structure(plan),structure(role.program),'Rebuilding retains the saved instruction and loop positions');
    assert.ok(plan.every(instruction=>instruction.range!==null));
  }
} finally {rmSync(directory,{recursive:true});}
console.log('All guest, account, backup and cloud saves migrate sources, plans, subject state and entity identities without changing control flow.');
