import assert from 'node:assert/strict';
import { recordWebsiteProgram } from './recording_runtime.mjs';

const { output, requests, cancelled } = await recordWebsiteProgram(process.argv[2], { cancelRequest: id => id === 3 });
assert.equal(output, 'Website compilation, streaming output, Stop and queued-reply ownership work.\n');
assert.equal(requests[0].service, 'compiler');
assert.equal(requests[0].payload.target, 'cpu');
assert.match(requests[0].payload.sources['main.dl'], /square 8/);
assert.equal(requests[0].payload.sources['helper.dl'], 'helper means: 37');
assert.deepEqual(requests[1], { service: 'program', payload: { artifact: 42, invocation: 1, outputLimit: 1048576 } });
assert.deepEqual(cancelled, [3, 4]);
assert.deepEqual(requests[4], { service: 'compiler', payload: { operation: 'release', artifact: 99 } });
console.log('Website program controller tests passed.');
