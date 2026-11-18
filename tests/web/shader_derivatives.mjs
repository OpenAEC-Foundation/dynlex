import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createWgslTranslator} from '../../web/wgsl-translator.js';

const directory=path.resolve(process.argv[2]??'build-web');
const factory=(await import(pathToFileURL(path.join(directory,'dynlex_web.js')))).default;
const compiler=await factory({locateFile:name=>path.join(directory,name)});
const translator=await createWgslTranslator(await readFile(path.join(directory,'dynlex_wgsl_translator.wasm')));
const call=(name,result=null,types=[],values=[])=>compiler.ccall(name,result,types,values);
call('dynlex_web_init');
call('dynlex_web_set_main_source',null,['string'],[await readFile(new URL('../runtime/graphics_shaders/derivatives.dl',import.meta.url),'utf8')]);
assert.equal(call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],['fragment']),0,call('dynlex_web_get_diagnostics_json','string'));
const reflection=JSON.parse(call('dynlex_web_get_shader_reflection_json','string'));
assert.deepEqual(reflection,{resources:[],uniforms:[{name:'mode',group:0,binding:0,type:{scalar:'sint32',rows:1,columns:1}}],vertexInputs:[]});
const bytes=Buffer.from(call('dynlex_web_get_output_shader_spirv_base64','string'),'base64');
const stages=translator.translateBoth(bytes,'fragment');
assert.match(stages.webgpu,/dpdx\(/);assert.match(stages.webgpu,/dpdy\(/);
assert.match(stages.webgl,/dFdx\(/);assert.match(stages.webgl,/dFdy\(/);
const cases=[[.25,0,0],[.125,0,0],[.125,.25,0],[.25,-.125,0],[.125,.25,.375],[.25,-.125,.0625],[-.25,.25,.375],[.375,-.125,.0625]].map(channels=>channels.map(value=>value*.5+.5));
if(process.argv[3]) await writeFile(process.argv[3],JSON.stringify({webgpu:stages.webgpu,webgl:stages.webgl,
  reflection:{...reflection,glslBindings:stages.bindings,glslUniforms:stages.uniforms},cases}));
console.log('Browser shaders preserve horizontal/vertical derivatives and float32 scalar/vector vocabulary.');
