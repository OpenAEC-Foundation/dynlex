import assert from 'node:assert/strict';
import { recordWebsiteProgram } from './recording_runtime.mjs';

const { output, requests, cancelled } = await recordWebsiteProgram(process.argv[2], { cancelRequest: () => true });
assert.equal(output, 'Shared code links retain Unicode source, filenames, autorun and edits across repeated locations.\n');
assert.equal(requests[0].service, 'compiler');
assert.equal(requests[0].payload.entry, 'linked.dl');
assert.equal(requests[0].payload.sources['linked.dl'], 'import lib/std.dl\nprint line "linked é😀"');
assert.equal(requests[0].payload.sources['provided.dl'], 'a project helper means: 7');
assert.deepEqual(requests.slice(1).map(request => request.service), ['log', 'log', 'log', 'log', 'fetch', 'fetch', 'compiler']);
assert.equal(requests[5].payload.url, 'assets/shaders/endless-terrain.dl');
assert.equal(requests[6].payload.url, 'assets/shaders/nano-choreography.dl');
assert.equal(requests[7].payload.target, 'shader');
assert.equal(requests[7].payload.sources['preview.dl'], 'shader source');
assert.deepEqual(cancelled, [1, 6, 7], 'Replacing the source entry did not stop its previous program or scene fetch');
console.log('Website shared-code entry tests passed.');
