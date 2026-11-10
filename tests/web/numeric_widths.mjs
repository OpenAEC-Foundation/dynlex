// Intrinsic type admission must match native and browser code generation.
import assert from 'node:assert/strict';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const directory=path.resolve(process.argv[2]||'src/web/ide/public/compiler');
const factory=(await import(pathToFileURL(path.join(directory,'dynlex_web.js')).href)).default;
const compiler=await factory({locateFile:name=>path.join(directory,name)});
const call=(name,result=null,types=[],values=[])=>compiler.ccall(name,result,types,values);
call('dynlex_web_init');
const compile=source=>{
  call('dynlex_web_set_main_source',null,['string'],[source]);
  return call('dynlex_web_compile_and_emit_wasm','number');
};
for(const [kind,width,requirement] of [
  ['int',24,'8, 16, 32, or 64'],['uint',128,'8, 16, 32, or 64'],['integer',40,'8, 16, 32, or 64'],
  ['any',24,'8, 16, 32, or 64'],['float',8,'32 or 64'],['float',16,'32 or 64'],['float',128,'32 or 64'],
  ['bool',8,'a numeric type kind that accepts a bit width']
]) {
  assert.equal(compile(`@intrinsic("discard", @intrinsic("type", "${kind}", ${width}))\n`),1);
  const {diagnostics}=JSON.parse(call('dynlex_web_get_diagnostics_json','string'));
  assert.equal(diagnostics.length,1);
  assert(diagnostics[0].message.includes(requirement),diagnostics[0].message);
}
let valid=0;
for(const kind of ['int','uint','integer','float','any']) for(const width of (kind==='float'?[32,64]:[8,16,32,64])) {
  const type=`@intrinsic("type", "${kind}", ${width})`;
  const target=kind==='any'?`@intrinsic("and", ${type}, @intrinsic("type", "int"))`:type;
  assert.equal(compile(`@intrinsic("store", value, @intrinsic("cast", 7, ${target}))\n`),0,
    call('dynlex_web_get_diagnostics_json','string'));
  assert(WebAssembly.validate(Buffer.from(call('dynlex_web_get_output_wasm_base64','string'),'base64')));
  valid++;
}
console.log(`Browser type admission rejects8 unsupported widths and compiles${valid} supported type/width combinations.`);
