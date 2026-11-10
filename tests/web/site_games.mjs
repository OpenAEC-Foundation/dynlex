// Exercise the real shipped compiler, game vocabulary and DynLex replay model.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const root=path.resolve(import.meta.dirname,'../..');
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-site-games-'));
const run=(file,args=[])=>execFileSync(process.execPath,[path.join(root,file),...args],{cwd:root,encoding:'utf8'});
try {
  const files=['site/games/execution.dl','site/games/river/types.dl','site/games/river/model.dl',
    'site/games/river/trace.dl','tests/web/fixtures/river_solution.dl','tests/web/fixtures/river_replay.dl'];
  const sources=Object.fromEntries(await Promise.all(files.map(async file=>[file,await fs.readFile(path.join(root,file),'utf8')])));
  const request=path.join(directory,'project.json');
  await fs.writeFile(request,JSON.stringify({sources,jobs:[
    {entry:'tests/web/fixtures/river_solution.dl',kind:'wasm',traced:true,output:'solution.wasm'},
    {entry:'tests/web/fixtures/river_replay.dl',kind:'wasm',output:'replay.wasm'}
  ]}));
  run('scripts/compile_project.mjs',[request,directory]);
  const stdout=run('scripts/run_program.mjs',[path.join(directory,'solution.wasm')]);
  assert(stdout.includes('DYNLEX|TRACE|'),'The game program was not compiled with source tracing');
  assert(stdout.includes('RIVER|SUCCESS\n'));
  const input=path.join(directory,'files.json');
  await fs.writeFile(input,JSON.stringify({'/input.trace':stdout}));
  const replay=run('scripts/run_program.mjs',[path.join(directory,'replay.wasm'),input]);
  assert.equal(replay,"The browser compiler's traced river solution replays in DynLex with source ranges, functions and conditions.\n");
  process.stdout.write(replay);
} finally { await fs.rm(directory,{recursive:true,force:true}); }
