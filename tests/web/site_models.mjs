// Run the model and library regressions through the compiler shipped to browsers.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {execFileSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'../..');
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-site-models-'));
const cases=['json_edit','json_text_buffers','json_float_roundtrip','string_integer_width','site_account_model'];
try {
  const files=cases.map(name=>`tests/integration/${name}/main.dl`);
  for(const folder of ['site/games/progress','site/account'])
    for(const name of await fs.readdir(path.join(root,folder))) if(name.endsWith('.dl')) files.push(`${folder}/${name}`);
  const sources=Object.fromEntries(await Promise.all(files.map(async name=>[name,await fs.readFile(path.join(root,name),'utf8')])));
  const request=path.join(directory,'project.json');
  await fs.writeFile(request,JSON.stringify({sources,jobs:cases.map(name=>({entry:`tests/integration/${name}/main.dl`,kind:'wasm',output:name+'.wasm'}))}));
  execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:'inherit',timeout:180000});
  for(const name of cases) {
    const output=execFileSync(process.execPath,[path.join(root,'scripts/run_program.mjs'),path.join(directory,name+'.wasm')],{cwd:root,encoding:'utf8',timeout:30000});
    assert.equal(output,await fs.readFile(path.join(root,`tests/integration/${name}/expected.txt`),'utf8'));
    console.log('PASS shipped compiler:',name);
  }
  execFileSync(process.execPath,[path.join(root,'tests/web/json_memory.mjs'),path.join(directory,'json_text_buffers.wasm')],{cwd:root,stdio:'inherit',timeout:30000});
} finally {await fs.rm(directory,{recursive:true,force:true});}
