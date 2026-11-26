import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const root=path.resolve(import.meta.dirname,'../..');
const directory=path.resolve(process.argv[2]??path.join(root,'src/web/ide/public/compiler'));
const factory=(await import(pathToFileURL(path.join(directory,'dynlex_web.js')))).default;
const compiler=await factory({locateFile:name=>path.join(directory,name)});
const call=(name,result=null,types=[],values=[])=>compiler.ccall(name,result,types,values);
call('dynlex_web_init');
for(const name of ['static_pure_call_values','static_aggregate_reads']) {
  const source=await fs.readFile(path.join(root,'tests/required',name,'main.dl'),'utf8');
  assert.equal(call('dynlex_web_set_import_sources','number',['string'],[JSON.stringify({'main.dl':source})]),0);
  assert.equal(call('dynlex_web_set_entry_file','number',['string'],['main.dl']),0);
  assert.equal(call('dynlex_web_compile_and_emit_wasm_debugged','number'),0,
    call('dynlex_web_get_diagnostics_json','string'));
  const bytes=Buffer.from(call('dynlex_web_get_output_wasm_base64','string'),'base64');
  assert(WebAssembly.validate(bytes),name+' emitted invalid debug Wasm');
}
console.log('Debug browser compilation preserves required static constant values.');
