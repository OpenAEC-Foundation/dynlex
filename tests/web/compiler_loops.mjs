// Compiler regressions remain import-free on every execution target.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {execFileSync} from 'node:child_process';
import {nativeText} from './native_text.mjs';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
const root=path.resolve(import.meta.dirname,'../..');
const compiler=path.resolve(process.argv[2]??path.join(root,'build/dynlex'));
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-loop-cleanup-'));
const names=['managed_subject_early_return','guaranteed_loop_early_return'];
const expected=new Map();
async function checkWasm(file,name,target) {
  const bytes=await fs.readFile(file);
  let output='';
  const runtime=buildRuntimeImports({push:chunk=>{output+=chunk;}},createRuntimeFilesystem(),inspectRuntimeWasmLayout(bytes));
  const {instance}=await WebAssembly.instantiate(bytes,runtime);instance.exports.main();
  assert.equal(output,expected.get(name),`${name}: ${target}`);
  console.log(`PASS ${name}: ${target}`);
}
try {
  const sources={};
  for(const name of names) {
    const entry=`tests/required/${name}/main.dl`;
    sources[entry]=await fs.readFile(path.join(root,entry),'utf8');
    expected.set(name,await fs.readFile(path.join(root,`tests/required/${name}/expected.txt`),'utf8'));
    for(const optimization of ['O0','O2']) {
      const binary=path.join(directory,name+'-'+optimization+'.out');
      execFileSync(compiler,[entry,`-${optimization}`,'-o',binary],{cwd:root,stdio:'inherit',timeout:30000});
      assert.equal(nativeText(execFileSync(binary,{encoding:'utf8'})),expected.get(name),`${name}: ${optimization}`);
      console.log(`PASS ${name}: ${optimization}`);
    }
    const wasm=path.join(directory,name+'.wasm');
    execFileSync(compiler,[entry,'--emit-wasm','-O2','-o',wasm],{cwd:root,stdio:'inherit',timeout:30000});
    await checkWasm(wasm,name,'native compiler/Wasm');
  }
  if(process.argv.includes('--shipped')) {
    const request=path.join(directory,'request.json');
    await fs.writeFile(request,JSON.stringify({sources,jobs:names.map(name=>({entry:`tests/required/${name}/main.dl`,kind:'wasm',output:name+'.wasm'}))}));
    execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:'inherit',timeout:60000});
    for(const name of names) await checkWasm(path.join(directory,name+'.wasm'),name,'shipped compiler/Wasm');
  }
} finally {await fs.rm(directory,{recursive:true,force:true});}
