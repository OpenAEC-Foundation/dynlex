import assert from 'node:assert/strict';
import { recordWebsiteProgram } from './recording_runtime.mjs';

const frames = [];
const { output, requests, cancelled, shaderCalls } = await recordWebsiteProgram(process.argv[2], {
  cancelRequest: () => false, renderShader: frame => { frames.push(frame); return 100; }
});
assert.equal(output, 'Shader compilation, last-successful previews, playback and cancellation share DynLex state.\n');
assert.equal(requests[0].service, 'compiler');
assert.equal(requests[0].payload.target, 'shader');
assert.equal(requests[0].payload.entry, 'shader.dl');
assert.deepEqual(requests[0].payload.stages, ['fragment']);
assert.equal(requests[1].service, 'graphics');
assert.equal(requests[1].payload.passes[0].fragment.webgpu.kind, 'source');
assert.deepEqual(cancelled, [5]);
assert.deepEqual(frames.map(frame => [frame.width, frame.height, new DataView(Uint8Array.from(frame.inputs.find(input=>input.binding===0).data).buffer).getFloat32(0,true)]), [
  [320, 180, 0], [320, 180, 0.5], [321, 180, 0.5]
], 'An unchanged shader frame was rendered again');
assert.deepEqual(shaderCalls.filter(call=>call.operation==='close').map(call=>call.resource), [43]);
assert.equal(requests.length,5,'Closing a shader must not leave an asynchronous release request behind');
console.log('Website shader controller tests passed.');
