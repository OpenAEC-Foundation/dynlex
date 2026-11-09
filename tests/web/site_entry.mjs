import assert from 'node:assert/strict';
import { recordWebsiteProgram } from './recording_runtime.mjs';

const { output, requests, cancelled } = await recordWebsiteProgram(process.argv[2], { cancelRequest: () => true });
assert.equal(output, 'Shared code links retain Unicode source, filenames, autorun and edits across repeated locations.\n');
assert.equal(requests[0].service, 'compiler');
assert.equal(requests[0].payload.entry, 'linked.dl');
assert.equal(requests[0].payload.sources['linked.dl'], 'import lib/std.dl\nprint line "linked é😀"');
assert.deepEqual(requests.slice(1).map(request => request.service), ['log', 'log']);
assert.deepEqual(cancelled, [1], 'Replacing the source entry did not stop its previous program');
console.log('Website shared-code entry tests passed.');
