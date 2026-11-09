// Test-only browser capabilities for DynLex controller programs. No UI host or window.
import { readFile } from 'node:fs/promises';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../../src/web/ide/public/compiler/runtimeImports.js';

export async function recordWebsiteProgram(file, { renderShader = () => { throw new Error('Unexpected shader rendering'); }, cancelRequest = () => {
  throw new Error('The controller unexpectedly cancelled a platform request');
} } = {}) {
  const bytes = await readFile(file);
  const output = [], requests = [], cancelled = [];
  const runtime = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
  const decoder = new TextDecoder(), memory = runtime.env.__linear_memory;
  const text = (pointer, count) => decoder.decode(new Uint8Array(memory.buffer, pointer, count));
  runtime.env.ui_request = (surface, name, pointer, count) => {
    const data = new Uint8Array(memory.buffer); let end = name;
    while (data[end]) end++;
    requests.push({ service: text(name, end - name), payload: JSON.parse(text(pointer, count)) });
    return requests.length;
  };
  runtime.env.ui_cancel_request = id => { cancelled.push(id); return cancelRequest(id); };
  runtime.env.ui_shader_frame = (resource, width, height, pointer, count) =>
    renderShader({ resource, width, height, values: JSON.parse(text(pointer, count)) });
  runtime.env.ui_shader_geometry = () => { throw new Error('Unexpected shader geometry upload'); };
  const { instance } = await WebAssembly.instantiate(bytes, runtime);
  try { instance.exports.main(); }
  catch (error) { throw new Error(output.join(''), { cause: error }); }
  return { output: output.join(''), requests, cancelled };
}
