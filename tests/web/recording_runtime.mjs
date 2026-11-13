// Test-only browser capabilities for DynLex controller programs. No UI host or window.
import { readFile } from 'node:fs/promises';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../../src/web/ide/public/compiler/runtimeImports.js';

export async function recordWebsiteProgram(file, { synchronousService=()=>{throw new Error('Unexpected immediate platform call');}, hooks={}, shaderQuery = () => {throw new Error('Unexpected shader range query');}, renderShader = () => { throw new Error('Unexpected shader rendering'); }, cancelRequest = () => {
  throw new Error('The controller unexpectedly cancelled a platform request');
} } = {}) {
  const bytes = await readFile(file);
  const output = [], requests = [], cancelled = [], calls=[],shaderCalls=[],uniformCalls=[];
  const shaderInputs=new Map();
  const runtime = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
  const decoder = new TextDecoder(), memory = runtime.env.__linear_memory;
  const text = (pointer, count) => decoder.decode(new Uint8Array(memory.buffer, pointer, count));
  runtime.env.ui_call=(surface,name,pointer,count,address,length)=>{
    const bytes=new Uint8Array(memory.buffer);let end=name;while(bytes[end]) end++;
    const service=text(name,end-name),payload=JSON.parse(text(pointer,count));calls.push({service,payload});
    try {
      const encoded=new TextEncoder().encode(JSON.stringify(synchronousService(service,payload)));
      const result=runtime.env.malloc(encoded.length+1);
      const buffer=new Uint8Array(memory.buffer,result,encoded.length+1);buffer.set(encoded);buffer[encoded.length]=0;
      const view=new DataView(memory.buffer);view.setUint32(address,result,true);view.setInt32(length,encoded.length,true);
      return 0;
    } catch {const view=new DataView(memory.buffer);view.setUint32(address,0,true);view.setInt32(length,0,true);return 1;}
  };
  Object.assign(runtime.env,hooks);
  runtime.env.ui_request = (surface, name, pointer, count) => {
    const data = new Uint8Array(memory.buffer); let end = name;
    while (data[end]) end++;
    requests.push({ service: text(name, end - name), payload: JSON.parse(text(pointer, count)) });
    return requests.length;
  };
  runtime.env.ui_cancel_request = id => { cancelled.push(id); return cancelRequest(id); };
  runtime.env.ui_shader_uniform = (surface,resource,pass,group,binding,scalar,rows,columns,pointer,count) => {
    const input={surface,resource,pass,group,binding,scalar,rows,columns,data:[...new Uint8Array(memory.buffer,pointer,count)]};
    uniformCalls.push(input);shaderInputs.set(`${surface}:${resource}:${pass}:${group}:${binding}`,input);
  };
  runtime.env.ui_shader_frame = (surface,resource,width,height) =>
    renderShader({surface,resource,width,height,inputs:[...shaderInputs.values()].filter(input=>input.surface===surface&&input.resource===resource)});
  runtime.env.ui_shader_vertex_buffer = (surface,resource,pass,slot,pointer,count) =>
    shaderCalls.push({operation:'vertex',surface,resource,pass,slot,data:[...new Uint8Array(memory.buffer,pointer,count)]});
  runtime.env.ui_shader_image = (surface,resource,name,length,level,layer,pointer,count) =>
    shaderCalls.push({operation:'image',surface,resource,name:text(name,length),level,layer,data:[...new Uint8Array(memory.buffer,pointer,count)]});
  runtime.env.ui_shader_index_buffer = (surface,resource,pass,pointer,count) =>
    shaderCalls.push({operation:'index',surface,resource,pass,data:[...new Uint32Array(memory.buffer,pointer,count)]});
  runtime.env.ui_shader_draws = (surface,resource,pass,pointer,count) =>
    shaderCalls.push({operation:'draws',surface,resource,pass,data:[...new Uint32Array(memory.buffer,pointer,count*6)]});
  runtime.env.ui_shader_buffer_bytes = (surface,resource,pass,slot) => shaderQuery({operation:'bytes',surface,resource,pass,slot});
  runtime.env.ui_shader_buffer_records = (surface,resource,pass,slot) => shaderQuery({operation:'records',surface,resource,pass,slot});
  runtime.env.ui_shader_index_count = (surface,resource,pass) => shaderQuery({operation:'indices',surface,resource,pass});
  runtime.env.ui_shader_close = (surface,resource) => {shaderCalls.push({operation:'close',surface,resource});};
  const { instance } = await WebAssembly.instantiate(bytes, runtime);
  try { instance.exports.main(); }
  catch (error) { throw new Error(output.join(''), { cause: error }); }
  return { output: output.join(''), requests, cancelled, calls, shaderCalls, uniformCalls };
}
