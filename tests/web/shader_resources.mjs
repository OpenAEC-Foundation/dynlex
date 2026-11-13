// Inspect the emitted interface and translate actual samples for both browser backends.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createWgslTranslator} from '../../web/wgsl-translator.js';

const directory=path.resolve(process.argv[2]??'build-web');
const factory=(await import(pathToFileURL(path.join(directory,'dynlex_web.js')).href)).default;
const compiler=await factory({locateFile:name=>path.join(directory,name)});
const translator=await createWgslTranslator(await fs.readFile(path.join(directory,'dynlex_wgsl_translator.wasm')));
const call=(name,result=null,types=[],values=[])=>compiler.ccall(name,result,types,values);
call('dynlex_web_init');
const source=await fs.readFile(new URL('../runtime/graphics_shaders/resources.dl',import.meta.url),'utf8');
const artifacts=[];
for(const stage of ['vertex','fragment']) {
  call('dynlex_web_set_main_source',null,['string'],[source]);
  assert.equal(call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],[stage]),0,
    call('dynlex_web_get_diagnostics_json','string'));
  const reflection=JSON.parse(call('dynlex_web_get_shader_reflection_json','string'));
  assert.deepEqual(reflection.resources,[
    {name:'albedo',group:0,binding:1,kind:'texture',dimension:'2d',sampleType:'float'},
    {name:'sky',group:0,binding:2,kind:'texture',dimension:'cube',sampleType:'float'},
    {name:'shadow',group:0,binding:3,kind:'texture',dimension:'2d',sampleType:'depth'},
    {name:'linear',group:0,binding:4,kind:'sampler',comparison:false},
    {name:'comparison',group:0,binding:5,kind:'sampler',comparison:true},
    {name:'detail 🌽',group:0,binding:6,kind:'texture',dimension:'2d',sampleType:'float'},
  ]);
  const bytes=Buffer.from(call('dynlex_web_get_output_shader_spirv_base64','string'),'base64');
  const webgpu=translator.translate(bytes),webgl=translator.translateGlsl(bytes,stage);
  assert.match(webgpu,/texture_cube<f32>/);
  assert.match(webgpu,/texture_depth_2d/);
  assert.match(webgpu,/sampler_comparison/);
  assert.match(webgpu,/textureSampleCompareLevel/);
  assert.match(webgl,/samplerCube/);
  assert.match(webgl,/sampler2DShadow/);
  artifacts.push({stage,reflection,webgpu,webgl});
}
const vocabulary=await fs.readFile(new URL('../runtime/graphics_shaders/resource_vocabulary.dl',import.meta.url),'utf8');
for(const stage of ['vertex','fragment']) {
  call('dynlex_web_set_main_source',null,['string'],[vocabulary]);
  assert.equal(call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],[stage]),0,
    call('dynlex_web_get_diagnostics_json','string'));
  const reflection=JSON.parse(call('dynlex_web_get_shader_reflection_json','string'));
  assert.deepEqual(reflection.resources.map(resource=>resource.name),['paint','sky','shadow','smooth','comparison']);
  const bytes=Buffer.from(call('dynlex_web_get_output_shader_spirv_base64','string'),'base64');
  translator.translate(bytes);translator.translateGlsl(bytes,stage);
}
const indexOrder=await fs.readFile(new URL('../runtime/graphics_shaders/uniform_index_order.dl',import.meta.url),'utf8');
call('dynlex_web_set_main_source',null,['string'],[indexOrder]);
assert.equal(call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],['fragment']),0,
  call('dynlex_web_get_diagnostics_json','string'));
const indexBytes=Buffer.from(call('dynlex_web_get_output_shader_spirv_base64','string'),'base64');
translator.translate(indexBytes);translator.translateGlsl(indexBytes,'fragment');
call('dynlex_web_set_main_source',null,['string'],[
  '@intrinsic("discard", @intrinsic("shader image", "unused", 9, "2d", @intrinsic("equal", 0, 1)))\n' +
  '@intrinsic("shader output", 1.0)\n'
]);
assert.equal(call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],['fragment']),0);
assert.deepEqual(JSON.parse(call('dynlex_web_get_shader_reflection_json','string')).resources,[],
  'Reusing the compiler retained resources from its previous shader');
if(process.argv[3]) await fs.writeFile(process.argv[3],JSON.stringify(artifacts));
console.log('Browser resource reflection and WGSL/GLSL preserve texture dimensions, shared samplers and depth comparisons.');
