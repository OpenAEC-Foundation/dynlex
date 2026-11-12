// Verify owning values and streaming text through both code generators.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
import {observeAllocations} from './allocation_probe.mjs';
const root=path.resolve(import.meta.dirname,'../..');
const compiler=path.resolve(process.argv[2]??path.join(root,'build/dynlex'));
const fixtures=['sequence_values','unicode_stream','workspace_edit_text','workspace_text_plan','unicode_properties','text_search'];
assert(process.argv.slice(3).every(argument=>argument==='--shipped' || argument.startsWith('--case=')),'Unknown library fixture option');
const selected=process.argv.filter(argument=>argument.startsWith('--case=')).map(argument=>argument.slice(7));
assert(selected.every(name=>fixtures.includes(name)),'Unknown library fixture');
const cases=selected.length?selected:fixtures;
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-library-values-'));
const expected=new Map();
async function runWasm(name,target) {
  const bytes=await fs.readFile(path.join(directory,name+'.wasm'));
  let output='';
  const runtime=buildRuntimeImports({push:chunk=>{output+=chunk;}},createRuntimeFilesystem(),inspectRuntimeWasmLayout(bytes));
  const allocation=observeAllocations(runtime,1024*1024);
  const {instance}=await WebAssembly.instantiate(bytes,runtime);instance.exports.main();
  assert.equal(output,expected.get(name),`${name}: ${target}`);
  assert.equal(allocation.live,0,`${name} left owned values allocated`);
  console.log(`PASS ${name}: ${target}, ${allocation.peak} peak live bytes, zero retained`);
}
try {
  for(const name of cases) {
    const entry=`tests/integration/${name}/main.dl`;
    const source=await fs.readFile(path.join(root,entry),'utf8');
    expected.set(name,await fs.readFile(path.join(root,`tests/integration/${name}/expected.txt`),'utf8'));
    for(const optimization of ['O0','O2']) {
      const binary=path.join(directory,name+'.out');
      execFileSync(compiler,[entry,'-'+optimization,'-o',binary],{cwd:root,stdio:'inherit',timeout:30000});
      assert.equal(execFileSync(binary,{encoding:'utf8'}),expected.get(name),`${name}: ${optimization}`);
      console.log(`PASS ${name}: ${optimization}`);
    }
    execFileSync(compiler,[entry,'--emit-wasm','-O2','-o',path.join(directory,name+'.wasm')],{cwd:root,stdio:'inherit',timeout:30000});
    await runWasm(name,'native compiler/Wasm');
    if(process.argv.includes('--shipped')) {
      const request=path.join(directory,'request.json');
      await fs.writeFile(request,JSON.stringify({sources:{[entry]:source},jobs:[{entry,kind:'wasm',output:name+'.wasm'}]}));
      execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:'inherit',timeout:30000});
      await runWasm(name,'shipped compiler/Wasm');
    }
  }
} finally {await fs.rm(directory,{recursive:true,force:true});}
