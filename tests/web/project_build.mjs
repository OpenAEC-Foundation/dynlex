// Build source snapshots from another working directory through both frontends.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {execFileSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'../..');
const native=path.resolve(process.argv[2]??'build/dynlex');
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-project-build-'));
try {
  const request=path.join(directory,'project.json');
  await fs.writeFile(request,JSON.stringify({sources:{
    'project/main.dl':'import lib/std.dl\nimport helpers.dl\nprint line doubled 21\n',
    'project/helpers.dl':'doubled value means: @intrinsic("multiply", value, 2)\n'
  },jobs:[{entry:'project/main.dl',kind:'wasm',output:'program.wasm'},
    {entry:'project/main.dl',kind:'wasm',traced:true,output:'traced.wasm'}]}));
  for(const frontend of ['browser','native']) {
    const env={...process.env};delete env.DYNLEX_PROJECT_COMPILER;
    if(frontend==='native') env.DYNLEX_PROJECT_COMPILER=native;
    execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:directory,env,stdio:'inherit'});
    for(const file of ['program','traced']) {
      const wasm=path.join(directory,file+'.wasm');
      assert(WebAssembly.validate(await fs.readFile(wasm)));
      const output=execFileSync(process.execPath,[path.join(root,'scripts/run_program.mjs'),wasm],{cwd:directory,encoding:'utf8'});
      if(file==='program') assert.equal(output,'42\n');
      else {assert(output.includes('DYNLEX|TRACE|'));assert(output.includes('42\n'));}
    }
  }
} finally {await fs.rm(directory,{recursive:true,force:true});}
console.log('Relocated project build: both frontends, standard library, nested imports and tracing passed.');
