import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../../src/web/ide/public/compiler/runtimeImports.js';

const bytes = await readFile(process.argv[2]);
const output = [], requests = [];
const runtime = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
const decoder = new TextDecoder();
const memory = runtime.env.__linear_memory;
const text = (pointer, count) => decoder.decode(new Uint8Array(memory.buffer, pointer, count));
runtime.env.ui_request = (surface, name, pointer, count) => {
  const data = new Uint8Array(memory.buffer); let end = name;
  while (data[end]) end++;
  assert.equal(text(name, end - name), 'language');
  requests.push(JSON.parse(text(pointer, count)));
  return requests.length;
};
const { instance } = await WebAssembly.instantiate(bytes, runtime);
instance.exports.main();
assert.equal(output.join(''), 'Website analysis synchronizes documents, converts Unicode tokens and ignores stale diagnostics.\n');
assert.equal(requests[0].operation, 'project');
assert.deepEqual(requests.slice(1).map(request => request.message.method), [
  'initialize', 'initialized', 'textDocument/didOpen', 'textDocument/semanticTokens/full', 'textDocument/didChange'
]);
assert.equal(requests[3].message.params.textDocument.version, 1);
assert.equal(requests[5].message.params.textDocument.version, 2);
assert.equal(requests[5].message.params.contentChanges[0].text, 'newer text');
console.log('Website language controller tests passed.');
