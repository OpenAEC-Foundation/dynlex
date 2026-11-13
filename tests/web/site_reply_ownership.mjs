import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const result=await recordWebsiteProgram(process.argv[2],{cancelRequest:()=>false,allocationLimit:32*1024*1024});
assert.equal(result.output,'Cancelled input replies accept their declared result shapes and release only owned compiler, graphics and audio resources.\n');
assert.deepEqual(result.requests.filter(request=>request.service!=='fixture'),[
  {service:'compiler',payload:{operation:'release',artifact:99}},
  {service:'audio',payload:{operation:'unload',buffers:[71,72]}}
]);
assert.deepEqual(result.shaderCalls.filter(call=>call.operation==='close').map(call=>call.resource),[41,42,43]);
assert.equal(result.allocations.live,0,'Retiring queued replies must release every owned allocation.');
console.log('Production input-loop cancellation regression passed: null, array and object replies; exact artifact, graphics and decoded-audio cleanup.');
