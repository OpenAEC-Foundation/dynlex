// Inspect the actual browser compiler output before any host layout is built.
import assert from 'node:assert/strict';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const directory=path.resolve(process.argv[2]??'src/web/ide/public/compiler');
const factory=(await import(pathToFileURL(path.join(directory,'dynlex_web.js')).href)).default;
const compiler=await factory({locateFile:name=>path.join(directory,name)});
const call=(name,result=null,types=[],values=[])=>compiler.ccall(name,result,types,values);
call('dynlex_web_init');
const f32='@intrinsic("type", "float", 32)',u32='@intrinsic("type", "uint", 32)';
const vector=count=>`@intrinsic("vector", ${count}, ${f32})`;
const input=(name,location,kind)=>`@intrinsic("discard", @intrinsic("shader vertex input", "${name}", ${location}, ${kind}))\n`;
const output='@intrinsic("shader output", @intrinsic("construct", @intrinsic("vector", 4), 0.0, 0.0, 0.0, 1.0))\n';
function compile(source,stage){
 call('dynlex_web_set_main_source',null,['string'],[source]);
 const status=call('dynlex_web_compile_and_emit_shader_spirv','number',['string'],[stage]);
 assert.equal(status,0,call('dynlex_web_get_diagnostics_json','string'));
 return JSON.parse(call('dynlex_web_get_shader_reflection_json','string'));
}
const source=input('normal 🌽',3,vector(3))+input('color',2,vector(4))+input('object',4,u32)+input('offset',5,'@intrinsic("vector", 2, @intrinsic("type", "int", 32))')+'@intrinsic("shader output", @intrinsic("shader input", "Position"))\n';
assert.deepEqual(compile(source,'vertex'),{resources:[],uniforms:[],vertexInputs:[
 {name:'Position',location:0,format:'float32x4'},
 {name:'color',location:2,format:'float32x4'},
 {name:'normal 🌽',location:3,format:'float32x3'},
 {name:'object',location:4,format:'uint32'},
 {name:'offset',location:5,format:'sint32x2'},
]});
assert.deepEqual(compile(input('point',0,vector(2))+output,'vertex'),{resources:[],uniforms:[],vertexInputs:[{name:'point',location:0,format:'float32x2'}]});
// Reusing a compiler must remove reflection left by its preceding stage.
assert.deepEqual(compile('@intrinsic("discard", @intrinsic("shader uniform", "time", 1, @intrinsic("type", "float", 32)))\n'+output,'fragment'),{resources:[],uniforms:[{name:'time',group:0,binding:1,type:{scalar:'float32',rows:1,columns:1}}],vertexInputs:[]});
assert.deepEqual(compile(output,'vertex'),{resources:[],uniforms:[],vertexInputs:[]});
console.log('Browser compiler reflects typed vertex inputs, Unicode names and locations without stale stage metadata.');
