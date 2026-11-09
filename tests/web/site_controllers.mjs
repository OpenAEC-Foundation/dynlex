import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../../src/web/ide/public/compiler/runtimeImports.js';

const bytes = await readFile(process.argv[2]);
const output = [], requests = [], cancelled = [];
const runtime = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
const decoder = new TextDecoder();
const memory = runtime.env.__linear_memory;
const text = (pointer, count) => decoder.decode(new Uint8Array(memory.buffer, pointer, count));
const cstring = pointer => {
  const bytes = new Uint8Array(memory.buffer); let end = pointer;
  while (bytes[end]) end++;
  return text(pointer, end - pointer);
};
runtime.env.ui_request = (surface, service, pointer, count) => {
  requests.push({ service: cstring(service), payload: JSON.parse(text(pointer, count)) });
  return requests.length;
};
runtime.env.ui_cancel_request = id => { cancelled.push(id); return id === 3; };
const { instance } = await WebAssembly.instantiate(bytes, runtime);
instance.exports.main();
assert.equal(output.join(''), 'Website compilation, streaming output, Stop and queued-reply ownership work.\n');
assert.equal(requests[0].service, 'compiler');
assert.equal(requests[0].payload.target, 'wasm');
assert.match(requests[0].payload.sources['main.dl'], /square 8/);
assert.deepEqual(requests[1], { service: 'program', payload: { artifact: 42, invocation: 1, outputLimit: 1048576 } });
assert.deepEqual(cancelled, [3, 4]);
assert.deepEqual(requests[4], { service: 'compiler', payload: { operation: 'release', artifact: 99 } });
console.log('Website program controller tests passed.');
