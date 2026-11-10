import assert from 'node:assert/strict';
import { recordWebsiteProgram } from './recording_runtime.mjs';

const { output, requests: operations } = await recordWebsiteProgram(process.argv[2]);
const requests = operations.map(({ service, payload }) => { assert.equal(service, 'language'); return payload; });
assert.equal(output, 'Studio and River share source projects, Unicode colors, mapped diagnostics and stale-result isolation.\n');
const projects = requests.filter(request => request.operation === 'project');
assert.deepEqual(projects.map(request => Object.keys(request.sources)), [['main.dl'], ['renamed.dl'], ['river.dl'], ['shader.dl']]);
assert.equal(projects[2].sources['river.dl'], 'import lib/river_challenge.dl\n\nget the hay in the boat');
const changes = requests.filter(request => request.message?.method === 'textDocument/didChange');
assert.equal(changes.length, 1);
assert.equal(changes[0].message.params.textDocument.version, 2);
assert.equal(changes[0].message.params.contentChanges[0].text, 'newer text');
const opens = requests.filter(request => request.message?.method === 'textDocument/didOpen');
assert.deepEqual(opens.map(request => [request.message.params.textDocument.uri, request.message.params.textDocument.version]), [
  ['file:///workspace/main.dl', 1], ['file:///workspace/renamed.dl', 1], ['file:///workspace/river.dl', 1]
]);
assert.equal(requests.filter(request => request.operation === 'reset').length, 1);
assert.deepEqual(requests.at(-1).message.params.initializationOptions.dynlex.analysisProfiles,
  [{ target: 'spirv', shaderStage: 'fragment' }]);
console.log('Website shared language controller tests passed.');
