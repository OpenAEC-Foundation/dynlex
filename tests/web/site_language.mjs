import assert from 'node:assert/strict';
import { recordWebsiteProgram } from './recording_runtime.mjs';

const { output, requests: operations } = await recordWebsiteProgram(process.argv[2]);
const requests = operations.map(({ service, payload }) => { assert.equal(service, 'language'); return payload; });
assert.equal(output, 'Website analysis synchronizes documents, converts Unicode tokens and ignores stale diagnostics.\n');
assert.equal(requests[0].operation, 'project');
assert.deepEqual(requests.slice(1).map(request => request.message?.method), [
  'initialize', 'initialized', 'textDocument/didOpen', 'textDocument/semanticTokens/full', 'textDocument/didChange',
  'textDocument/didClose', undefined, 'textDocument/didOpen'
]);
assert.equal(requests[3].message.params.textDocument.version, 1);
assert.equal(requests[5].message.params.textDocument.version, 2);
assert.equal(requests[5].message.params.contentChanges[0].text, 'newer text');
assert.equal(requests[6].message.params.textDocument.uri, 'file:///workspace/main.dl');
assert.deepEqual(Object.keys(requests[7].sources), ['renamed.dl']);
assert.equal(requests[8].message.params.textDocument.uri, 'file:///workspace/renamed.dl');
console.log('Website language controller tests passed.');
