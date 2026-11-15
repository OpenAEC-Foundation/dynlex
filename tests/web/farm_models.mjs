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
  {name:'guide',entry:'tests/integration/site_farm_guide/main.dl',expected:'tests/integration/site_farm_guide/expected.txt'},
  {name:'plans',entry:'tests/web/farm_plan_model.dl',fixture:'farm-plans.json'},
  {name:'routines',entry:'tests/web/farm_routine_model.dl',fixture:'farm-routines.json'},
  {name:'world',entry:'tests/web/farm_world_model.dl',fixture:'farm-world-cases.json'},
  {name:'actions',entry:'tests/web/farm_world_model.dl',fixture:'farm-actions.json'},
  {name:'simulation',entry:'tests/web/farm_world_model.dl',fixture:'farm-simulation.json'},
  {name:'goals',entry:'tests/integration/site_coding_goals/main.dl',expected:'tests/integration/site_coding_goals/expected.txt'},
  {name:'roles',entry:'tests/integration/site_farm_roles/main.dl',expected:'tests/integration/site_farm_roles/expected.txt'},
  {name:'saves',group:'state',entry:'tests/web/farm_save_model.dl',fixture:'farm-saves.json'},
  {name:'resume',group:'state',entry:'tests/web/farm_save_model.dl',fixture:'farm-resume.json'},
  {name:'documents',group:'ownership',entry:'tests/integration/site_farm_document/main.dl',expected:'tests/integration/site_farm_document/expected.txt'},
  {name:'preparation',group:'ownership',entry:'tests/integration/site_farm_preparation/main.dl',expected:'tests/integration/site_farm_preparation/expected.txt'},
];
const selected=process.argv.find(argument=>argument.startsWith('--model='))?.slice(8);
const group=process.argv.find(argument=>argument.startsWith('--group='))?.slice(8);
if(group) {
  assert(!selected,'Choose either --group or --model.');
  assert(['core','state','ownership'].includes(group),`Unknown Farm test group ${group}`);
  for(let index=models.length-1;index>=0;index--) if((models[index].group??'core')!==group) models.splice(index,1);
}
if(selected) {
  assert(models.some(model=>model.name===selected),`Unknown Farm model ${selected}`);
  for(let index=models.length-1;index>=0;index--) if(models[index].name!==selected) models.splice(index,1);
}
function check(model,output,target) {
  if(model.expected) {
    assert.equal(output,model.data.expected,`${model.name}: ${target}`);
    console.log(`PASS Farm ${model.name} ${target}`);return;
  }
  const actual=JSON.parse(output);
  const expected=model.name==='catalog'?model.data:model.data.expected;
  if(['world','actions','simulation'].includes(model.name)) compareWorld(actual,expected,`${model.name}: ${target}`);
  else if(model.data.cases) {
    assert.equal(actual.length,expected.length,`${model.name}: ${target}: case count`);
    expected.forEach((value,index)=>assert.deepEqual(actual[index],value,`${model.name}: ${target}: case ${index} (${model.data.cases[index].name??'captured state'})`));
  } else assert.deepEqual(actual,expected,`${model.name}: ${target}`);
  console.log(`PASS Farm ${model.name} ${target}${model.data.cases?`: ${model.data.cases.length} scenarios`:''}`);
}

// Native libm and the browser's Math implementation can differ by an ulp.
// Only initial terrain heights use that tolerance; every other value is exact.
function compareWorld(actual,expected,location) {
  if(location.endsWith('.height')) {
    assert(Math.abs(actual-expected)<1e-14,`${location}: ${actual} != ${expected}`);
  } else if(Array.isArray(expected)) {
    assert(Array.isArray(actual),location);
    assert.equal(actual.length,expected.length,location+'.length');
    expected.forEach((value,index)=>compareWorld(actual[index],value,location+'['+index+']'));
  } else if(expected!==null&&typeof expected==='object') {
    assert.deepEqual(Object.keys(actual).sort(),Object.keys(expected).sort(),location+'.keys');
    for(const [key,value] of Object.entries(expected)) compareWorld(actual[key],value,location+'.'+key);
  } else assert.equal(actual,expected,location);
}

async function checkWasm(model,file,target) {
  const bytes=await fs.readFile(file);
  const filesystem=createRuntimeFilesystem();
  filesystem.files.set('input.farm',{data:new TextEncoder().encode(modelInput(model)),modificationTime:filesystem.lastTimestamp});
  let output='';
  const runtime=buildRuntimeImports({push:chunk=>{output+=chunk;}},filesystem,inspectRuntimeWasmLayout(bytes));
  const allocation=observeAllocations(runtime,64*1024*1024);
  const {instance}=await WebAssembly.instantiate(bytes,runtime);
  const started=performance.now();
  instance.exports.main();
  check(model,output,target);
  assert.equal(allocation.live,0,`${model.name} left owned allocations`);
  console.log(`Farm ${model.name}: ${allocation.peak} peak live bytes, every allocation released, ${Math.round(performance.now()-started)} ms.`);
}

function modelInput(model) {
  const cases=model.data.cases??[];
  // The save corpus contains hundreds of full worlds. Decode one saved
  // document at a time, as the application does, rather than retaining all
  // typed JSON trees merely to transport independent test cases.
  return JSON.stringify(['saves','resume'].includes(model.name)?cases.map(value=>JSON.stringify(value)):cases);
}

async function compilationBudget(entry) {
  const directory=path.dirname(path.join(root,entry));
  const names=await fs.readdir(directory);
  if(!names.includes('compile_timeout_seconds.txt')) return 60000;
  const seconds=(await fs.readFile(path.join(directory,'compile_timeout_seconds.txt'),'utf8')).trim();
  assert(/^[1-9]\d{0,2}$/.test(seconds) && Number(seconds)<=300,
    `${entry}: invalid compile_timeout_seconds.txt`);
  return Number(seconds)*1000;
}
try {
  const nativePrograms=new Map(),wasmPrograms=new Map();
  for(const model of models) {
    const timeout=await compilationBudget(model.entry);
    model.data=model.expected?{expected:await fs.readFile(path.join(root,model.expected),'utf8')}
      :JSON.parse(await fs.readFile(path.join(root,'tests/web/fixtures',model.fixture)));
    const input=modelInput(model);
    await fs.writeFile(path.join(directory,'input.farm'),input);
    for(const optimization of ['O0','O2']) {
      const key=model.entry+':'+optimization;
      if(!nativePrograms.has(key)) {
        const binary=path.join(directory,`${model.name}-${optimization}.out`);
        execFileSync(compiler,[model.entry,`-${optimization}`,'-o',binary],{cwd:root,stdio:'inherit',timeout});
        nativePrograms.set(key,binary);
      }
      const binary=nativePrograms.get(key);
      const outputBudget=Math.max(8*1024*1024,2*Buffer.byteLength(JSON.stringify(model.data.expected??model.data)));
      check(model,execFileSync(binary,{cwd:directory,encoding:'utf8',maxBuffer:outputBudget,timeout:30000}),optimization);
    }
    if(process.argv.includes('--native-wasm')) {
      if(!wasmPrograms.has(model.entry)) {
        const wasm=path.join(directory,model.name+'.wasm');
        execFileSync(compiler,[model.entry,'--emit-wasm','-O2','-o',wasm],{cwd:root,stdio:'inherit',timeout});
        wasmPrograms.set(model.entry,wasm);
      }
      const wasm=wasmPrograms.get(model.entry);
      await checkWasm(model,wasm,'native compiler/Wasm');
    }
  }
  if(process.argv.includes('--shipped')) {
    const sources={};
    for(const folder of ['site/games/farm','site/games/progress','site/games'])
      for(const name of await fs.readdir(path.join(root,folder))) if(name.endsWith('.dl')) sources[`${folder}/${name}`]=await fs.readFile(path.join(root,folder,name),'utf8');
    for(const name of new Set([...models.map(model=>model.entry),'tests/web/farm_world_test.dl','tests/web/farm_test_json.dl']))
      sources[name]=await fs.readFile(path.join(root,name),'utf8');
    const request=path.join(directory,'request.json');
    const outputs=new Map(),jobs=[];
    for(const model of models) if(!outputs.has(model.entry)) {
      const output=model.name+'.wasm';outputs.set(model.entry,output);
      jobs.push({entry:model.entry,kind:'wasm',output});
    }
    await fs.writeFile(request,JSON.stringify({sources,jobs}));
    execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:'inherit',timeout:120000*jobs.length});
    for(const model of models) {
      await checkWasm(model,path.join(directory,outputs.get(model.entry)),'shipped compiler/Wasm');
    }
  }
} finally {await fs.rm(directory,{recursive:true,force:true});}
