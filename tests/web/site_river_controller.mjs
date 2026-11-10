import assert from 'node:assert/strict';
import { recordWebsiteProgram } from './recording_runtime.mjs';
const {output,requests,cancelled}=await recordWebsiteProgram(process.argv[2],{cancelRequest:()=>true});
assert.equal(output,'River controls select the challenge, compile a single step, replay, change speed, reset, reveal hints and clear stale edits.\n');
assert.equal(requests[0].service,'compiler');
assert.equal(requests[0].payload.traced,true);
assert.equal(requests[1].service,'program');
assert.deepEqual(cancelled,[]);
console.log('DynLex River interface and controller tests passed.');
