// Independent snapshots captured before retiring the JavaScript progress backend.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';

const root=path.resolve(import.meta.dirname,'../..');
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-progress-'));
const {cases,expected}=JSON.parse(await fs.readFile(new URL('./fixtures/progress-model.json',import.meta.url),'utf8'));
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
