// Validate the browser compiler's actual resource layout and both shader translations.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createWgslTranslator} from '../../web/wgsl-translator.js';
import {shaderArithmeticCases} from './shader_arithmetic_cases.mjs';
const directory=path.resolve(process.argv[2]??'build-web');
const factory=(await import(pathToFileURL(path.join(directory,'dynlex_web.js')).href)).default;
const compiler=await factory({locateFile:name=>path.join(directory,name)});
const translator=await createWgslTranslator(await fs.readFile(path.join(directory,'dynlex_wgsl_translator.wasm')));
const call=(name,result=null,types=[],values=[])=>compiler.ccall(name,result,types,values);
call('dynlex_web_init');
const f32='@intrinsic("type", "float", 32)',vector=(count,type)=>`@intrinsic("vector", ${count}, ${type})`;
const cases=[],artifacts=[];
for(const [name,kind] of [['float32','float'],['sint32','int'],['uint32','uint']]) {
  const scalar=`@intrinsic("type", "${kind}", 32)`;
  for(let columns=1;columns<=4;columns++) cases.push({scalar:name,rows:1,columns,kind:columns===1?scalar:vector(columns,scalar)});
}
for(let rows=2;rows<=4;rows++) for(let columns=2;columns<=4;columns++)
  cases.push({scalar:'float32',rows,columns,kind:`@intrinsic("matrix", ${rows}, ${columns}, ${f32})`});
for(const {scalar,rows,columns,kind} of cases) {
  let value=`@intrinsic("shader uniform", "camera 🌽", 3, ${kind})`;
  if(rows>1) {
    const operand=`@intrinsic("construct", ${vector(columns,f32)}, ${Array(columns).fill('1.0').join(', ')})`;
    value=`@intrinsic("extract element", @intrinsic("multiply", ${value}, ${operand}), ${rows-1})`;
  } else if(columns>1) value=`@intrinsic("extract element", ${value}, ${columns-1})`;
  let component=`@intrinsic("cast", ${value}, ${f32})`;
  if(scalar!=='float32') {
    let denominator=`@intrinsic("cast", 4294967296.0, ${f32})`;
    if(scalar==='sint32') denominator=`@intrinsic("negate", ${denominator})`;
    component=`@intrinsic("divide", ${component}, ${denominator})`;
  }
  const source=`@intrinsic("shader output", @intrinsic("construct", ${vector(4,f32)}, ${component}, 0.0, 0.0, 1.0))\n`;
  call('dynlex_web_set_main_source',null,['string'],[source]);
  assert.equal(call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],['fragment']),0,
    JSON.stringify({scalar,rows,columns,source,diagnostics:JSON.parse(call('dynlex_web_get_diagnostics_json','string'))}));
  assert.deepEqual(JSON.parse(call('dynlex_web_get_shader_reflection_json','string')),
    {resources:[],uniforms:[{name:'camera 🌽',group:0,binding:3,type:{scalar,rows,columns}}],vertexInputs:[]});
  const bytes=Buffer.from(call('dynlex_web_get_output_shader_spirv_base64','string'),'base64');
  let webgpu,webgl;
  try {webgpu=translator.translate(bytes);webgl=translator.translateGlsl(bytes,'fragment');}
  catch(error) {throw new Error(JSON.stringify({scalar,rows,columns,source}),{cause:error});}
  assert.match(webgpu,/@group\(0\) @binding\(3\)/);
  assert.match(webgl,/_group_0_binding_3_/);
  const samples=Array(rows*4).fill(0);
  samples[rows>1?(rows-1)*4:columns-1]={float32:0.25,sint32:-2147483648,uint32:4294967295}[scalar];
  artifacts.push({kind:'uniform',name:`${scalar} ${rows}x${columns}`,type:{scalar,rows,columns},samples,
    red:{float32:0.25,sint32:0.5,uint32:1}[scalar],uniforms:JSON.parse(call('dynlex_web_get_shader_reflection_json','string')).uniforms,webgpu,webgl});
}
for(const record of shaderArithmeticCases) {
  call('dynlex_web_set_main_source',null,['string'],[record.source]);
  assert.equal(call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],['fragment']),0,
    JSON.stringify({name:record.name,diagnostics:JSON.parse(call('dynlex_web_get_diagnostics_json','string'))}));
  const reflection=JSON.parse(call('dynlex_web_get_shader_reflection_json','string'));
  assert.deepEqual(reflection,{resources:[],uniforms:[{name:'data',group:0,binding:3,type:record.type}],vertexInputs:[]});
  const bytes=Buffer.from(call('dynlex_web_get_output_shader_spirv_base64','string'),'base64');
  artifacts.push({kind:'arithmetic',...record,uniforms:reflection.uniforms,webgpu:translator.translate(bytes),webgl:translator.translateGlsl(bytes,'fragment')});
}
if(process.argv[3]) await fs.writeFile(process.argv[3],JSON.stringify(artifacts));
console.log('Browser uniform reflection and WGSL/GLSL translations preserve every supported scalar, vector and matrix shape.');
