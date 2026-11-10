// The replaced implementation is an independent oracle until migration cleanup.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {readProgress,mergeProgress,emptyProgress} from '../../web/game-progress.js';
import {upgradeFarmSource} from '../../web/farm-language-upgrade.js';
import {createFarm} from '../../web/farm-world.js';
import {createRoles} from '../../web/farm-roles.js';
import {newGoalProgress,RIVER_GOALS} from '../../web/coding-goals.js';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';

const root=path.resolve(import.meta.dirname,'../..');
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-progress-'));
const cases=[],expected=[];
const add=(request,result)=>{cases.push(request);expected.push(result);};
const actions=['forward','backward','left','right','chop','plow','carrots','wheat','water','harvest','fill','store','pickup','dig','feedCow','feedChickens','milk','eggs','compost','takeCompost','fertilize','wait','grab','give','walk','drop'];
const conditions=['treeAhead','reachTree','full','empty','rain','dry','ripe','bare','unplowed','bucketEmpty','thirsty','items','compostReady','cowHungry','chickensHungry','carrying','notCarrying','role','canStep','barnAtLeast','barnFewer'];
const oldSource='# chop it down\r\nif i can reach it:\n    chop it down\n    take finished compost\nprint "i can reach it"\n';
const world=createFarm(),roles=createRoles();
roles[0].source=oldSource+'# unapplied';roles[0].applied=oldSource;
roles[0].program=[{op:'loop',count:3,end:50,extra:'keep'},
  ...actions.map(key=>({op:'action',key,argument:7,amount:9,area:'North field',range:{old:true},extra:'drop'})),
  ...conditions.map(key=>({op:'test',key,argument:6,amount:4,to:49,range:{old:true}})),
  {op:'next',to:0,range:{old:true}}];
roles.push({id:'custom',name:'Custom',source:'',applied:'',program:null,generation:0});
world.player.routine.pc=6;world.player.routine.loops={0:3};
const base={version:5,games:{farm:{updatedAt:1794279584001,data:{world,roles,selected:'player',roleId:'lumberjack',speed:2}},
  river:{updatedAt:1794279584000,data:{source:'take the sheep',solved:true,coding:newGoalProgress(RIVER_GOALS)}}},lastGame:'farm'};
for(let version=1;version<=7;version++) {
  let save=structuredClone(base);save.version=version;
  if(version<=4) {
    for(const role of save.games.farm.data.roles) for(const instruction of role.program??[]) {
      delete instruction.amount;delete instruction.area;
    }
    delete save.games.farm.data.world.areas;delete save.games.farm.data.world.coding;
    delete save.games.river.data.coding;
  }
  if(version===1) for(const chicken of save.games.farm.data.world.chickens) delete chicken.held;
  if(version<=2) for(const actor of [save.games.farm.data.world.player,...save.games.farm.data.world.workers]) delete actor.paused;
  if(version>=6) {
    save=readProgress(JSON.stringify(base));save.version=version;
    if(version===6) for(const role of save.games.farm.data.roles) for(const instruction of role.program??[])
      if(instruction.op==='action'&&instruction.key==='collect') instruction.key='take';
  }
  const text=JSON.stringify(save),result=readProgress(text);
  add({op:'read',text},result);
  add({op:'read',text:JSON.stringify(result)},result);
  const riverOnly={version,games:{river:structuredClone(save.games.river)},lastGame:'river'};
  add({op:'read',text:JSON.stringify(riverOnly)},readProgress(JSON.stringify(riverOnly)));
  const empty={version,games:{},lastGame:null};add({op:'read',text:JSON.stringify(empty)},readProgress(JSON.stringify(empty)));
}
for(const prefix of ['', 'x','_', '1','é','😀',' ','"','#','\n']) for(const suffix of ['', 'x','_', '1','é','😀',' ','"','#','\r\n']) {
  const source=prefix+'i can reach it / chop it down / take finished compost'+suffix;
  add({op:'source',source},{source:upgradeFarmSource(source)});
}
for(const source of ['"unclosed chop it down','"escaped \\" chop it down" # i can reach it','"\\\" i can reach it" chop it down',
  '"chop it down" "i can reach it" take finished compost','chop it down\r# take finished compost\ni can reach it',
  ('chop it down\n'.repeat(5000))]) add({op:'source',source},{source:upgradeFarmSource(source)});
const game=(updatedAt,source)=>({updatedAt,data:{source,solved:false,coding:newGoalProgress(RIVER_GOALS)}});
for(const localTime of [null,0,1794279584001,1794279584003]) for(const remoteTime of [null,0,1794279584001,1794279584004]) {
  const local=emptyProgress(),remote=emptyProgress(),guest=emptyProgress();
  if(localTime!==null) local.games.river=game(localTime,'local');
  if(remoteTime!==null) remote.games.river=game(remoteTime,'remote');
  remote.games.farm={updatedAt:1794279584002,data:{keep:'raw farm state'}};
  guest.games.river=game(1794279584999,'guest');guest.games.farm={updatedAt:1794279584999,data:{unrelated:true}};
  add({op:'merge',local,remote},mergeProgress(local,remote));
  const merged=mergeProgress(local,remote);
  for(const [key,value] of Object.entries(guest.games)) if(!merged.games[key]) merged.games[key]=value;
  add({op:'import',local,remote,guest},mergeProgress(merged,emptyProgress()));
}
for(const now of [100,1794279584000,1794279584001,1794279584005]) {
  const current={version:7,games:{river:game(1794279584001,'before')},lastGame:'river'};
  for(const source of ['before','after']) {
    const data=game(0,source).data;
    const next=structuredClone(current);
    if(source!=='before') next.games.river={updatedAt:Math.max(now,current.games.river.updatedAt+1),data};
    add({op:'save',game:'river',now,current,data},next);
  }
}
for(const lastGame of [null,'farm']) {
  const current={version:7,games:{river:game(1794279584001,'before')},lastGame};
  const next=structuredClone(current);next.lastGame='river';next.games.river.updatedAt++;
  add({op:'save',game:'river',now:100,current,data:current.games.river.data},next);
}
for(const now of [-100,0,1,1794279584123]) {
  const data=game(0,'first save').data;
  add({op:'save',game:'river',now,current:emptyProgress(),data},
    {version:7,games:{river:{updatedAt:Math.max(now,1),data}},lastGame:'river'});
}
const invalid=['{broken','null','[]','{}','{"version":999}','{"version":4294967303,"games":{},"lastGame":null}',
  '{"version":7,"games":{},"lastGame":"unknown"}',
  '{"version":7,"games":{"river":{"updatedAt":1.5,"data":{}}},"lastGame":null}'];
for(const change of [save=>save.games.farm.data.roles[0].program[1].key='unknown',save=>save.games.farm.data.world.tiles[0][0].items=null,
  save=>save.games.farm.data.world.player.routine=null,save=>save.games.farm.data.roles[0].program[1].op=null,
  save=>save.games.farm.data.roles[0].program[27].to='invalid']) {
  const save=structuredClone(base);change(save);invalid.push(JSON.stringify(save));
}
for(const text of invalid) add({op:'read',text},{invalid:true});
function check(actual,kind) {
  assert.equal(actual.length,expected.length);
  for(let i=0;i<actual.length;i++) {
    if(expected[i].invalid) assert.equal(typeof actual[i].error,'string',`${kind}: malformed save ${i} was accepted`);
    else assert.deepEqual(actual[i],expected[i],`${kind}: case ${i} (${cases[i].op}) differs from the replaced behavior`);
  }
  console.log(`${kind}: ${cases.length} progress, migration, source, timestamp and profile cases passed.`);
}
try {
  const text=JSON.stringify(cases);await fs.writeFile(path.join(directory,'input.progress'),text);
  for(const optimization of ['-O0','-O2']) {
    const binary=path.join(directory,`progress${optimization}.out`);
    execFileSync(path.join(root,'build/dynlex'),['tests/web/progress_model.dl',optimization,'-o',binary],{cwd:root,stdio:['ignore','pipe','pipe'],timeout:180000});
    check(JSON.parse(execFileSync(binary,[],{cwd:directory,encoding:'utf8',maxBuffer:16e6,timeout:30000})),`Native ${optimization}`);
  }
  const names=['tests/web/progress_model.dl',...(await fs.readdir(path.join(root,'site/games/progress'))).filter(name=>name.endsWith('.dl')).map(name=>'site/games/progress/'+name)];
  const sources=Object.fromEntries(await Promise.all(names.map(async name=>[name,await fs.readFile(path.join(root,name),'utf8')])));
  const request=path.join(directory,'project.json');
  await fs.writeFile(request,JSON.stringify({sources,jobs:[{entry:'tests/web/progress_model.dl',kind:'wasm',output:'progress.wasm'}]}));
  execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:['ignore','pipe','pipe'],timeout:180000});
  const bytes=await fs.readFile(path.join(directory,'progress.wasm')),filesystem=createRuntimeFilesystem();
  filesystem.files.set('input.progress',{data:new TextEncoder().encode(text),modificationTime:filesystem.lastTimestamp});
  let stdout='';
  const runtime=buildRuntimeImports({push:chunk=>{stdout+=chunk;}},filesystem,inspectRuntimeWasmLayout(bytes));
  const {instance}=await WebAssembly.instantiate(bytes,runtime);instance.exports.main();
  check(JSON.parse(stdout),'Shipped browser compiler');
} finally {await fs.rm(directory,{recursive:true,force:true});}
