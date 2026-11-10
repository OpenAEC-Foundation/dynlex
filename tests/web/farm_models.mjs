// Independent snapshots captured before replacing the JavaScript Farm backend.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {execFileSync} from 'node:child_process';
import {observeAllocations} from './allocation_probe.mjs';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
const root=path.resolve(import.meta.dirname,'../..');
const compiler=path.resolve(process.argv[2]??path.join(root,'build/dynlex'));
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-farm-models-'));
const models=[
  {name:'catalog',entry:'site/games/farm/export_vocabulary.dl',fixture:'farm-vocabulary.json'},
  {name:'plans',entry:'tests/web/farm_plan_model.dl',fixture:'farm-plans.json'},
  {name:'routines',entry:'tests/web/farm_routine_model.dl',fixture:'farm-routines.json'},
];
function check(model,output,target) {
  const actual=JSON.parse(output);
  const expected=model.name==='catalog'?model.data:model.data.expected;
  assert.deepEqual(actual,expected,`${model.name}: ${target}`);
  console.log(`PASS Farm ${model.name} ${target}${model.data.cases?`: ${model.data.cases.length} scenarios`:''}`);
}

async function checkWasm(model,file,target) {
  const bytes=await fs.readFile(file);
  const filesystem=createRuntimeFilesystem();
  filesystem.files.set('input.farm',{data:new TextEncoder().encode(JSON.stringify(model.data.cases??[])),modificationTime:filesystem.lastTimestamp});
  let output='';
  const runtime=buildRuntimeImports({push:chunk=>{output+=chunk;}},filesystem,inspectRuntimeWasmLayout(bytes));
  const allocation=observeAllocations(runtime,64*1024*1024);
  const {instance}=await WebAssembly.instantiate(bytes,runtime);
  instance.exports.main();
  check(model,output,target);
  assert.equal(allocation.live,0,`${model.name} left owned allocations`);
  console.log(`Farm ${model.name}: ${allocation.peak} peak live bytes, every allocation released.`);
}
try {
  for(const model of models) {
    model.data=JSON.parse(await fs.readFile(path.join(root,'tests/web/fixtures',model.fixture)));
    const input=JSON.stringify(model.data.cases??[]);
    await fs.writeFile(path.join(directory,'input.farm'),input);
    for(const optimization of ['O0','O2']) {
      const binary=path.join(directory,`${model.name}-${optimization}.out`);
      execFileSync(compiler,[model.entry,`-${optimization}`,'-o',binary],{cwd:root,stdio:'inherit',timeout:60000});
      check(model,execFileSync(binary,{cwd:directory,encoding:'utf8',maxBuffer:8*1024*1024,timeout:30000}),optimization);
    }
    if(process.argv.includes('--native-wasm')) {
      const wasm=path.join(directory,model.name+'.wasm');
      execFileSync(compiler,[model.entry,'--emit-wasm','-O2','-o',wasm],{cwd:root,stdio:'inherit',timeout:60000});
      await checkWasm(model,wasm,'native compiler/Wasm');
    }
  }
  if(process.argv.includes('--shipped')) {
    const sources={};
    for(const folder of ['site/games/farm','site/games'])
      for(const name of await fs.readdir(path.join(root,folder))) if(name.endsWith('.dl')) sources[`${folder}/${name}`]=await fs.readFile(path.join(root,folder,name),'utf8');
    for(const name of ['tests/web/farm_plan_model.dl','tests/web/farm_routine_model.dl','tests/web/farm_test_json.dl','lib/text.dl'])
      sources[name]=await fs.readFile(path.join(root,name),'utf8');
    const request=path.join(directory,'request.json');
    await fs.writeFile(request,JSON.stringify({sources,jobs:models.map(model=>({entry:model.entry,kind:'wasm',output:model.name+'.wasm'}))}));
    execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:'inherit',timeout:120000});
    for(const model of models) {
      await checkWasm(model,path.join(directory,model.name+'.wasm'),'shipped compiler/Wasm');
    }
  }
} finally {await fs.rm(directory,{recursive:true,force:true});}
